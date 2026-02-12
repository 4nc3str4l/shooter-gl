#include "common.h"
#include "game.h"
#include "network.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>
#include <vector>
#include <string>
#include <chrono>
#include <thread>

// ============================================================================
// Server State
// ============================================================================

struct ClientConnection {
    sockaddr_in addr;
    uint32_t    lastInputSeq = 0;
    float       timeoutTimer = 0;
    int         playerId = -1;
    InputState  lastInput;
    bool        active = false;
};

enum class AIState : uint8_t {
    PATROL, CHASE, ATTACK, RETREAT, PICKUP_WEAPON
};

struct BotData {
    int         playerId = -1;
    AIState     aiState = AIState::PATROL;
    Vec3        targetPos;
    int         targetPlayerId = -1;
    float       stateTimer = 0;
    float       reactionDelay = 0.5f;
    float       reactionTimer = 0;
    int         currentWaypoint = 0;
    Vec3        lastPos;
    float       stuckTimer = 0;
    float       aimJitter = 0.03f;  // Radians of aim randomness
    InputState  input;

    // A* pathfinding
    std::vector<int> path;          // Waypoint indices forming current path
    int              pathIndex = 0; // Current position in path
    float            pathAge = 0;   // Time since last pathfind
    float            jumpCooldown = 0;
    float            combatJumpTimer = 0;
    float            strafeDir = 1.0f;
    float            strafeTimer = 0;
};

static GameMap          g_map;
static PlayerData       g_players[MAX_PLAYERS];
static ClientConnection g_clients[MAX_PLAYERS];
static BotData          g_bots[MAX_PLAYERS];
static int              g_numBots = 0;
static uint32_t         g_serverTick = 0;
static UDPSocket        g_socket;
static bool             g_running = true;
static VehicleData      g_vehicles[MAX_VEHICLES];
static int              g_numVehicles = 0;

// Teams & CTF
static int              g_teamScores[2] = {0, 0};
static FlagData         g_flags[2];
static int              g_nextTeam = 0; // Round-robin team assignment

// Tornados
static TornadoData      g_tornados[MAX_TORNADOS];
static float            g_tornadoSpawnTimer = 30.0f; // First tornado after 30s

// Kill feed
struct KillEvent {
    int killer, victim;
    float timer;
};
static std::vector<KillEvent> g_killFeed;

// ============================================================================
// Utility
// ============================================================================

static float randf() { return (float)rand() / RAND_MAX; }
static float randf(float mn, float mx) { return mn + randf() * (mx - mn); }

// Forward declarations
static void vehicleDamage(int victimId, int attackerId, int damage);
static bool canSeePlayer(int botId, int targetId);

// ============================================================================
// A* Pathfinding on Waypoint Graph
// ============================================================================

static std::vector<int> findPath(const GameMap& map, int startWP, int goalWP) {
    const auto& wps = map.waypoints();
    int numWP = (int)wps.size();
    if (startWP < 0 || goalWP < 0 || startWP >= numWP || goalWP >= numWP)
        return {};
    if (startWP == goalWP) return {startWP};

    struct Node {
        float g = 1e30f, f = 1e30f;
        int parent = -1;
        bool closed = false;
    };
    std::vector<Node> nodes(numWP);
    nodes[startWP].g = 0;
    nodes[startWP].f = (wps[goalWP].position - wps[startWP].position).length();

    // Simple open list (vector, find min each time - fine for <50 waypoints)
    std::vector<int> open;
    open.push_back(startWP);

    while (!open.empty()) {
        // Find node with lowest f
        int bestIdx = 0;
        for (int i = 1; i < (int)open.size(); i++) {
            if (nodes[open[i]].f < nodes[open[bestIdx]].f)
                bestIdx = i;
        }
        int current = open[bestIdx];
        open.erase(open.begin() + bestIdx);

        if (current == goalWP) {
            // Reconstruct path
            std::vector<int> path;
            int n = goalWP;
            while (n != -1) {
                path.push_back(n);
                n = nodes[n].parent;
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        nodes[current].closed = true;

        for (int neighbor : wps[current].neighbors) {
            if (nodes[neighbor].closed) continue;
            float tentG = nodes[current].g +
                          (wps[neighbor].position - wps[current].position).length();
            if (tentG < nodes[neighbor].g) {
                nodes[neighbor].parent = current;
                nodes[neighbor].g = tentG;
                nodes[neighbor].f = tentG +
                    (wps[goalWP].position - wps[neighbor].position).length();
                // Add to open if not already there
                bool found = false;
                for (int o : open) { if (o == neighbor) { found = true; break; } }
                if (!found) open.push_back(neighbor);
            }
        }
    }

    return {}; // No path found
}

// Find the nearest waypoint the bot can reach (closest by distance)
static int findNearestWaypointToPos(const GameMap& map, const Vec3& pos) {
    return map.findNearestWaypoint(pos);
}

// ============================================================================

static int findFreeSlot() {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g_players[i].state == PlayerState::DISCONNECTED) return i;
    }
    return -1;
}

static void spawnPlayer(int id) {
    // Use team-specific spawns
    int team = g_players[id].teamId;
    const auto& spawns = g_map.teamSpawns(team);
    const auto& fallback = g_map.spawns();
    const auto& chosen = spawns.empty() ? fallback : spawns;
    int si = rand() % chosen.size();
    g_players[id].position = chosen[si].position;
    g_players[id].yaw = chosen[si].yaw;
    g_players[id].pitch = 0;
    g_players[id].velocity = {0, 0, 0};
    g_players[id].health = MAX_HEALTH;
    g_players[id].state = PlayerState::ALIVE;
    g_players[id].fireCooldown = 0;
    g_players[id].respawnTimer = 0;
    g_players[id].vehicleId = -1;
    g_players[id].isDriver = false;
    g_players[id].abilityCooldown = 0;
    g_players[id].spotted = false;
    g_players[id].spottedTimer = 0;

    // Apply class loadout
    const auto& cdef = getClassDef(g_players[id].playerClass);
    g_players[id].currentWeapon = cdef.primaryWeapon;
    g_players[id].ammo = getWeaponDef(cdef.primaryWeapon).magSize;
    g_players[id].health = MAX_HEALTH + cdef.extraHealth;
}

// ============================================================================
// Networking
// ============================================================================

static void sendSnapshot(const sockaddr_in& addr, int clientPlayerId) {
    uint8_t buf[16384];
    int offset = 0;

    SnapshotPacket hdr;
    hdr.serverTick = g_serverTick;
    if (clientPlayerId >= 0 && clientPlayerId < MAX_PLAYERS) {
        hdr.ackInputSeq = g_clients[clientPlayerId].lastInputSeq;
    }
    hdr.teamScores[0] = (uint8_t)std::clamp(g_teamScores[0], 0, 255);
    hdr.teamScores[1] = (uint8_t)std::clamp(g_teamScores[1], 0, 255);

    // Count active players
    uint8_t count = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g_players[i].state != PlayerState::DISCONNECTED) count++;
    }
    hdr.numPlayers = count;

    memcpy(buf + offset, &hdr, sizeof(hdr));
    offset += sizeof(hdr);

    // Player states
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g_players[i].state == PlayerState::DISCONNECTED) continue;
        NetPlayerState np;
        np.playerId = i;
        np.state = (uint8_t)g_players[i].state;
        np.x = g_players[i].position.x;
        np.y = g_players[i].position.y;
        np.z = g_players[i].position.z;
        np.yaw = g_players[i].yaw;
        np.pitch = g_players[i].pitch;
        np.health = (uint8_t)std::clamp(g_players[i].health, 0, 255);
        np.weapon = (uint8_t)g_players[i].currentWeapon;
        np.ammo = (uint8_t)std::clamp(g_players[i].ammo, 0, 255);
        np.vehicleId = g_players[i].vehicleId;
        np.teamId = g_players[i].teamId;
        np.playerClass = (uint8_t)g_players[i].playerClass;
        np.spotted = g_players[i].spotted ? 1 : 0;
        memcpy(buf + offset, &np, sizeof(np));
        offset += sizeof(np);
    }

    // Weapon pickups
    auto& pickups = g_map.weaponPickups();
    uint8_t numWeapons = (uint8_t)pickups.size();
    memcpy(buf + offset, &numWeapons, 1);
    offset += 1;

    for (const auto& wp : pickups) {
        NetWeaponState nw;
        nw.id = wp.id;
        nw.type = (uint8_t)wp.type;
        nw.x = wp.position.x;
        nw.y = wp.position.y;
        nw.z = wp.position.z;
        nw.active = wp.active ? 1 : 0;
        memcpy(buf + offset, &nw, sizeof(nw));
        offset += sizeof(nw);
    }

    // Vehicle states
    uint8_t numVehicles = (uint8_t)g_numVehicles;
    memcpy(buf + offset, &numVehicles, 1);
    offset += 1;

    for (int i = 0; i < g_numVehicles; i++) {
        NetVehicleState nv;
        nv.id = i;
        nv.type = (uint8_t)g_vehicles[i].type;
        nv.x = g_vehicles[i].position.x;
        nv.y = g_vehicles[i].position.y;
        nv.z = g_vehicles[i].position.z;
        nv.yaw = g_vehicles[i].yaw;
        nv.pitch = g_vehicles[i].pitch;
        nv.turretYaw = g_vehicles[i].turretYaw;
        nv.health = (int16_t)g_vehicles[i].health;
        nv.driverId = g_vehicles[i].driverId;
        nv.active = g_vehicles[i].active ? 1 : 0;
        nv.rotorAngle = g_vehicles[i].rotorAngle;
        memcpy(buf + offset, &nv, sizeof(nv));
        offset += sizeof(nv);
    }

    // Flag states (2 flags)
    for (int t = 0; t < 2; t++) {
        NetFlagState nf;
        nf.teamId = t;
        nf.x = g_flags[t].position.x;
        nf.y = g_flags[t].position.y;
        nf.z = g_flags[t].position.z;
        nf.carrierId = g_flags[t].carrierId;
        nf.atBase = g_flags[t].atBase ? 1 : 0;
        memcpy(buf + offset, &nf, sizeof(nf));
        offset += sizeof(nf);
    }

    // Tornado states
    uint8_t numTornados = 0;
    for (int i = 0; i < MAX_TORNADOS; i++) {
        if (g_tornados[i].active) numTornados++;
    }
    memcpy(buf + offset, &numTornados, 1);
    offset += 1;
    for (int i = 0; i < MAX_TORNADOS; i++) {
        if (!g_tornados[i].active) continue;
        NetTornadoState nt;
        nt.x = g_tornados[i].position.x;
        nt.y = g_tornados[i].position.y;
        nt.z = g_tornados[i].position.z;
        nt.radius = g_tornados[i].radius;
        nt.rotation = g_tornados[i].rotation;
        nt.active = 1;
        memcpy(buf + offset, &nt, sizeof(nt));
        offset += sizeof(nt);
    }

    g_socket.sendTo(buf, offset, addr);
}

static void broadcastSnapshot() {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g_clients[i].active) {
            sendSnapshot(g_clients[i].addr, g_clients[i].playerId);
        }
    }
}

static void handleJoin(const JoinPacket& pkt, const sockaddr_in& from) {
    // Check if already connected
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g_clients[i].active && UDPSocket::addrEqual(g_clients[i].addr, from)) {
            // Resend ack
            JoinAckPacket ack;
            ack.playerId = g_clients[i].playerId;
            g_socket.sendTo(&ack, sizeof(ack), from);
            return;
        }
    }

    int slot = findFreeSlot();
    if (slot < 0) {
        printf("Server full, rejecting player\n");
        return;
    }

    g_players[slot] = PlayerData{};
    snprintf(g_players[slot].name, sizeof(g_players[slot].name), "%s", pkt.name);
    g_players[slot].currentWeapon = WeaponType::PISTOL;
    g_players[slot].ammo = getWeaponDef(WeaponType::PISTOL).magSize;
    // Assign team (round-robin)
    g_players[slot].teamId = g_nextTeam;
    g_nextTeam = (g_nextTeam + 1) % 2;
    spawnPlayer(slot);

    g_clients[slot].addr = from;
    g_clients[slot].playerId = slot;
    g_clients[slot].active = true;
    g_clients[slot].timeoutTimer = 0;
    g_clients[slot].lastInputSeq = 0;

    JoinAckPacket ack;
    ack.playerId = slot;
    ack.numBots = g_numBots;
    g_socket.sendTo(&ack, sizeof(ack), from);

    printf("Player '%s' joined as ID %d (Team %d)\n", g_players[slot].name, slot, g_players[slot].teamId);
}

static void handleInput(const InputPacket& pkt, const sockaddr_in& from) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g_clients[i].active && UDPSocket::addrEqual(g_clients[i].addr, from)) {
            if (pkt.seq > g_clients[i].lastInputSeq) {
                g_clients[i].lastInputSeq = pkt.seq;
                g_clients[i].lastInput.keys = pkt.keys;
                g_clients[i].lastInput.yaw = pkt.yaw;
                g_clients[i].lastInput.pitch = pkt.pitch;
                g_clients[i].timeoutTimer = 0;

                // Class selection (can change anytime, applies on next spawn)
                if (pkt.classSelect < (uint8_t)PlayerClass::COUNT) {
                    PlayerClass newClass = (PlayerClass)pkt.classSelect;
                    if (newClass != g_players[i].playerClass) {
                        g_players[i].playerClass = newClass;
                        const auto& cdef = getClassDef(newClass);
                        // If alive, apply new loadout immediately
                        if (g_players[i].state == PlayerState::ALIVE) {
                            g_players[i].currentWeapon = cdef.primaryWeapon;
                            g_players[i].ammo = getWeaponDef(cdef.primaryWeapon).magSize;
                            g_players[i].health = MAX_HEALTH + cdef.extraHealth;
                        }
                        printf("Player %d switched to %s class\n", i, cdef.name);
                    }
                }
            }
            return;
        }
    }
}

static void handleDisconnect(const sockaddr_in& from) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g_clients[i].active && UDPSocket::addrEqual(g_clients[i].addr, from)) {
            printf("Player '%s' (ID %d) disconnected\n", g_players[i].name, i);
            g_players[i].state = PlayerState::DISCONNECTED;
            g_clients[i].active = false;
            return;
        }
    }
}

// ============================================================================
// Shooting
// ============================================================================

static void processShot(int shooterId) {
    PlayerData& shooter = g_players[shooterId];
    if (shooter.state != PlayerState::ALIVE) return;
    if (shooter.fireCooldown > 0) return;
    if (shooter.ammo <= 0) return;

    const auto& def = getWeaponDef(shooter.currentWeapon);
    shooter.fireCooldown = def.fireRate;
    shooter.ammo--;

    // Auto-reload: when out of ammo, refill (simulates reload)
    if (shooter.ammo <= 0) {
        shooter.ammo = def.magSize;
        shooter.fireCooldown = def.fireRate * 3; // Longer delay for reload
    }

    Vec3 eyePos = shooter.position;
    eyePos.y += PLAYER_EYE_HEIGHT;

    for (int pellet = 0; pellet < def.pelletsPerShot; pellet++) {
        // Direction with spread
        float spreadYaw = shooter.yaw + randf(-def.spread, def.spread);
        float spreadPitch = shooter.pitch + randf(-def.spread, def.spread);

        Vec3 dir = {
            sinf(spreadYaw) * cosf(spreadPitch),
            sinf(spreadPitch),
            cosf(spreadYaw) * cosf(spreadPitch)
        };
        dir = dir.normalize();

        // Check player hit
        float playerDist = def.range;
        int hitPlayer = GameMap::raycastPlayers(eyePos, dir, def.range,
                                                g_players, MAX_PLAYERS, shooterId, playerDist);

        // Check wall hit
        Vec3 wallHit;
        float wallDist;
        bool hitWall = g_map.raycast(eyePos, dir, def.range, wallHit, wallDist);

        if (hitPlayer >= 0 && (!hitWall || playerDist < wallDist) &&
            g_players[hitPlayer].teamId != g_players[shooterId].teamId) {
            g_players[hitPlayer].health -= def.damage;
            printf("  HIT! %s -> %s for %d dmg (hp now %d)\n",
                   g_players[shooterId].name, g_players[hitPlayer].name,
                   def.damage, g_players[hitPlayer].health);

            // Send hit notification to all clients
            PlayerHitPacket hitPkt;
            hitPkt.attackerId = shooterId;
            hitPkt.victimId = hitPlayer;
            hitPkt.damage = def.damage;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (g_clients[i].active) {
                    g_socket.sendTo(&hitPkt, sizeof(hitPkt), g_clients[i].addr);
                }
            }

            if (g_players[hitPlayer].health <= 0) {
                g_players[hitPlayer].health = 0;
                g_players[hitPlayer].state = PlayerState::DEAD;
                g_players[hitPlayer].respawnTimer = RESPAWN_TIME;

                // Drop flag if carrying
                for (int t = 0; t < 2; t++) {
                    if (g_flags[t].carrierId == hitPlayer) {
                        g_flags[t].carrierId = -1;
                        g_flags[t].atBase = false;
                        g_flags[t].position = g_players[hitPlayer].position;
                        g_flags[t].returnTimer = 30.0f;
                    }
                }

                PlayerDiedPacket diePkt;
                diePkt.victimId = hitPlayer;
                diePkt.killerId = shooterId;
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (g_clients[i].active) {
                        g_socket.sendTo(&diePkt, sizeof(diePkt), g_clients[i].addr);
                    }
                }
                g_killFeed.push_back({shooterId, hitPlayer, 5.0f});

                printf("%s killed %s\n",
                       g_players[shooterId].name[0] ? g_players[shooterId].name : "Bot",
                       g_players[hitPlayer].name[0] ? g_players[hitPlayer].name : "Bot");
            }
        }
    }
}

// ============================================================================
// Class Abilities
// ============================================================================

static void processAbility(int playerId, const InputState& input) {
    PlayerData& p = g_players[playerId];
    if (p.state != PlayerState::ALIVE) return;
    if (!(input.keys & InputState::KEY_ABILITY)) return;
    if (p.abilityCooldown > 0) return;

    const auto& cdef = getClassDef(p.playerClass);
    p.abilityCooldown = cdef.abilityCooldown;

    switch (cdef.ability) {
        case AbilityType::FRAG_GRENADE: {
            // Throw grenade: instant damage in area
            Vec3 eyePos = p.position;
            eyePos.y += PLAYER_EYE_HEIGHT;
            Vec3 dir = {sinf(p.yaw) * cosf(p.pitch), sinf(p.pitch), cosf(p.yaw) * cosf(p.pitch)};
            dir = dir.normalize();
            Vec3 grenadePos = eyePos + dir * 15.0f; // Grenade lands 15m ahead
            grenadePos.y = 0.5f;

            // Damage all enemies in 6m radius
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (i == playerId) continue;
                if (g_players[i].state != PlayerState::ALIVE) continue;
                if (g_players[i].teamId == p.teamId) continue;
                float d = (g_players[i].position - grenadePos).length();
                if (d < 6.0f) {
                    int dmg = (int)(60.0f * (1.0f - d / 6.0f));
                    vehicleDamage(i, playerId, dmg);
                }
            }
            printf("Player %d threw frag grenade!\n", playerId);
            break;
        }
        case AbilityType::ROCKET_LAUNCHER: {
            // Fire a rocket: hitscan with big damage, mainly for vehicles
            Vec3 eyePos = p.position;
            eyePos.y += PLAYER_EYE_HEIGHT;
            Vec3 dir = {sinf(p.yaw) * cosf(p.pitch), sinf(p.pitch), cosf(p.yaw) * cosf(p.pitch)};
            dir = dir.normalize();

            // Check vehicle hit
            float bestDist = 400.0f;
            int hitVeh = -1;
            for (int v = 0; v < g_numVehicles; v++) {
                if (!g_vehicles[v].active) continue;
                const auto& vdef = getVehicleDef(g_vehicles[v].type);
                AABB vBox = {
                    g_vehicles[v].position - Vec3{vdef.length*0.5f, 0, vdef.width*0.5f},
                    g_vehicles[v].position + Vec3{vdef.length*0.5f, vdef.height, vdef.width*0.5f}
                };
                float t;
                if (vBox.raycast(eyePos, dir, t) && t < bestDist) {
                    bestDist = t;
                    hitVeh = v;
                }
            }
            // Check player hit too
            float pDist = 400.0f;
            int hitP = GameMap::raycastPlayers(eyePos, dir, 400.0f, g_players, MAX_PLAYERS, playerId, pDist);

            if (hitVeh >= 0 && bestDist < pDist) {
                g_vehicles[hitVeh].health -= 150; // Big anti-vehicle damage
                printf("Player %d rocket hit vehicle %d!\n", playerId, hitVeh);
            } else if (hitP >= 0 && g_players[hitP].teamId != p.teamId) {
                vehicleDamage(hitP, playerId, 80);
            }
            break;
        }
        case AbilityType::AMMO_DROP: {
            // Refill ammo for all nearby teammates
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (g_players[i].state != PlayerState::ALIVE) continue;
                if (g_players[i].teamId != p.teamId) continue;
                float d = (g_players[i].position - p.position).length();
                if (d < 10.0f) {
                    g_players[i].ammo = getWeaponDef(g_players[i].currentWeapon).magSize;
                }
            }
            printf("Player %d dropped ammo!\n", playerId);
            break;
        }
        case AbilityType::SPOT_ENEMIES: {
            // Spot all visible enemies within range
            Vec3 eyePos = p.position;
            eyePos.y += PLAYER_EYE_HEIGHT;
            int spotted = 0;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (g_players[i].state != PlayerState::ALIVE) continue;
                if (g_players[i].teamId == p.teamId) continue;
                float d = (g_players[i].position - p.position).length();
                if (d < 80.0f && canSeePlayer(playerId, i)) {
                    g_players[i].spotted = true;
                    g_players[i].spottedTimer = 8.0f;
                    spotted++;
                }
            }
            printf("Player %d spotted %d enemies!\n", playerId, spotted);
            break;
        }
        default: break;
    }
}

// ============================================================================
// Weapon Pickups
// ============================================================================

static void processPickups(float dt) {
    auto& pickups = g_map.weaponPickups();
    for (auto& wp : pickups) {
        if (!wp.active) {
            wp.respawnTimer -= dt;
            if (wp.respawnTimer <= 0) {
                wp.active = true;
                printf("Weapon %s respawned\n", getWeaponDef(wp.type).name);
            }
            continue;
        }

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (g_players[i].state != PlayerState::ALIVE) continue;
            float dist = (g_players[i].position - wp.position).length();
            if (dist < 1.5f) {
                g_players[i].currentWeapon = wp.type;
                g_players[i].ammo = getWeaponDef(wp.type).magSize;
                wp.active = false;
                wp.respawnTimer = WEAPON_RESPAWN;
                printf("Player %d picked up %s\n", i, getWeaponDef(wp.type).name);
                break;
            }
        }
    }
}

// ============================================================================
// CTF (Capture the Flag)
// ============================================================================

static void initFlags() {
    for (int t = 0; t < 2; t++) {
        g_flags[t].basePos = g_map.flagBasePos(t);
        g_flags[t].position = g_flags[t].basePos;
        g_flags[t].carrierId = -1;
        g_flags[t].atBase = true;
        g_flags[t].returnTimer = 0;
    }
}

static void tickCTF(float dt) {
    for (int t = 0; t < 2; t++) {
        auto& flag = g_flags[t];

        // If flag is being carried, update position to carrier
        if (flag.carrierId >= 0) {
            if (g_players[flag.carrierId].state != PlayerState::ALIVE) {
                // Carrier died, drop flag
                flag.position = g_players[flag.carrierId].position;
                flag.carrierId = -1;
                flag.atBase = false;
                flag.returnTimer = 30.0f;
            } else {
                flag.position = g_players[flag.carrierId].position;
                flag.position.y += 2.2f; // Float above carrier's head

                // Check if carrier returned to own base with enemy flag
                int carrierTeam = g_players[flag.carrierId].teamId;
                if (carrierTeam != t) {
                    // Carrying enemy flag, check if own flag is at base
                    auto& ownFlag = g_flags[carrierTeam];
                    float distToBase = (g_players[flag.carrierId].position - ownFlag.basePos).length();
                    if (distToBase < FLAG_CAPTURE_DIST && ownFlag.atBase) {
                        // SCORE!
                        g_teamScores[carrierTeam]++;
                        printf("TEAM %d SCORED! Score: %d-%d\n",
                               carrierTeam, g_teamScores[0], g_teamScores[1]);
                        // Return flag to base
                        flag.position = flag.basePos;
                        flag.carrierId = -1;
                        flag.atBase = true;
                    }
                }
            }
            continue;
        }

        // Flag on ground (not at base): auto-return timer
        if (!flag.atBase) {
            flag.returnTimer -= dt;
            if (flag.returnTimer <= 0) {
                flag.position = flag.basePos;
                flag.atBase = true;
                printf("Flag %d returned to base\n", t);
            }
        }

        // Check if any player can pick up this flag (enemy team)
        for (int p = 0; p < MAX_PLAYERS; p++) {
            if (g_players[p].state != PlayerState::ALIVE) continue;
            float d = (g_players[p].position - flag.position).length();
            if (d < FLAG_CAPTURE_DIST) {
                if (g_players[p].teamId != t) {
                    // Enemy picks up flag
                    flag.carrierId = p;
                    flag.atBase = false;
                    printf("Player %d picked up team %d's flag!\n", p, t);
                    break;
                } else if (!flag.atBase) {
                    // Friendly player returns flag to base
                    flag.position = flag.basePos;
                    flag.atBase = true;
                    printf("Player %d returned team %d's flag!\n", p, t);
                    break;
                }
            }
        }
    }
}

// ============================================================================
// Tornados
// ============================================================================

static void tickTornados(float dt) {
    g_tornadoSpawnTimer -= dt;

    // Spawn new tornado periodically
    if (g_tornadoSpawnTimer <= 0) {
        g_tornadoSpawnTimer = randf(45.0f, 90.0f); // Next tornado in 45-90s
        // Find inactive tornado slot
        for (int i = 0; i < MAX_TORNADOS; i++) {
            if (!g_tornados[i].active) {
                auto& t = g_tornados[i];
                t.active = true;
                t.position = {randf(-120, 120), 0, randf(-120, 120)};
                t.velocity = {randf(-3, 3), 0, randf(-3, 3)};
                t.radius = randf(12, 20);
                t.innerRadius = 3.0f;
                t.strength = randf(25, 40);
                t.damage = randf(3, 8);
                t.lifetime = 0;
                t.maxLifetime = randf(30, 60);
                t.rotation = 0;
                printf("Tornado spawned at %.0f, %.0f\n", t.position.x, t.position.z);
                break;
            }
        }
    }

    for (int i = 0; i < MAX_TORNADOS; i++) {
        auto& t = g_tornados[i];
        if (!t.active) continue;

        t.lifetime += dt;
        t.rotation += dt * 4.0f; // Visual rotation

        // Move tornado
        t.position = t.position + t.velocity * dt;
        // Wander
        t.velocity.x += randf(-1, 1) * dt;
        t.velocity.z += randf(-1, 1) * dt;
        float hSpeed = sqrtf(t.velocity.x * t.velocity.x + t.velocity.z * t.velocity.z);
        if (hSpeed > 5.0f) {
            t.velocity.x = t.velocity.x / hSpeed * 5.0f;
            t.velocity.z = t.velocity.z / hSpeed * 5.0f;
        }
        // Keep in bounds
        t.position.x = std::clamp(t.position.x, -180.0f, 180.0f);
        t.position.z = std::clamp(t.position.z, -180.0f, 180.0f);

        // Affect players
        for (int p = 0; p < MAX_PLAYERS; p++) {
            if (g_players[p].state != PlayerState::ALIVE) continue;
            Vec3 diff = t.position - g_players[p].position;
            diff.y = 0;
            float dist = diff.length();

            if (dist < t.radius && dist > 0.1f) {
                // Pull toward center
                Vec3 pullDir = diff * (1.0f / dist);
                float pullStrength = t.strength * (1.0f - dist / t.radius);

                if (g_players[p].vehicleId < 0) {
                    // On foot: pull and damage
                    g_players[p].velocity = g_players[p].velocity + pullDir * pullStrength * dt;
                    // Add upward force near center
                    if (dist < t.radius * 0.5f) {
                        g_players[p].velocity.y += pullStrength * 0.5f * dt;
                    }
                    // Damage in inner radius
                    if (dist < t.innerRadius) {
                        g_players[p].health -= (int)(t.damage * dt);
                        if (g_players[p].health <= 0) {
                            g_players[p].health = 0;
                            g_players[p].state = PlayerState::DEAD;
                            g_players[p].respawnTimer = RESPAWN_TIME;
                        }
                    }
                }
            }
        }

        // Affect vehicles
        for (int v2 = 0; v2 < g_numVehicles; v2++) {
            auto& veh = g_vehicles[v2];
            if (!veh.active) continue;
            Vec3 diff = t.position - veh.position;
            diff.y = 0;
            float dist = diff.length();
            if (dist < t.radius && dist > 0.1f) {
                Vec3 pullDir = diff * (1.0f / dist);
                float pullStrength = t.strength * 0.3f * (1.0f - dist / t.radius);
                veh.velocity = veh.velocity + pullDir * pullStrength * dt;
                // Damage vehicles in inner radius
                if (dist < t.innerRadius) {
                    veh.health -= (int)(t.damage * 2 * dt);
                }
            }
        }

        // Expire
        if (t.lifetime > t.maxLifetime) {
            t.active = false;
            printf("Tornado expired\n");
        }
    }
}

// ============================================================================
// Vehicles
// ============================================================================

static void spawnVehicles() {
    const auto& spawns = g_map.vehicleSpawns();
    g_numVehicles = std::min((int)spawns.size(), MAX_VEHICLES);
    for (int i = 0; i < g_numVehicles; i++) {
        g_vehicles[i].type = spawns[i].type;
        g_vehicles[i].position = spawns[i].position;
        g_vehicles[i].yaw = spawns[i].yaw;
        g_vehicles[i].spawnPos = spawns[i].position;
        g_vehicles[i].spawnYaw = spawns[i].yaw;
        g_vehicles[i].health = getVehicleDef(spawns[i].type).maxHealth;
        g_vehicles[i].active = true;
        g_vehicles[i].driverId = -1;
        g_vehicles[i].turretYaw = 0;
        g_vehicles[i].velocity = {0,0,0};
        g_vehicles[i].fireCooldown = 0;
        g_vehicles[i].respawnTimer = 0;
    }
}

static void enterVehicle(int playerId) {
    PlayerData& p = g_players[playerId];
    if (p.vehicleId >= 0) return; // Already in vehicle

    float bestDist = VEHICLE_ENTER_RANGE;
    int bestVeh = -1;
    for (int i = 0; i < g_numVehicles; i++) {
        if (!g_vehicles[i].active || g_vehicles[i].driverId >= 0) continue;
        float d = (p.position - g_vehicles[i].position).length();
        if (d < bestDist) {
            bestDist = d;
            bestVeh = i;
        }
    }
    if (bestVeh >= 0) {
        p.vehicleId = bestVeh;
        p.isDriver = true;
        g_vehicles[bestVeh].driverId = playerId;
    }
}

static void exitVehicle(int playerId) {
    PlayerData& p = g_players[playerId];
    if (p.vehicleId < 0) return;
    int vid = p.vehicleId;
    VehicleType vtype = g_vehicles[vid].type;
    g_vehicles[vid].driverId = -1;

    if (vtype == VehicleType::HELICOPTER || vtype == VehicleType::PLANE) {
        // Eject: place player below vehicle, give downward velocity (will fall)
        p.position = g_vehicles[vid].position;
        p.position.y = std::max(0.1f, g_vehicles[vid].position.y - 2.0f);
        p.velocity = {0, -2.0f, 0}; // Gentle fall
    } else {
        // Place player next to ground vehicle
        p.position = g_vehicles[vid].position + Vec3{
            sinf(g_vehicles[vid].yaw + PI * 0.5f) * 3.0f, 0,
            cosf(g_vehicles[vid].yaw + PI * 0.5f) * 3.0f
        };
        p.position.y = 0.1f;
        p.velocity = {0, 0, 0};
    }
    p.vehicleId = -1;
    p.isDriver = false;
}

static void vehicleKill(int victimId, int killerId) {
    g_players[victimId].health = 0;
    g_players[victimId].state = PlayerState::DEAD;
    g_players[victimId].respawnTimer = RESPAWN_TIME;
    if (g_players[victimId].vehicleId >= 0) exitVehicle(victimId);

    // Drop flag if carrying
    for (int t = 0; t < 2; t++) {
        if (g_flags[t].carrierId == victimId) {
            g_flags[t].carrierId = -1;
            g_flags[t].atBase = false;
            g_flags[t].returnTimer = 30.0f;
        }
    }

    PlayerDiedPacket diePkt;
    diePkt.victimId = victimId;
    diePkt.killerId = killerId;
    for (int c = 0; c < MAX_PLAYERS; c++) {
        if (g_clients[c].active)
            g_socket.sendTo(&diePkt, sizeof(diePkt), g_clients[c].addr);
    }
    g_killFeed.push_back({killerId, victimId, 5.0f});
}

static void vehicleDamage(int victimId, int attackerId, int damage) {
    g_players[victimId].health -= damage;
    // Send hit notification
    PlayerHitPacket hitPkt;
    hitPkt.attackerId = attackerId;
    hitPkt.victimId = victimId;
    hitPkt.damage = damage;
    for (int c = 0; c < MAX_PLAYERS; c++) {
        if (g_clients[c].active)
            g_socket.sendTo(&hitPkt, sizeof(hitPkt), g_clients[c].addr);
    }
    if (g_players[victimId].health <= 0) {
        vehicleKill(victimId, attackerId);
    }
}

static void tickVehicles(float dt) {
    for (int i = 0; i < g_numVehicles; i++) {
        auto& v = g_vehicles[i];
        if (!v.active) {
            v.respawnTimer -= dt;
            if (v.respawnTimer <= 0) {
                v.position = v.spawnPos;
                v.yaw = v.spawnYaw;
                v.pitch = 0;
                v.health = getVehicleDef(v.type).maxHealth;
                v.active = true;
                v.driverId = -1;
                v.velocity = {0,0,0};
                v.turretYaw = 0;
                v.rotorAngle = 0;
                v.altitude = 0;
            }
            continue;
        }

        if (v.fireCooldown > 0) v.fireCooldown -= dt;

        // Rotor animation for helicopter
        if (v.type == VehicleType::HELICOPTER) {
            if (v.driverId >= 0) v.rotorAngle += dt * 25.0f;
            else v.rotorAngle += dt * 2.0f; // Slow idle spin
        }
        // Propeller for plane
        if (v.type == VehicleType::PLANE && v.driverId >= 0) {
            v.rotorAngle += dt * 40.0f;
        }

        if (v.driverId >= 0 && v.driverId < MAX_PLAYERS) {
            // Get driver's input
            InputState* input = nullptr;
            if (g_clients[v.driverId].active) {
                input = &g_clients[v.driverId].lastInput;
            } else {
                for (int b = 0; b < g_numBots; b++) {
                    if (g_bots[b].playerId == v.driverId) {
                        input = &g_bots[b].input;
                        break;
                    }
                }
            }

            if (input) {
                const auto& def = getVehicleDef(v.type);

                if (v.type == VehicleType::JEEP || v.type == VehicleType::TANK) {
                    // === GROUND VEHICLE PHYSICS ===
                    if (input->keys & InputState::KEY_A) v.yaw += def.turnRate * dt;
                    if (input->keys & InputState::KEY_D) v.yaw -= def.turnRate * dt;

                    float accel = 0;
                    if (input->keys & InputState::KEY_W) accel = def.speed;
                    if (input->keys & InputState::KEY_S) accel = -def.speed * 0.5f;

                    Vec3 forward = {sinf(v.yaw), 0, cosf(v.yaw)};
                    v.velocity = forward * accel;

                    if (v.type == VehicleType::TANK) {
                        v.turretYaw = input->yaw - v.yaw;
                    }

                    Vec3 newPos = v.position + v.velocity * dt;
                    newPos.y = 0.1f;
                    newPos.x = std::clamp(newPos.x, -190.0f, 190.0f);
                    newPos.z = std::clamp(newPos.z, -190.0f, 190.0f);
                    v.position = newPos;

                } else if (v.type == VehicleType::HELICOPTER) {
                    // === HELICOPTER PHYSICS ===
                    // A/D = yaw turn
                    if (input->keys & InputState::KEY_A) v.yaw += def.turnRate * dt;
                    if (input->keys & InputState::KEY_D) v.yaw -= def.turnRate * dt;

                    // W/S = forward/back
                    float accel = 0;
                    if (input->keys & InputState::KEY_W) accel = def.speed;
                    if (input->keys & InputState::KEY_S) accel = -def.speed * 0.4f;

                    Vec3 forward = {sinf(v.yaw), 0, cosf(v.yaw)};
                    Vec3 hVel = forward * accel;

                    // Space/Ctrl = ascend/descend (use JUMP/KEY_DOWN)
                    float vertSpeed = 0;
                    if (input->keys & InputState::KEY_UP) vertSpeed = 10.0f;
                    if (input->keys & InputState::KEY_DOWN) vertSpeed = -10.0f;

                    v.velocity = {hVel.x, vertSpeed, hVel.z};

                    Vec3 newPos = v.position + v.velocity * dt;
                    newPos.y = std::clamp(newPos.y, 0.5f, 80.0f);
                    newPos.x = std::clamp(newPos.x, -190.0f, 190.0f);
                    newPos.z = std::clamp(newPos.z, -190.0f, 190.0f);
                    v.position = newPos;

                    // Tilt based on movement
                    v.pitch = accel / def.speed * -0.2f;

                } else if (v.type == VehicleType::PLANE) {
                    // === PLANE PHYSICS ===
                    // Always moves forward at high speed
                    float speed = def.speed;
                    if (input->keys & InputState::KEY_W) speed = def.speed * 1.3f;
                    if (input->keys & InputState::KEY_S) speed = def.speed * 0.7f;

                    // A/D = yaw/roll
                    if (input->keys & InputState::KEY_A) v.yaw += def.turnRate * dt;
                    if (input->keys & InputState::KEY_D) v.yaw -= def.turnRate * dt;

                    // JUMP/DOWN = pitch up/down
                    if (input->keys & InputState::KEY_UP) v.pitch += 1.5f * dt;
                    if (input->keys & InputState::KEY_DOWN) v.pitch -= 1.5f * dt;
                    v.pitch = std::clamp(v.pitch, -0.6f, 0.6f);

                    Vec3 forward = {
                        sinf(v.yaw) * cosf(v.pitch),
                        sinf(v.pitch),
                        cosf(v.yaw) * cosf(v.pitch)
                    };
                    v.velocity = forward * speed;

                    Vec3 newPos = v.position + v.velocity * dt;
                    newPos.y = std::clamp(newPos.y, 5.0f, 100.0f); // Planes can't go below 5m
                    newPos.x = std::clamp(newPos.x, -190.0f, 190.0f);
                    newPos.z = std::clamp(newPos.z, -190.0f, 190.0f);

                    // If at boundary, turn around
                    if (fabsf(newPos.x) > 185.0f || fabsf(newPos.z) > 185.0f) {
                        v.yaw += PI * dt; // Force turn
                    }
                    v.position = newPos;
                }

                // Update driver position to follow vehicle
                g_players[v.driverId].position = v.position;
                g_players[v.driverId].position.y = v.position.y + 1.0f;
                g_players[v.driverId].yaw = input->yaw;
                g_players[v.driverId].pitch = input->pitch;

                // === VEHICLE SHOOTING ===
                if ((input->keys & InputState::KEY_SHOOT) && def.cannonDamage > 0 && v.fireCooldown <= 0) {
                    v.fireCooldown = def.cannonRate;

                    float aimYaw, aimPitch;
                    Vec3 origin;

                    if (v.type == VehicleType::TANK) {
                        aimYaw = v.yaw + v.turretYaw;
                        aimPitch = input->pitch;
                        origin = v.position + Vec3{0, 2.5f, 0};
                    } else {
                        // Helicopters/planes aim where driver looks
                        aimYaw = input->yaw;
                        aimPitch = input->pitch;
                        origin = v.position + Vec3{0, 0.5f, 0};
                    }

                    Vec3 cannonDir = {
                        sinf(aimYaw) * cosf(aimPitch),
                        sinf(aimPitch),
                        cosf(aimYaw) * cosf(aimPitch)
                    };
                    cannonDir = cannonDir.normalize();
                    origin = origin + cannonDir * 3.0f;

                    float pDist = 500.0f;
                    int hitP = GameMap::raycastPlayers(origin, cannonDir, 500.0f,
                                                      g_players, MAX_PLAYERS, v.driverId, pDist);
                    Vec3 wallHit;
                    float wallDist;
                    bool hitWall = g_map.raycast(origin, cannonDir, 500.0f, wallHit, wallDist);

                    if (hitP >= 0 && (!hitWall || pDist < wallDist)) {
                        vehicleDamage(hitP, v.driverId, def.cannonDamage);
                        printf("Vehicle cannon hit! %s -> %s for %d dmg\n",
                               g_players[v.driverId].name, g_players[hitP].name, def.cannonDamage);
                    }
                }

                // Run over players (ground vehicles only)
                if (v.type == VehicleType::JEEP || v.type == VehicleType::TANK) {
                    float speed = v.velocity.length();
                    if (speed > 5.0f) {
                        for (int p = 0; p < MAX_PLAYERS; p++) {
                            if (p == v.driverId) continue;
                            if (g_players[p].state != PlayerState::ALIVE) continue;
                            if (g_players[p].vehicleId >= 0) continue;
                            // Don't run over teammates
                            if (g_players[p].teamId == g_players[v.driverId].teamId) continue;
                            float d = (g_players[p].position - v.position).length();
                            if (d < 2.5f) {
                                int dmg = (int)(speed * 3.0f);
                                g_players[p].velocity = v.velocity * 0.5f + Vec3{0, 5, 0};
                                vehicleDamage(p, v.driverId, dmg);
                            }
                        }
                    }
                }
            }
        } else {
            // No driver
            if (v.type == VehicleType::HELICOPTER) {
                // Slowly descend
                v.velocity = {0, -3.0f, 0};
                v.position = v.position + v.velocity * dt;
                if (v.position.y <= 0.5f) {
                    v.position.y = 0.5f;
                    v.velocity = {0,0,0};
                }
            } else if (v.type == VehicleType::PLANE) {
                // Plane with no driver crashes
                v.velocity.y -= 15.0f * dt;
                v.position = v.position + v.velocity * dt;
                if (v.position.y <= 0.1f) {
                    v.health = 0; // Crash
                }
            } else {
                v.velocity = v.velocity * 0.95f;
                if (v.velocity.lengthSq() < 0.01f) v.velocity = {0,0,0};
            }
        }

        // Vehicle destruction
        if (v.health <= 0) {
            v.active = false;
            v.respawnTimer = 30.0f;
            if (v.driverId >= 0) {
                g_players[v.driverId].health = 0;
                g_players[v.driverId].state = PlayerState::DEAD;
                g_players[v.driverId].respawnTimer = RESPAWN_TIME;
                exitVehicle(v.driverId);
            }
        }
    }
}

// ============================================================================
// AI Bot Logic
// ============================================================================

static bool canSeePlayer(int botId, int targetId) {
    Vec3 from = g_players[botId].position;
    from.y += PLAYER_EYE_HEIGHT;
    Vec3 to = g_players[targetId].position;
    to.y += PLAYER_HEIGHT * 0.5f;

    Vec3 dir = to - from;
    float dist = dir.length();
    if (dist < 0.1f) return true;
    dir = dir * (1.0f / dist);

    Vec3 hitPt;
    float hitDist;
    if (g_map.raycast(from, dir, dist, hitPt, hitDist)) {
        return hitDist > dist - 0.5f; // Wall is behind target
    }
    return true; // No wall in the way
}

static int findNearestVisibleEnemy(int botId, float maxRange) {
    int best = -1;
    float bestDist = maxRange;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (i == botId) continue;
        if (g_players[i].state != PlayerState::ALIVE) continue;
        // Don't target teammates
        if (g_players[i].teamId == g_players[botId].teamId) continue;
        float d = (g_players[i].position - g_players[botId].position).length();
        if (d < bestDist && canSeePlayer(botId, i)) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

// Helper: move bot along a path, with jump detection
static void botFollowPath(BotData& bot, PlayerData& p, float dt) {
    const auto& waypoints = g_map.waypoints();

    // Advance through path
    if (bot.pathIndex < (int)bot.path.size()) {
        int wpIdx = bot.path[bot.pathIndex];
        Vec3 wp = waypoints[wpIdx].position;
        Vec3 toWP = wp - p.position;
        float distXZ = sqrtf(toWP.x * toWP.x + toWP.z * toWP.z);
        float distY = wp.y - p.position.y;

        if (distXZ < 2.0f && fabsf(distY) < 2.0f) {
            bot.pathIndex++;
            if (bot.pathIndex >= (int)bot.path.size()) {
                bot.path.clear();
                bot.pathIndex = 0;
            }
            return;
        }

        // Face waypoint
        float targetYaw = atan2f(toWP.x, toWP.z);
        p.yaw = targetYaw;
        bot.input.yaw = p.yaw;
        bot.input.pitch = 0;
        bot.input.keys |= InputState::KEY_W;

        // Jump if waypoint is above us (stairs, crates, towers)
        if (distY > 0.5f && bot.jumpCooldown <= 0) {
            bot.input.keys |= InputState::KEY_JUMP;
            bot.jumpCooldown = 0.4f;
        }

        // Jump over obstacles detected ahead
        float obstacleH = 0;
        if (g_map.hasObstacleAhead(p.position, p.yaw, 1.5f, obstacleH)) {
            if (obstacleH < 2.0f && bot.jumpCooldown <= 0) {
                bot.input.keys |= InputState::KEY_JUMP;
                bot.jumpCooldown = 0.4f;
            }
        }
    }
}

// Compute A* path from bot's current position to a target position
static void botPathfindTo(BotData& bot, const Vec3& target) {
    int startWP = findNearestWaypointToPos(g_map, g_players[bot.playerId].position);
    int goalWP = findNearestWaypointToPos(g_map, target);
    bot.path = findPath(g_map, startWP, goalWP);
    bot.pathIndex = 0;
    bot.pathAge = 0;
}

static void updateBotAI(BotData& bot, float dt) {
    int id = bot.playerId;
    PlayerData& p = g_players[id];

    if (p.state == PlayerState::DEAD) {
        p.respawnTimer -= dt;
        if (p.respawnTimer <= 0) {
            spawnPlayer(id);
            bot.aiState = AIState::PATROL;
            bot.path.clear();
            bot.pathIndex = 0;
        }
        return;
    }
    if (p.state != PlayerState::ALIVE) return;

    const auto& waypoints = g_map.waypoints();
    bot.stateTimer -= dt;
    bot.pathAge += dt;
    if (bot.jumpCooldown > 0) bot.jumpCooldown -= dt;
    if (bot.combatJumpTimer > 0) bot.combatJumpTimer -= dt;
    if (bot.strafeTimer > 0) bot.strafeTimer -= dt;

    // Stuck detection
    float moved = (p.position - bot.lastPos).length();
    if (moved < 0.05f * dt) {
        bot.stuckTimer += dt;
    } else {
        bot.stuckTimer = 0;
    }
    bot.lastPos = p.position;

    // Unstick: try jumping first, then repath
    if (bot.stuckTimer > 0.5f && bot.jumpCooldown <= 0) {
        bot.input.keys |= InputState::KEY_JUMP;
        bot.jumpCooldown = 0.4f;
    }
    if (bot.stuckTimer > 1.5f) {
        // Repath to a random waypoint
        int randWP = rand() % waypoints.size();
        bot.path = findPath(g_map, g_map.findNearestWaypoint(p.position), randWP);
        bot.pathIndex = 0;
        bot.stuckTimer = 0;
    }

    // Build input
    bot.input = InputState{};

    switch (bot.aiState) {
        case AIState::PATROL: {
            if (waypoints.empty()) break;

            // Generate path if we don't have one
            if (bot.path.empty() || bot.pathAge > 8.0f) {
                // Pick a random distant waypoint
                int curWP = g_map.findNearestWaypoint(p.position);
                int targetWP = rand() % waypoints.size();
                // Prefer waypoints that are far away for interesting patrol routes
                for (int attempt = 0; attempt < 3; attempt++) {
                    int candidate = rand() % waypoints.size();
                    if ((waypoints[candidate].position - p.position).lengthSq() >
                        (waypoints[targetWP].position - p.position).lengthSq()) {
                        targetWP = candidate;
                    }
                }
                bot.path = findPath(g_map, curWP, targetWP);
                bot.pathIndex = 0;
                bot.pathAge = 0;
            }

            // Follow path
            botFollowPath(bot, p, dt);

            // Check for enemies
            int enemy = findNearestVisibleEnemy(id, 40.0f);
            if (enemy >= 0) {
                bot.targetPlayerId = enemy;
                bot.aiState = AIState::CHASE;
                bot.reactionTimer = bot.reactionDelay;
                bot.stateTimer = 10.0f;
                bot.path.clear();
            }

            // Check for weapon pickups if only have pistol
            if (p.currentWeapon == WeaponType::PISTOL) {
                float bestPickupDist = 30.0f;
                Vec3 bestPickupPos;
                bool foundPickup = false;
                for (const auto& wp2 : g_map.weaponPickups()) {
                    if (!wp2.active) continue;
                    float d = (p.position - wp2.position).length();
                    if (d < bestPickupDist) {
                        bestPickupDist = d;
                        bestPickupPos = wp2.position;
                        foundPickup = true;
                    }
                }
                if (foundPickup) {
                    botPathfindTo(bot, bestPickupPos);
                    bot.targetPos = bestPickupPos;
                    bot.aiState = AIState::PICKUP_WEAPON;
                    bot.stateTimer = 12.0f;
                }
            }
            break;
        }

        case AIState::CHASE: {
            int tid = bot.targetPlayerId;
            if (tid < 0 || tid >= MAX_PLAYERS || g_players[tid].state != PlayerState::ALIVE) {
                bot.aiState = AIState::PATROL;
                bot.path.clear();
                break;
            }

            Vec3 toEnemy = g_players[tid].position - p.position;
            float dist = toEnemy.length();

            // Repath to enemy periodically
            if (bot.path.empty() || bot.pathAge > 2.0f) {
                botPathfindTo(bot, g_players[tid].position);
            }

            // Face enemy
            float targetYaw = atan2f(toEnemy.x, toEnemy.z);
            p.yaw = targetYaw;

            // Aim at enemy with jitter
            float hDist = sqrtf(toEnemy.x * toEnemy.x + toEnemy.z * toEnemy.z);
            float targetPitch = atan2f(toEnemy.y + PLAYER_HEIGHT * 0.5f - PLAYER_EYE_HEIGHT, hDist);
            p.pitch = targetPitch + randf(-bot.aimJitter, bot.aimJitter);

            bot.input.yaw = p.yaw + randf(-bot.aimJitter, bot.aimJitter);
            bot.input.pitch = p.pitch;

            if (dist < getWeaponDef(p.currentWeapon).range * 0.8f && canSeePlayer(id, tid)) {
                bot.aiState = AIState::ATTACK;
                bot.stateTimer = 5.0f;
                bot.strafeTimer = 0;
                bot.strafeDir = randf() < 0.5f ? 1.0f : -1.0f;
            } else {
                // Follow path toward enemy
                botFollowPath(bot, p, dt);
                // Override yaw to face movement direction while chasing
                if (!bot.path.empty() && bot.pathIndex < (int)bot.path.size()) {
                    Vec3 wpPos = waypoints[bot.path[bot.pathIndex]].position;
                    Vec3 toWP = wpPos - p.position;
                    p.yaw = atan2f(toWP.x, toWP.z);
                    bot.input.yaw = p.yaw;
                }
            }

            // Jump while chasing to be unpredictable
            if (bot.combatJumpTimer <= 0 && randf() < 0.01f) {
                bot.input.keys |= InputState::KEY_JUMP;
                bot.combatJumpTimer = randf(1.0f, 3.0f);
            }

            if (bot.stateTimer <= 0 || (!canSeePlayer(id, tid) && dist > 20.0f)) {
                bot.aiState = AIState::PATROL;
                bot.path.clear();
            }
            break;
        }

        case AIState::ATTACK: {
            int tid = bot.targetPlayerId;
            if (tid < 0 || tid >= MAX_PLAYERS || g_players[tid].state != PlayerState::ALIVE) {
                bot.aiState = AIState::PATROL;
                bot.path.clear();
                break;
            }

            Vec3 toEnemy = g_players[tid].position - p.position;
            float dist = toEnemy.length();

            // Face and aim at enemy
            float targetYaw = atan2f(toEnemy.x, toEnemy.z);
            p.yaw = targetYaw + randf(-bot.aimJitter, bot.aimJitter);
            float hDist = sqrtf(toEnemy.x * toEnemy.x + toEnemy.z * toEnemy.z);
            float targetPitch = atan2f(toEnemy.y + PLAYER_HEIGHT * 0.5f - PLAYER_EYE_HEIGHT, hDist);
            p.pitch = targetPitch + randf(-bot.aimJitter, bot.aimJitter);

            bot.input.yaw = p.yaw;
            bot.input.pitch = p.pitch;

            // Advanced strafing: change direction every 1-3 seconds
            if (bot.strafeTimer <= 0) {
                bot.strafeDir = -bot.strafeDir;
                bot.strafeTimer = randf(0.8f, 2.5f);
                // Sometimes add forward/backward movement
                if (randf() < 0.3f) {
                    bot.input.keys |= (dist > 10.0f) ? InputState::KEY_W : InputState::KEY_S;
                }
            }
            if (bot.strafeDir > 0) {
                bot.input.keys |= InputState::KEY_D;
            } else {
                bot.input.keys |= InputState::KEY_A;
            }

            // Combat jumping - jump to dodge
            if (bot.combatJumpTimer <= 0 && randf() < 0.03f) {
                bot.input.keys |= InputState::KEY_JUMP;
                bot.combatJumpTimer = randf(0.8f, 2.0f);
            }

            // Obstacle jump while strafing
            float obstH = 0;
            if (g_map.hasObstacleAhead(p.position, p.yaw + (bot.strafeDir > 0 ? PI * 0.5f : -PI * 0.5f), 1.0f, obstH)) {
                if (obstH < 2.0f && bot.jumpCooldown <= 0) {
                    bot.input.keys |= InputState::KEY_JUMP;
                    bot.jumpCooldown = 0.4f;
                }
            }

            // Shoot (after reaction delay, with miss chance)
            bot.reactionTimer -= dt;
            if (bot.reactionTimer <= 0 && canSeePlayer(id, tid)) {
                if (randf() < 0.6f) { // 60% chance to actually pull trigger each tick
                    bot.input.keys |= InputState::KEY_SHOOT;
                }
            }

            // Retreat if low health
            if (p.health < 30) {
                bot.aiState = AIState::RETREAT;
                bot.stateTimer = 5.0f;
                bot.path.clear();
                // Path away from enemy
                Vec3 fleeTarget = p.position + (p.position - g_players[tid].position).normalize() * 20.0f;
                botPathfindTo(bot, fleeTarget);
                break;
            }

            // If enemy out of range or dead
            if (dist > getWeaponDef(p.currentWeapon).range || bot.stateTimer <= 0) {
                bot.aiState = AIState::CHASE;
                bot.stateTimer = 10.0f;
                bot.path.clear();
            }

            if (!canSeePlayer(id, tid)) {
                bot.aiState = AIState::CHASE;
                bot.stateTimer = 5.0f;
                // Path to enemy's last known position
                botPathfindTo(bot, g_players[tid].position);
            }
            break;
        }

        case AIState::RETREAT: {
            int tid = bot.targetPlayerId;

            // Follow retreat path
            if (!bot.path.empty()) {
                botFollowPath(bot, p, dt);
            } else if (tid >= 0 && tid < MAX_PLAYERS && g_players[tid].state == PlayerState::ALIVE) {
                // Generate retreat path away from enemy
                Vec3 away = p.position - g_players[tid].position;
                away.y = 0;
                if (away.lengthSq() > 0.1f) {
                    Vec3 fleeTarget = p.position + away.normalize() * 25.0f;
                    botPathfindTo(bot, fleeTarget);
                }
            }

            // Jump while retreating for evasion
            if (bot.combatJumpTimer <= 0 && randf() < 0.04f) {
                bot.input.keys |= InputState::KEY_JUMP;
                bot.combatJumpTimer = randf(0.5f, 1.5f);
            }

            // Shoot back while retreating if enemy visible
            if (tid >= 0 && tid < MAX_PLAYERS && g_players[tid].state == PlayerState::ALIVE) {
                Vec3 toEnemy = g_players[tid].position - p.position;
                if (canSeePlayer(id, tid)) {
                    // Aim and shoot while running (very inaccurate)
                    float aimYaw = atan2f(toEnemy.x, toEnemy.z);
                    bot.input.yaw = aimYaw + randf(-bot.aimJitter * 3, bot.aimJitter * 3);
                    float hDist = sqrtf(toEnemy.x * toEnemy.x + toEnemy.z * toEnemy.z);
                    bot.input.pitch = atan2f(toEnemy.y + PLAYER_HEIGHT * 0.5f - PLAYER_EYE_HEIGHT, hDist);
                    if (randf() < 0.25f) { // Rarely shoot while retreating
                        bot.input.keys |= InputState::KEY_SHOOT;
                    }
                }
            }

            if (bot.stateTimer <= 0 || p.health > 60) {
                bot.aiState = AIState::PATROL;
                bot.path.clear();
            }
            break;
        }

        case AIState::PICKUP_WEAPON: {
            // Follow path to weapon pickup
            if (!bot.path.empty()) {
                botFollowPath(bot, p, dt);
            } else {
                // Direct movement as fallback
                Vec3 toTarget = bot.targetPos - p.position;
                toTarget.y = 0;
                if (toTarget.lengthSq() > 0.1f) {
                    float targetYaw = atan2f(toTarget.x, toTarget.z);
                    p.yaw = targetYaw;
                    bot.input.yaw = p.yaw;
                    bot.input.pitch = 0;
                    bot.input.keys |= InputState::KEY_W;
                }
            }

            Vec3 toTarget = bot.targetPos - p.position;
            float dist = toTarget.length();

            if (dist < 1.5f || bot.stateTimer <= 0 || p.currentWeapon != WeaponType::PISTOL) {
                bot.aiState = AIState::PATROL;
                bot.path.clear();
                break;
            }

            // If see enemy while going for weapon, fight instead
            int enemy = findNearestVisibleEnemy(id, 20.0f);
            if (enemy >= 0) {
                bot.targetPlayerId = enemy;
                bot.aiState = AIState::ATTACK;
                bot.reactionTimer = bot.reactionDelay;
                bot.stateTimer = 5.0f;
                bot.path.clear();
            }
            break;
        }
    }
}

static void spawnBots(int count) {
    for (int i = 0; i < count; i++) {
        int slot = findFreeSlot();
        if (slot < 0) break;

        g_players[slot] = PlayerData{};
        g_players[slot].isBot = true;
        // Assign team (alternating)
        g_players[slot].teamId = g_nextTeam;
        g_nextTeam = (g_nextTeam + 1) % 2;
        // Random class
        g_players[slot].playerClass = (PlayerClass)(rand() % (int)PlayerClass::COUNT);
        const auto& cdef = getClassDef(g_players[slot].playerClass);
        snprintf(g_players[slot].name, sizeof(g_players[slot].name), "Bot_%d", i + 1);
        g_players[slot].currentWeapon = cdef.primaryWeapon;
        g_players[slot].ammo = getWeaponDef(cdef.primaryWeapon).magSize;
        spawnPlayer(slot);

        g_bots[i].playerId = slot;
        g_bots[i].aiState = AIState::PATROL;
        g_bots[i].currentWaypoint = rand() % g_map.waypoints().size();
        g_bots[i].targetPos = g_map.waypoints()[g_bots[i].currentWaypoint].position;
        g_bots[i].reactionDelay = randf(0.6f, 1.5f);
        g_bots[i].aimJitter = randf(0.06f, 0.14f);
        g_bots[i].lastPos = g_players[slot].position;

        printf("Spawned bot '%s' at slot %d\n", g_players[slot].name, slot);
    }
    g_numBots = count;
}

// ============================================================================
// Main Server Loop
// ============================================================================

int main(int argc, char** argv) {
    srand((unsigned)time(nullptr));

    int port = DEFAULT_PORT;
    int botCount = 100;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-bots") == 0 && i + 1 < argc) {
            botCount = atoi(argv[++i]);
            if (botCount > MAX_PLAYERS - 4) botCount = MAX_PLAYERS - 4;
        }
    }

    printf("=== ARCTIC ASSAULT SERVER ===\n");
    printf("Port: %d, Bots: %d\n", port, botCount);

    g_map.buildArcticMap();
    printf("Map built: %zu blocks, %zu spawns, %zu pickups, %zu waypoints\n",
           g_map.blocks().size(), g_map.spawns().size(),
           g_map.weaponPickups().size(), g_map.waypoints().size());

    if (!g_socket.bind(port)) {
        fprintf(stderr, "Failed to bind to port %d\n", port);
        return 1;
    }
    g_socket.setNonBlocking(true);
    printf("Listening on port %d\n", port);

    // Initialize all players as disconnected
    for (int i = 0; i < MAX_PLAYERS; i++) {
        g_players[i].state = PlayerState::DISCONNECTED;
        g_clients[i].active = false;
    }

    spawnBots(botCount);
    spawnVehicles();
    initFlags();
    printf("Vehicles spawned: %d\n", g_numVehicles);
    printf("CTF flags initialized\n");

    printf("Server running. Press Ctrl+C to stop.\n\n");

    auto lastTime = std::chrono::high_resolution_clock::now();

    while (g_running) {
        auto now = std::chrono::high_resolution_clock::now();
        lastTime = now;

        // --- Receive packets ---
        uint8_t buf[8192];
        sockaddr_in fromAddr;
        int len;
        while ((len = g_socket.recvFrom(buf, sizeof(buf), fromAddr)) > 0) {
            if (len < 1) continue;
            uint8_t type = buf[0];

            switch ((ClientPacket)type) {
                case ClientPacket::JOIN:
                    if (len >= (int)sizeof(JoinPacket)) {
                        JoinPacket pkt;
                        memcpy(&pkt, buf, sizeof(pkt));
                        handleJoin(pkt, fromAddr);
                    }
                    break;
                case ClientPacket::INPUT:
                    if (len >= (int)sizeof(InputPacket)) {
                        InputPacket pkt;
                        memcpy(&pkt, buf, sizeof(pkt));
                        handleInput(pkt, fromAddr);
                    }
                    break;
                case ClientPacket::DISCONNECT:
                    handleDisconnect(fromAddr);
                    break;
            }
        }

        // --- Update AI bots ---
        for (int i = 0; i < g_numBots; i++) {
            updateBotAI(g_bots[i], TICK_DURATION);
        }

        // --- Tick all players ---
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (g_players[i].state == PlayerState::DEAD) {
                g_players[i].respawnTimer -= TICK_DURATION;
                if (g_players[i].respawnTimer <= 0) {
                    spawnPlayer(i);
                }
                continue;
            }
            if (g_players[i].state != PlayerState::ALIVE) continue;

            InputState* input = nullptr;
            // Find input source
            if (g_clients[i].active) {
                input = &g_clients[i].lastInput;
            } else {
                // Check if this is a bot
                for (int b = 0; b < g_numBots; b++) {
                    if (g_bots[b].playerId == i) {
                        input = &g_bots[b].input;
                        break;
                    }
                }
            }

            if (input) {
                // Vehicle enter/exit
                if (input->keys & InputState::KEY_USE) {
                    if (g_players[i].vehicleId >= 0) {
                        exitVehicle(i);
                    } else {
                        enterVehicle(i);
                    }
                    input->keys &= ~InputState::KEY_USE;
                }

                // Ability cooldown
                if (g_players[i].abilityCooldown > 0)
                    g_players[i].abilityCooldown -= TICK_DURATION;

                // Process class ability (Q key)
                if (input->keys & InputState::KEY_ABILITY) {
                    processAbility(i, *input);
                    input->keys &= ~InputState::KEY_ABILITY;
                }

                // Only tick player movement if NOT in vehicle
                if (g_players[i].vehicleId < 0) {
                    tickPlayer(g_players[i], *input, g_map, TICK_DURATION);

                    // Process shooting (on foot)
                    if (input->keys & InputState::KEY_SHOOT) {
                        processShot(i);
                    }
                }
                // In vehicle: shooting is handled by tickVehicles
            }

            // Spotted timer
            if (g_players[i].spotted) {
                g_players[i].spottedTimer -= TICK_DURATION;
                if (g_players[i].spottedTimer <= 0) {
                    g_players[i].spotted = false;
                }
            }
        }

        // --- Tick vehicles ---
        tickVehicles(TICK_DURATION);

        // --- Process weapon pickups ---
        processPickups(TICK_DURATION);

        // --- CTF logic ---
        tickCTF(TICK_DURATION);

        // --- Tornado logic ---
        tickTornados(TICK_DURATION);

        // --- Update kill feed timers ---
        for (auto it = g_killFeed.begin(); it != g_killFeed.end();) {
            it->timer -= TICK_DURATION;
            if (it->timer <= 0) it = g_killFeed.erase(it);
            else ++it;
        }

        // --- Client timeouts ---
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (g_clients[i].active) {
                g_clients[i].timeoutTimer += TICK_DURATION;
                if (g_clients[i].timeoutTimer > 10.0f) {
                    printf("Player '%s' timed out\n", g_players[i].name);
                    g_players[i].state = PlayerState::DISCONNECTED;
                    g_clients[i].active = false;
                }
            }
        }

        // --- Broadcast snapshot ---
        broadcastSnapshot();

        g_serverTick++;

        // Sleep to maintain tick rate
        auto tickEnd = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(tickEnd - now).count();
        float sleepTime = TICK_DURATION - elapsed;
        if (sleepTime > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds((int)(sleepTime * 1000000)));
        }
    }

    g_socket.close();
    printf("Server stopped.\n");
    return 0;
}
