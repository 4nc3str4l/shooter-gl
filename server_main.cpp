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
};

static GameMap          g_map;
static PlayerData       g_players[MAX_PLAYERS];
static ClientConnection g_clients[MAX_PLAYERS];
static BotData          g_bots[MAX_PLAYERS];
static int              g_numBots = 0;
static uint32_t         g_serverTick = 0;
static UDPSocket        g_socket;
static bool             g_running = true;

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

static int findFreeSlot() {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g_players[i].state == PlayerState::DISCONNECTED) return i;
    }
    return -1;
}

static void spawnPlayer(int id) {
    const auto& spawns = g_map.spawns();
    int si = rand() % spawns.size();
    g_players[id].position = spawns[si].position;
    g_players[id].yaw = spawns[si].yaw;
    g_players[id].pitch = 0;
    g_players[id].velocity = {0, 0, 0};
    g_players[id].health = MAX_HEALTH;
    g_players[id].state = PlayerState::ALIVE;
    g_players[id].fireCooldown = 0;
    g_players[id].respawnTimer = 0;

    if (g_players[id].currentWeapon == WeaponType::NONE) {
        g_players[id].currentWeapon = WeaponType::PISTOL;
        g_players[id].ammo = getWeaponDef(WeaponType::PISTOL).magSize;
    }
}

// ============================================================================
// Networking
// ============================================================================

static void sendSnapshot(const sockaddr_in& addr, int clientPlayerId) {
    uint8_t buf[2048];
    int offset = 0;

    SnapshotPacket hdr;
    hdr.serverTick = g_serverTick;
    if (clientPlayerId >= 0 && clientPlayerId < MAX_PLAYERS) {
        hdr.ackInputSeq = g_clients[clientPlayerId].lastInputSeq;
    }

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

    printf("Player '%s' joined as ID %d\n", g_players[slot].name, slot);
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

        if (hitPlayer >= 0 && (!hitWall || playerDist < wallDist)) {
            g_players[hitPlayer].health -= def.damage;

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
        float d = (g_players[i].position - g_players[botId].position).length();
        if (d < bestDist && canSeePlayer(botId, i)) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

static void updateBotAI(BotData& bot, float dt) {
    int id = bot.playerId;
    PlayerData& p = g_players[id];

    if (p.state == PlayerState::DEAD) {
        p.respawnTimer -= dt;
        if (p.respawnTimer <= 0) {
            spawnPlayer(id);
            bot.aiState = AIState::PATROL;
        }
        return;
    }
    if (p.state != PlayerState::ALIVE) return;

    const auto& waypoints = g_map.waypoints();
    bot.stateTimer -= dt;

    // Stuck detection
    float moved = (p.position - bot.lastPos).length();
    if (moved < 0.05f * dt) {
        bot.stuckTimer += dt;
    } else {
        bot.stuckTimer = 0;
    }
    bot.lastPos = p.position;

    // Unstick: pick random waypoint
    if (bot.stuckTimer > 1.0f) {
        bot.currentWaypoint = rand() % waypoints.size();
        bot.targetPos = waypoints[bot.currentWaypoint].position;
        bot.stuckTimer = 0;
    }

    // Build input
    bot.input = InputState{};

    switch (bot.aiState) {
        case AIState::PATROL: {
            if (waypoints.empty()) break;

            Vec3 wp = waypoints[bot.currentWaypoint].position;
            float distToWP = (p.position - wp).length();

            if (distToWP < 2.0f) {
                // Move to next waypoint (random neighbor)
                const auto& neighbors = waypoints[bot.currentWaypoint].neighbors;
                if (!neighbors.empty()) {
                    bot.currentWaypoint = neighbors[rand() % neighbors.size()];
                }
                bot.targetPos = waypoints[bot.currentWaypoint].position;
            }

            // Move toward waypoint
            Vec3 toTarget = bot.targetPos - p.position;
            toTarget.y = 0;
            if (toTarget.lengthSq() > 0.1f) {
                float targetYaw = atan2f(toTarget.x, toTarget.z);
                p.yaw = targetYaw;
                bot.input.yaw = p.yaw;
                bot.input.pitch = 0;
                bot.input.keys |= InputState::KEY_W;
            }

            // Check for enemies
            int enemy = findNearestVisibleEnemy(id, 40.0f);
            if (enemy >= 0) {
                bot.targetPlayerId = enemy;
                bot.aiState = AIState::CHASE;
                bot.reactionTimer = bot.reactionDelay;
                bot.stateTimer = 10.0f;
            }

            // Check for weapon pickups if only have pistol
            if (p.currentWeapon == WeaponType::PISTOL) {
                for (const auto& wp2 : g_map.weaponPickups()) {
                    if (!wp2.active) continue;
                    float d = (p.position - wp2.position).length();
                    if (d < 25.0f) {
                        bot.targetPos = wp2.position;
                        bot.aiState = AIState::PICKUP_WEAPON;
                        bot.stateTimer = 8.0f;
                        break;
                    }
                }
            }
            break;
        }

        case AIState::CHASE: {
            int tid = bot.targetPlayerId;
            if (tid < 0 || tid >= MAX_PLAYERS || g_players[tid].state != PlayerState::ALIVE) {
                bot.aiState = AIState::PATROL;
                break;
            }

            Vec3 toEnemy = g_players[tid].position - p.position;
            float dist = toEnemy.length();

            // Face enemy
            float targetYaw = atan2f(toEnemy.x, toEnemy.z);
            p.yaw = targetYaw;

            // Aim at enemy with jitter
            float targetPitch = atan2f(toEnemy.y + PLAYER_HEIGHT * 0.5f - PLAYER_EYE_HEIGHT,
                                       sqrtf(toEnemy.x * toEnemy.x + toEnemy.z * toEnemy.z));
            p.pitch = targetPitch + randf(-bot.aimJitter, bot.aimJitter);

            bot.input.yaw = p.yaw + randf(-bot.aimJitter, bot.aimJitter);
            bot.input.pitch = p.pitch;

            if (dist < getWeaponDef(p.currentWeapon).range * 0.8f && canSeePlayer(id, tid)) {
                bot.aiState = AIState::ATTACK;
                bot.stateTimer = 5.0f;
            } else {
                // Move toward enemy
                bot.input.keys |= InputState::KEY_W;
            }

            if (bot.stateTimer <= 0 || !canSeePlayer(id, tid)) {
                bot.aiState = AIState::PATROL;
            }
            break;
        }

        case AIState::ATTACK: {
            int tid = bot.targetPlayerId;
            if (tid < 0 || tid >= MAX_PLAYERS || g_players[tid].state != PlayerState::ALIVE) {
                bot.aiState = AIState::PATROL;
                break;
            }

            Vec3 toEnemy = g_players[tid].position - p.position;
            float dist = toEnemy.length();

            // Face and aim at enemy
            float targetYaw = atan2f(toEnemy.x, toEnemy.z);
            p.yaw = targetYaw + randf(-bot.aimJitter, bot.aimJitter);
            float targetPitch = atan2f(toEnemy.y + PLAYER_HEIGHT * 0.5f - PLAYER_EYE_HEIGHT,
                                       sqrtf(toEnemy.x * toEnemy.x + toEnemy.z * toEnemy.z));
            p.pitch = targetPitch + randf(-bot.aimJitter, bot.aimJitter);

            bot.input.yaw = p.yaw;
            bot.input.pitch = p.pitch;

            // Strafe randomly
            if (randf() < 0.02f) {
                // Change strafe direction
            }
            if (g_serverTick % 120 < 60) {
                bot.input.keys |= InputState::KEY_A;
            } else {
                bot.input.keys |= InputState::KEY_D;
            }

            // Shoot (after reaction delay)
            bot.reactionTimer -= dt;
            if (bot.reactionTimer <= 0 && canSeePlayer(id, tid)) {
                bot.input.keys |= InputState::KEY_SHOOT;
            }

            // Retreat if low health
            if (p.health < 30) {
                bot.aiState = AIState::RETREAT;
                bot.stateTimer = 5.0f;
                break;
            }

            // If enemy out of range or dead
            if (dist > getWeaponDef(p.currentWeapon).range || bot.stateTimer <= 0) {
                bot.aiState = AIState::CHASE;
                bot.stateTimer = 10.0f;
            }

            if (!canSeePlayer(id, tid)) {
                bot.aiState = AIState::CHASE;
                bot.stateTimer = 5.0f;
            }
            break;
        }

        case AIState::RETREAT: {
            int tid = bot.targetPlayerId;
            if (tid >= 0 && tid < MAX_PLAYERS && g_players[tid].state == PlayerState::ALIVE) {
                // Move away from enemy
                Vec3 away = p.position - g_players[tid].position;
                away.y = 0;
                if (away.lengthSq() > 0.1f) {
                    float targetYaw = atan2f(away.x, away.z);
                    p.yaw = targetYaw;
                    bot.input.yaw = p.yaw;
                    bot.input.pitch = 0;
                    bot.input.keys |= InputState::KEY_W;
                }
            }

            if (bot.stateTimer <= 0 || p.health > 60) {
                bot.aiState = AIState::PATROL;
            }
            break;
        }

        case AIState::PICKUP_WEAPON: {
            Vec3 toTarget = bot.targetPos - p.position;
            toTarget.y = 0;
            float dist = toTarget.length();

            if (dist < 1.5f || bot.stateTimer <= 0 || p.currentWeapon != WeaponType::PISTOL) {
                bot.aiState = AIState::PATROL;
                break;
            }

            float targetYaw = atan2f(toTarget.x, toTarget.z);
            p.yaw = targetYaw;
            bot.input.yaw = p.yaw;
            bot.input.pitch = 0;
            bot.input.keys |= InputState::KEY_W;

            // If see enemy while going for weapon, fight instead
            int enemy = findNearestVisibleEnemy(id, 20.0f);
            if (enemy >= 0) {
                bot.targetPlayerId = enemy;
                bot.aiState = AIState::ATTACK;
                bot.reactionTimer = bot.reactionDelay;
                bot.stateTimer = 5.0f;
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
        snprintf(g_players[slot].name, sizeof(g_players[slot].name), "Bot_%d", i + 1);
        g_players[slot].currentWeapon = WeaponType::PISTOL;
        g_players[slot].ammo = getWeaponDef(WeaponType::PISTOL).magSize;
        spawnPlayer(slot);

        g_bots[i].playerId = slot;
        g_bots[i].aiState = AIState::PATROL;
        g_bots[i].currentWaypoint = rand() % g_map.waypoints().size();
        g_bots[i].targetPos = g_map.waypoints()[g_bots[i].currentWaypoint].position;
        g_bots[i].reactionDelay = randf(0.3f, 0.8f);
        g_bots[i].aimJitter = randf(0.02f, 0.06f);
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
    int botCount = 4;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-bots") == 0 && i + 1 < argc) {
            botCount = atoi(argv[++i]);
            if (botCount > MAX_PLAYERS - 2) botCount = MAX_PLAYERS - 2;
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

    printf("Server running. Press Ctrl+C to stop.\n\n");

    auto lastTime = std::chrono::high_resolution_clock::now();

    while (g_running) {
        auto now = std::chrono::high_resolution_clock::now();
        lastTime = now;

        // --- Receive packets ---
        uint8_t buf[2048];
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
                tickPlayer(g_players[i], *input, g_map, TICK_DURATION);

                // Process shooting
                if (input->keys & InputState::KEY_SHOOT) {
                    processShot(i);
                }
            }
        }

        // --- Process weapon pickups ---
        processPickups(TICK_DURATION);

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
