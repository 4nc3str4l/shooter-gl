#include "common.h"
#include "game.h"
#include "network.h"
#include "renderer.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <chrono>

// ============================================================================
// Client State
// ============================================================================

enum class ClientState {
    MENU,
    CONNECTING,
    PLAYING,
    DEAD,
};

static GLFWwindow*   g_window = nullptr;
static Renderer      g_renderer;
static GameMap       g_map;
static ClientState   g_clientState = ClientState::MENU;
static UDPSocket     g_socket;
static sockaddr_in   g_serverAddr;

// Player state
static PlayerData    g_players[MAX_PLAYERS];
static int           g_localId = -1;
static uint32_t      g_inputSeq = 0;
static InputState    g_currentInput;

// Weapon pickups (received from server)
static std::vector<WeaponPickup> g_weaponPickups;

// Vehicles (received from server)
static VehicleData g_vehicles[MAX_VEHICLES];
static int g_numVehicles = 0;
static bool g_usePressed = false; // For toggle behavior

// CTF state
static FlagData g_flags[2];
static int g_teamScores[2] = {0, 0};

// Tornado state
static TornadoData g_tornados[MAX_TORNADOS];
static int g_numTornados = 0;

// View angles
static float g_yaw = 0, g_pitch = 0;
static double g_lastMouseX = 0, g_lastMouseY = 0;
static bool g_firstMouse = true;

// Menu state
static char g_ipBuf[64] = "127.0.0.1";
static char g_portBuf[16] = "27015";
static int  g_ipLen = 9;
static int  g_portLen = 5;
static int  g_selectedField = 0; // 0=IP, 1=port, 2=connect, 3=quit
static char g_statusMsg[128] = {};
static float g_connectTimer = 0;
static float g_connectRetryTimer = 0;

// Timing
static float g_time = 0;
static float g_deltaTime = 0;

// Kill feed
struct KillFeedEntry {
    char text[128];
    float timer;
};
static KillFeedEntry g_killFeed[5];
static int g_killFeedCount = 0;

// Window size
static int g_screenW = 1280, g_screenH = 720;

// Tab key for scoreboard
static bool g_showScoreboard = false;

// Shooting feedback
static float g_localFireCooldown = 0;
static float g_muzzleFlashTimer = 0;
static float g_hitMarkerTimer = 0;
static float g_damageFlashTimer = 0;
static int   g_lastHealth = MAX_HEALTH;

// Class selection
static uint8_t g_pendingClassSelect = 0xFF; // No pending change
static PlayerClass g_selectedClass = PlayerClass::ASSAULT;

// Footprint tracking
static float g_footstepAccum = 0;
static bool  g_footIsLeft = false;
static Vec3  g_lastFootPos = {0, 0, 0};

// ============================================================================
// GLFW Callbacks
// ============================================================================

static void framebufferSizeCallback(GLFWwindow*, int w, int h) {
    g_screenW = w;
    g_screenH = h;
    g_renderer.resize(w, h);
}

static void mouseCallback(GLFWwindow*, double xpos, double ypos) {
    if (g_clientState != ClientState::PLAYING && g_clientState != ClientState::DEAD) return;

    if (g_firstMouse) {
        g_lastMouseX = xpos;
        g_lastMouseY = ypos;
        g_firstMouse = false;
    }

    double dx = xpos - g_lastMouseX;
    double dy = ypos - g_lastMouseY;
    g_lastMouseX = xpos;
    g_lastMouseY = ypos;

    g_yaw -= (float)dx * MOUSE_SENS;
    g_pitch -= (float)dy * MOUSE_SENS;

    // Clamp pitch
    if (g_pitch > PI * 0.49f) g_pitch = PI * 0.49f;
    if (g_pitch < -PI * 0.49f) g_pitch = -PI * 0.49f;
}

static void mouseButtonCallback(GLFWwindow*, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return;
    if (g_clientState != ClientState::MENU) return;

    // Get mouse position for menu clicks
    double mx, my;
    glfwGetCursorPos(g_window, &mx, &my);
    // Convert to our coord system (origin bottom-left)
    float x = (float)mx;
    float y = (float)(g_screenH - my);

    float cx = g_screenW * 0.5f;
    float cy = g_screenH * 0.5f;
    float fieldW = 300, fieldH = 35;

    // IP field
    if (x >= cx - 150 && x <= cx - 150 + fieldW &&
        y >= cy + 60 && y <= cy + 60 + fieldH) {
        g_selectedField = 0;
    }
    // Port field
    else if (x >= cx - 150 && x <= cx - 150 + fieldW &&
             y >= cy - 20 && y <= cy - 20 + fieldH) {
        g_selectedField = 1;
    }
    // Connect button
    else if (x >= cx - 150 && x <= cx - 150 + fieldW &&
             y >= cy - 100 && y <= cy - 100 + fieldH + 5) {
        g_selectedField = 2;
    }
    // Quit button
    else if (x >= cx - 150 && x <= cx - 150 + fieldW &&
             y >= cy - 160 && y <= cy - 160 + fieldH + 5) {
        g_selectedField = 3;
    }
}

static void charCallback(GLFWwindow*, unsigned int codepoint) {
    if (g_clientState != ClientState::MENU) return;
    if (codepoint > 127) return;

    char c = (char)codepoint;

    if (g_selectedField == 0 && g_ipLen < 63) {
        g_ipBuf[g_ipLen++] = c;
        g_ipBuf[g_ipLen] = 0;
    } else if (g_selectedField == 1 && g_portLen < 15) {
        g_portBuf[g_portLen++] = c;
        g_portBuf[g_portLen] = 0;
    }
}

static void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        if (g_clientState == ClientState::PLAYING || g_clientState == ClientState::DEAD) {
            // Disconnect and go back to menu
            DisconnectPacket pkt;
            g_socket.sendTo(&pkt, sizeof(pkt), g_serverAddr);
            g_socket.close();
            g_clientState = ClientState::MENU;
            g_localId = -1;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            g_firstMouse = true;
            g_statusMsg[0] = 0;
        } else if (g_clientState == ClientState::CONNECTING) {
            g_socket.close();
            g_clientState = ClientState::MENU;
            snprintf(g_statusMsg, sizeof(g_statusMsg), "Connection cancelled");
        }
    }

    if (g_clientState == ClientState::MENU) {
        if (key == GLFW_KEY_TAB && action == GLFW_PRESS) {
            g_selectedField = (g_selectedField + 1) % 4;
        }
        if (key == GLFW_KEY_BACKSPACE && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
            if (g_selectedField == 0 && g_ipLen > 0) {
                g_ipBuf[--g_ipLen] = 0;
            } else if (g_selectedField == 1 && g_portLen > 0) {
                g_portBuf[--g_portLen] = 0;
            }
        }
        if (key == GLFW_KEY_ENTER && action == GLFW_PRESS) {
            if (g_selectedField == 3) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            } else {
                g_selectedField = 2; // Trigger connect
            }
        }
    }

    // Scoreboard
    if (key == GLFW_KEY_TAB) {
        g_showScoreboard = (action == GLFW_PRESS || action == GLFW_REPEAT);
    }

    // Class selection (1-4 keys)
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_1) { g_pendingClassSelect = 0; g_selectedClass = PlayerClass::ASSAULT; }
        if (key == GLFW_KEY_2) { g_pendingClassSelect = 1; g_selectedClass = PlayerClass::ENGINEER; }
        if (key == GLFW_KEY_3) { g_pendingClassSelect = 2; g_selectedClass = PlayerClass::SUPPORT; }
        if (key == GLFW_KEY_4) { g_pendingClassSelect = 3; g_selectedClass = PlayerClass::RECON; }
    }
}

// ============================================================================
// Networking
// ============================================================================

static void sendJoin() {
    JoinPacket pkt;
    snprintf(pkt.name, sizeof(pkt.name), "Player");
    g_socket.sendTo(&pkt, sizeof(pkt), g_serverAddr);
}

static void sendInput() {
    InputPacket pkt;
    pkt.seq = ++g_inputSeq;
    pkt.keys = g_currentInput.keys;
    pkt.yaw = g_yaw;
    pkt.pitch = g_pitch;
    pkt.classSelect = g_pendingClassSelect;
    if (g_pendingClassSelect != 0xFF) g_pendingClassSelect = 0xFF; // Send once
    g_socket.sendTo(&pkt, sizeof(pkt), g_serverAddr);
}

static void addKillFeedEntry(const char* text) {
    // Shift entries up
    if (g_killFeedCount >= 5) {
        for (int i = 0; i < 4; i++) g_killFeed[i] = g_killFeed[i + 1];
        g_killFeedCount = 4;
    }
    snprintf(g_killFeed[g_killFeedCount].text, sizeof(g_killFeed[g_killFeedCount].text), "%s", text);
    g_killFeed[g_killFeedCount].timer = 5.0f;
    g_killFeedCount++;
}

static void receivePackets() {
    uint8_t buf[16384];
    sockaddr_in fromAddr;
    int len;

    while ((len = g_socket.recvFrom(buf, sizeof(buf), fromAddr)) > 0) {
        if (len < 1) continue;
        uint8_t type = buf[0];

        switch ((ServerPacket)type) {
            case ServerPacket::JOIN_ACK: {
                if (len >= (int)sizeof(JoinAckPacket)) {
                    JoinAckPacket ack;
                    memcpy(&ack, buf, sizeof(ack));
                    g_localId = ack.playerId;
                    g_clientState = ClientState::PLAYING;
                    glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    g_firstMouse = true;
                    printf("Joined server as player %d\n", g_localId);
                }
                break;
            }

            case ServerPacket::SNAPSHOT: {
                int offset = 0;
                SnapshotPacket hdr;
                if (len < (int)sizeof(hdr)) break;
                memcpy(&hdr, buf, sizeof(hdr));
                offset += sizeof(hdr);
                g_teamScores[0] = hdr.teamScores[0];
                g_teamScores[1] = hdr.teamScores[1];

                for (uint8_t i = 0; i < hdr.numPlayers && offset + (int)sizeof(NetPlayerState) <= len; i++) {
                    NetPlayerState np;
                    memcpy(&np, buf + offset, sizeof(np));
                    offset += sizeof(np);

                    int pid = np.playerId;
                    if (pid < 0 || pid >= MAX_PLAYERS) continue;

                    g_players[pid].position = {np.x, np.y, np.z};
                    g_players[pid].state = (PlayerState)np.state;
                    g_players[pid].health = np.health;
                    g_players[pid].currentWeapon = (WeaponType)np.weapon;
                    g_players[pid].ammo = np.ammo;
                    g_players[pid].teamId = np.teamId;
                    g_players[pid].vehicleId = np.vehicleId;
                    g_players[pid].playerClass = (PlayerClass)np.playerClass;
                    g_players[pid].spotted = np.spotted != 0;

                    if (pid != g_localId) {
                        g_players[pid].yaw = np.yaw;
                        g_players[pid].pitch = np.pitch;
                    }
                }

                // Weapon pickups
                if (offset + 1 <= len) {
                    uint8_t numWeapons = buf[offset++];
                    g_weaponPickups.resize(numWeapons);
                    for (uint8_t i = 0; i < numWeapons && offset + (int)sizeof(NetWeaponState) <= len; i++) {
                        NetWeaponState nw;
                        memcpy(&nw, buf + offset, sizeof(nw));
                        offset += sizeof(nw);
                        g_weaponPickups[i].id = nw.id;
                        g_weaponPickups[i].type = (WeaponType)nw.type;
                        g_weaponPickups[i].position = {nw.x, nw.y, nw.z};
                        g_weaponPickups[i].active = nw.active != 0;
                    }
                }

                // Vehicle states
                if (offset + 1 <= len) {
                    uint8_t numVehicles = buf[offset++];
                    g_numVehicles = numVehicles;
                    for (uint8_t i = 0; i < numVehicles && offset + (int)sizeof(NetVehicleState) <= len; i++) {
                        NetVehicleState nv;
                        memcpy(&nv, buf + offset, sizeof(nv));
                        offset += sizeof(nv);
                        if (nv.id < MAX_VEHICLES) {
                            g_vehicles[nv.id].type = (VehicleType)nv.type;
                            g_vehicles[nv.id].position = {nv.x, nv.y, nv.z};
                            g_vehicles[nv.id].yaw = nv.yaw;
                            g_vehicles[nv.id].pitch = nv.pitch;
                            g_vehicles[nv.id].turretYaw = nv.turretYaw;
                            g_vehicles[nv.id].health = nv.health;
                            g_vehicles[nv.id].driverId = nv.driverId;
                            g_vehicles[nv.id].active = nv.active != 0;
                            g_vehicles[nv.id].rotorAngle = nv.rotorAngle;
                        }
                    }
                }

                // Flag states
                for (int t = 0; t < 2 && offset + (int)sizeof(NetFlagState) <= len; t++) {
                    NetFlagState nf;
                    memcpy(&nf, buf + offset, sizeof(nf));
                    offset += sizeof(nf);
                    g_flags[t].position = {nf.x, nf.y, nf.z};
                    g_flags[t].carrierId = nf.carrierId;
                    g_flags[t].atBase = nf.atBase != 0;
                }

                // Tornado states
                if (offset + 1 <= len) {
                    uint8_t numTornados = buf[offset++];
                    g_numTornados = numTornados;
                    int ti = 0;
                    for (uint8_t i = 0; i < numTornados && offset + (int)sizeof(NetTornadoState) <= len; i++) {
                        NetTornadoState nt;
                        memcpy(&nt, buf + offset, sizeof(nt));
                        offset += sizeof(nt);
                        if (ti < MAX_TORNADOS) {
                            g_tornados[ti].position = {nt.x, nt.y, nt.z};
                            g_tornados[ti].radius = nt.radius;
                            g_tornados[ti].rotation = nt.rotation;
                            g_tornados[ti].active = nt.active != 0;
                            ti++;
                        }
                    }
                    // Mark remaining as inactive
                    for (; ti < MAX_TORNADOS; ti++) g_tornados[ti].active = false;
                }

                // Update local player state from server
                if (g_localId >= 0 && g_localId < MAX_PLAYERS) {
                    auto& lp = g_players[g_localId];
                    if (lp.state == PlayerState::DEAD && g_clientState == ClientState::PLAYING) {
                        g_clientState = ClientState::DEAD;
                    } else if (lp.state == PlayerState::ALIVE && g_clientState == ClientState::DEAD) {
                        g_clientState = ClientState::PLAYING;
                    }
                }
                break;
            }

            case ServerPacket::PLAYER_HIT: {
                if (len >= (int)sizeof(PlayerHitPacket)) {
                    PlayerHitPacket pkt;
                    memcpy(&pkt, buf, sizeof(pkt));
                    // Could add hit indicator here
                }
                break;
            }

            case ServerPacket::PLAYER_DIED: {
                if (len >= (int)sizeof(PlayerDiedPacket)) {
                    PlayerDiedPacket pkt;
                    memcpy(&pkt, buf, sizeof(pkt));

                    char msg[128];
                    const char* killerName = g_players[pkt.killerId].name[0] ? g_players[pkt.killerId].name : "Bot";
                    const char* victimName = g_players[pkt.victimId].name[0] ? g_players[pkt.victimId].name : "Bot";
                    snprintf(msg, sizeof(msg), "%s killed %s", killerName, victimName);
                    addKillFeedEntry(msg);
                }
                break;
            }

            default: break;
        }
    }
}

// ============================================================================
// Game Logic (Client-side)
// ============================================================================

static void captureInput() {
    g_currentInput.keys = 0;
    if (glfwGetKey(g_window, GLFW_KEY_W) == GLFW_PRESS)     g_currentInput.keys |= InputState::KEY_W;
    if (glfwGetKey(g_window, GLFW_KEY_S) == GLFW_PRESS)     g_currentInput.keys |= InputState::KEY_S;
    if (glfwGetKey(g_window, GLFW_KEY_A) == GLFW_PRESS)     g_currentInput.keys |= InputState::KEY_A;
    if (glfwGetKey(g_window, GLFW_KEY_D) == GLFW_PRESS)     g_currentInput.keys |= InputState::KEY_D;
    if (glfwGetKey(g_window, GLFW_KEY_SPACE) == GLFW_PRESS) g_currentInput.keys |= InputState::KEY_JUMP;
    if (glfwGetMouseButton(g_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
        g_currentInput.keys |= InputState::KEY_SHOOT;
    if (glfwGetKey(g_window, GLFW_KEY_R) == GLFW_PRESS)     g_currentInput.keys |= InputState::KEY_RELOAD;
    if (glfwGetKey(g_window, GLFW_KEY_Q) == GLFW_PRESS)     g_currentInput.keys |= InputState::KEY_ABILITY;
    // E key for vehicle enter/exit (toggle, send once per press)
    bool eDown = glfwGetKey(g_window, GLFW_KEY_E) == GLFW_PRESS;
    if (eDown && !g_usePressed) g_currentInput.keys |= InputState::KEY_USE;
    g_usePressed = eDown;
    // Helicopter/plane vertical controls
    if (g_localId >= 0 && g_players[g_localId].vehicleId >= 0) {
        int vid = g_players[g_localId].vehicleId;
        if (vid < g_numVehicles) {
            VehicleType vt = g_vehicles[vid].type;
            if (vt == VehicleType::HELICOPTER || vt == VehicleType::PLANE) {
                // Space = ascend/pitch up, Ctrl = descend/pitch down
                if (glfwGetKey(g_window, GLFW_KEY_SPACE) == GLFW_PRESS)
                    g_currentInput.keys |= InputState::KEY_UP;
                if (glfwGetKey(g_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
                    g_currentInput.keys |= InputState::KEY_DOWN;
                // Override jump bit for vehicles (space used for ascend instead)
                g_currentInput.keys &= ~InputState::KEY_JUMP;
            }
        }
    }

    g_currentInput.yaw = g_yaw;
    g_currentInput.pitch = g_pitch;
}

// ============================================================================
// Connection Logic
// ============================================================================

static void startConnect() {
    if (g_ipLen == 0 || g_portLen == 0) {
        snprintf(g_statusMsg, sizeof(g_statusMsg), "Enter IP and port");
        return;
    }

    int port = atoi(g_portBuf);
    if (port <= 0 || port > 65535) {
        snprintf(g_statusMsg, sizeof(g_statusMsg), "Invalid port");
        return;
    }

    if (!g_socket.open()) {
        snprintf(g_statusMsg, sizeof(g_statusMsg), "Failed to create socket");
        return;
    }
    g_socket.setNonBlocking(true);

    g_serverAddr = UDPSocket::makeAddr(g_ipBuf, port);
    g_clientState = ClientState::CONNECTING;
    g_connectTimer = 5.0f;
    g_connectRetryTimer = 0;

    sendJoin();
    printf("Connecting to %s:%d...\n", g_ipBuf, port);
}

// ============================================================================
// Main Client Loop
// ============================================================================

int main(int, char**) {
    // Init GLFW
    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    g_window = glfwCreateWindow(g_screenW, g_screenH, "Arctic Assault", nullptr, nullptr);
    if (!g_window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1); // VSync

    // Init GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "Failed to init GLEW\n");
        return 1;
    }

    glEnable(GL_MULTISAMPLE);

    // Set callbacks
    glfwSetFramebufferSizeCallback(g_window, framebufferSizeCallback);
    glfwSetCursorPosCallback(g_window, mouseCallback);
    glfwSetMouseButtonCallback(g_window, mouseButtonCallback);
    glfwSetCharCallback(g_window, charCallback);
    glfwSetKeyCallback(g_window, keyCallback);

    // Init renderer
    g_renderer.init(g_screenW, g_screenH);

    // Build map
    g_map.buildArcticMap();
    g_renderer.buildMapMesh(g_map);

    printf("Arctic Assault Client started\n");
    printf("Map: %zu blocks\n", g_map.blocks().size());

    // Initialize all players as disconnected
    for (int i = 0; i < MAX_PLAYERS; i++) {
        g_players[i].state = PlayerState::DISCONNECTED;
    }

    auto lastTime = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(g_window)) {
        glfwPollEvents();

        auto now = std::chrono::high_resolution_clock::now();
        g_deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        g_time += g_deltaTime;

        // Cap delta time to prevent physics explosions
        if (g_deltaTime > 0.1f) g_deltaTime = 0.1f;

        // Update kill feed timers
        for (int i = 0; i < g_killFeedCount; i++) {
            g_killFeed[i].timer -= g_deltaTime;
            if (g_killFeed[i].timer <= 0) {
                for (int j = i; j < g_killFeedCount - 1; j++)
                    g_killFeed[j] = g_killFeed[j + 1];
                g_killFeedCount--;
                i--;
            }
        }

        switch (g_clientState) {
            case ClientState::MENU: {
                // Check if connect was triggered
                if (g_selectedField == 2) {
                    startConnect();
                    if (g_clientState == ClientState::MENU)
                        g_selectedField = 0; // Reset if failed
                }
                if (g_selectedField == 3) {
                    glfwSetWindowShouldClose(g_window, GLFW_TRUE);
                }

                g_renderer.beginFrame({0, 5, 0}, 0, -0.2f);
                g_renderer.renderMap();
                g_renderer.renderMenu(g_screenW, g_screenH, g_selectedField,
                                      g_ipBuf, g_portBuf, g_statusMsg, false);
                g_renderer.endFrame();
                break;
            }

            case ClientState::CONNECTING: {
                receivePackets();

                g_connectTimer -= g_deltaTime;
                g_connectRetryTimer -= g_deltaTime;

                if (g_connectRetryTimer <= 0) {
                    sendJoin();
                    g_connectRetryTimer = 0.5f;
                }

                if (g_connectTimer <= 0) {
                    g_socket.close();
                    g_clientState = ClientState::MENU;
                    snprintf(g_statusMsg, sizeof(g_statusMsg), "Connection timed out");
                }

                g_renderer.beginFrame({0, 5, 0}, 0, -0.2f);
                g_renderer.renderMap();
                g_renderer.renderMenu(g_screenW, g_screenH, 2,
                                      g_ipBuf, g_portBuf, g_statusMsg, true);
                g_renderer.endFrame();
                break;
            }

            case ClientState::PLAYING: {
                // Input
                captureInput();

                // Client-side fire tracking
                if (g_localFireCooldown > 0) g_localFireCooldown -= g_deltaTime;
                if (g_muzzleFlashTimer > 0) g_muzzleFlashTimer -= g_deltaTime;
                if (g_hitMarkerTimer > 0) g_hitMarkerTimer -= g_deltaTime;
                if (g_damageFlashTimer > 0) g_damageFlashTimer -= g_deltaTime;

                // Client-side predicted shooting
                if (g_localId >= 0 && g_localId < MAX_PLAYERS &&
                    g_players[g_localId].state == PlayerState::ALIVE) {

                    bool inVehicle = g_players[g_localId].vehicleId >= 0;
                    bool canShoot = false;

                    if (inVehicle) {
                        // Vehicle shooting: check vehicle cannon
                        int vid = g_players[g_localId].vehicleId;
                        if (vid >= 0 && vid < g_numVehicles) {
                            const auto& vdef = getVehicleDef(g_vehicles[vid].type);
                            canShoot = (g_currentInput.keys & InputState::KEY_SHOOT) &&
                                       g_localFireCooldown <= 0 && vdef.cannonDamage > 0;
                            if (canShoot) {
                                g_localFireCooldown = vdef.cannonRate;
                                g_muzzleFlashTimer = 0.1f;
                            }
                        }
                    } else {
                        // On-foot shooting
                        canShoot = (g_currentInput.keys & InputState::KEY_SHOOT) &&
                                   g_localFireCooldown <= 0 && g_players[g_localId].ammo > 0;
                        if (canShoot) {
                            const auto& def = getWeaponDef(g_players[g_localId].currentWeapon);
                            g_localFireCooldown = def.fireRate;
                            g_muzzleFlashTimer = 0.06f;
                        }
                    }

                    if (canShoot) {
                        float range = inVehicle ? 500.0f :
                            getWeaponDef(g_players[g_localId].currentWeapon).range;

                        Vec3 eyePos = g_players[g_localId].position;
                        eyePos.y += inVehicle ? 2.5f : PLAYER_EYE_HEIGHT;
                        Vec3 dir = {
                            sinf(g_yaw) * cosf(g_pitch),
                            sinf(g_pitch),
                            cosf(g_yaw) * cosf(g_pitch)
                        };
                        dir = dir.normalize();
                        float playerDist = range;
                        int hitP = GameMap::raycastPlayers(eyePos, dir, range,
                                                          g_players, MAX_PLAYERS, g_localId, playerDist);
                        Vec3 wallHit;
                        float wallDist;
                        bool hitWall = g_map.raycast(eyePos, dir, range, wallHit, wallDist);
                        if (hitP >= 0 && (!hitWall || playerDist < wallDist) &&
                            g_players[hitP].teamId != g_players[g_localId].teamId) {
                            g_hitMarkerTimer = 0.2f;
                            g_renderer.spawnBloodSplatter(g_players[hitP].position);
                        }
                        if (hitWall && (hitP < 0 || wallDist < playerDist)) {
                            Vec3 wallNorm = {0, 1, 0};
                            g_renderer.spawnBulletImpact(wallHit, wallNorm);
                        }

                        g_renderer.spawnMuzzleSpark(eyePos + dir * 0.5f, dir);
                    }

                    if (!inVehicle)
                        tickPlayer(g_players[g_localId], g_currentInput, g_map, g_deltaTime);
                }

                // Detect damage taken
                if (g_localId >= 0) {
                    int curHealth = g_players[g_localId].health;
                    if (curHealth < g_lastHealth && g_lastHealth > 0) {
                        g_damageFlashTimer = 0.3f;
                    }
                    g_lastHealth = curHealth;
                }

                // Send input to server
                sendInput();

                // Receive server updates
                receivePackets();

                // Update particles and footprints
                g_renderer.updateParticles(g_deltaTime);
                g_renderer.updateFootprints(g_deltaTime);

                // Footprint tracking
                if (g_localId >= 0 && g_players[g_localId].state == PlayerState::ALIVE) {
                    Vec3 pos = g_players[g_localId].position;
                    Vec3 diff = pos - g_lastFootPos;
                    diff.y = 0;
                    float moveDist = diff.length();
                    if (moveDist > 0.01f) {
                        g_footstepAccum += moveDist;
                        g_lastFootPos = pos;
                    }
                    if (g_footstepAccum >= 1.8f) {
                        g_footstepAccum = 0;
                        g_renderer.addFootprint(pos, g_yaw, g_footIsLeft);
                        g_renderer.spawnFootprintDust(pos);
                        g_footIsLeft = !g_footIsLeft;
                    }
                }

                // Render
                Vec3 camPos;
                float renderYaw = g_yaw;
                float renderPitch = g_pitch;
                if (g_localId >= 0) {
                    camPos = g_players[g_localId].position;
                    if (g_players[g_localId].vehicleId >= 0) {
                        // In vehicle: use vehicle position, higher camera
                        int vid = g_players[g_localId].vehicleId;
                        camPos = g_vehicles[vid].position;
                        camPos.y += 3.0f;
                    } else {
                        camPos.y += PLAYER_EYE_HEIGHT;
                    }

                    // Screen shake on damage
                    if (g_damageFlashTimer > 0) {
                        float shake = g_damageFlashTimer * 0.03f;
                        renderYaw += sinf(g_time * 60) * shake;
                        renderPitch += cosf(g_time * 45) * shake;
                    }
                }

                g_renderer.beginFrame(camPos, renderYaw, renderPitch);
                g_renderer.renderMap();
                g_renderer.renderFootprints();

                // Snow
                g_renderer.spawnSnow(camPos);

                // Render other players
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    bool isLocal = (i == g_localId);
                    g_renderer.renderPlayer(g_players[i], isLocal);
                }

                // Render weapon pickups
                for (const auto& wp : g_weaponPickups) {
                    g_renderer.renderWeaponPickup(wp, g_time);
                }

                // Render vehicles
                for (int i = 0; i < g_numVehicles; i++) {
                    g_renderer.renderVehicle(g_vehicles[i], g_time);
                }

                // Render flags
                for (int t = 0; t < 2; t++) {
                    g_renderer.renderFlag(g_flags[t], t, g_time);
                }

                // Render tornados
                for (int i = 0; i < MAX_TORNADOS; i++) {
                    if (g_tornados[i].active) {
                        g_renderer.renderTornado(g_tornados[i], g_time);
                    }
                }

                // Particles (3D scene)
                g_renderer.renderParticles();

                // Muzzle flash (bright quad in front of weapon)
                if (g_muzzleFlashTimer > 0 && g_localId >= 0) {
                    g_renderer.renderMuzzleFlash(g_screenW, g_screenH, g_muzzleFlashTimer);
                }

                // First person weapon with local fire cooldown
                if (g_localId >= 0) {
                    g_renderer.renderFirstPersonWeapon(
                        g_players[g_localId].currentWeapon,
                        g_localFireCooldown,
                        g_time);
                }

                // HUD
                if (g_localId >= 0) {
                    g_renderer.renderHUD(
                        g_players[g_localId].health,
                        g_players[g_localId].ammo,
                        g_players[g_localId].currentWeapon,
                        g_screenW, g_screenH);
                }

                // Class HUD
                if (g_localId >= 0) {
                    const auto& cdef = getClassDef(g_players[g_localId].playerClass);
                    char classBuf[96];
                    snprintf(classBuf, sizeof(classBuf), "[%s] %s  [Q] %s",
                             cdef.name, cdef.passiveDesc, cdef.abilityName);
                    g_renderer.drawText(classBuf, 10, 40, 2.0f, {0.8f, 0.8f, 0.6f},
                                        g_screenW, g_screenH);

                    // Ability cooldown bar
                    float cd = g_players[g_localId].abilityCooldown;
                    if (cd > 0) {
                        float maxCd = cdef.abilityCooldown;
                        float frac = cd / maxCd;
                        g_renderer.drawRect(10, 25, 200 * (1.0f - frac), 8,
                                            {0.2f, 0.8f, 0.3f}, 0.8f, g_screenW, g_screenH);
                        g_renderer.drawRect(10 + 200 * (1.0f - frac), 25, 200 * frac, 8,
                                            {0.3f, 0.3f, 0.3f}, 0.5f, g_screenW, g_screenH);
                    } else {
                        g_renderer.drawRect(10, 25, 200, 8,
                                            {0.2f, 0.8f, 0.3f}, 0.8f, g_screenW, g_screenH);
                        g_renderer.drawText("READY", 215, 22, 1.5f, {0.3f, 1, 0.3f},
                                            g_screenW, g_screenH);
                    }

                    // Class selection hint
                    g_renderer.drawText("[1]Assault [2]Engineer [3]Support [4]Recon",
                                        10, 8, 1.5f, {0.5f, 0.5f, 0.5f}, g_screenW, g_screenH);
                }

                // Spotted enemy markers (3D → screen projection)
                if (g_localId >= 0) {
                    for (int i = 0; i < MAX_PLAYERS; i++) {
                        if (i == g_localId) continue;
                        if (!g_players[i].spotted) continue;
                        if (g_players[i].state != PlayerState::ALIVE) continue;
                        if (g_players[i].teamId == g_players[g_localId].teamId) continue;

                        // Simple distance-based marker (above player head)
                        Vec3 diff = g_players[i].position - g_players[g_localId].position;
                        float dist = diff.length();
                        if (dist > 0.1f && dist < 100.0f) {
                            // Approximate screen position (simplified)
                            Vec3 dir = diff * (1.0f / dist);
                            float dotFwd = sinf(g_yaw) * dir.x + cosf(g_yaw) * dir.z;
                            if (dotFwd > 0) {
                                float dotRight = cosf(g_yaw) * dir.x - sinf(g_yaw) * dir.z;
                                float sx = g_screenW * 0.5f + (dotRight / dotFwd) * g_screenW * 0.5f;
                                float sy = g_screenH * 0.5f - ((dir.y + PLAYER_HEIGHT / dist) / dotFwd) * g_screenH * 0.5f;
                                sx = std::clamp(sx, 20.0f, (float)g_screenW - 20.0f);
                                sy = std::clamp(sy, 20.0f, (float)g_screenH - 20.0f);
                                char distBuf[16];
                                snprintf(distBuf, sizeof(distBuf), "%.0fm", dist);
                                g_renderer.drawText("!", sx - 4, sy, 3.0f, {1, 0.3f, 0.2f},
                                                    g_screenW, g_screenH);
                                g_renderer.drawText(distBuf, sx - 10, sy - 15, 1.5f, {1, 0.5f, 0.3f},
                                                    g_screenW, g_screenH);
                            }
                        }
                    }
                }

                // CTF HUD: team scores and flag status
                if (g_localId >= 0) {
                    char scoreBuf[64];
                    snprintf(scoreBuf, sizeof(scoreBuf), "RED %d - %d BLU",
                             g_teamScores[0], g_teamScores[1]);
                    g_renderer.drawText(scoreBuf, g_screenW * 0.5f - 60, g_screenH - 30,
                                        2.5f, {1, 1, 1}, g_screenW, g_screenH);

                    // Team indicator
                    int myTeam = g_players[g_localId].teamId;
                    Vec3 teamColor = myTeam == 0 ? Vec3{1, 0.3f, 0.3f} : Vec3{0.3f, 0.5f, 1};
                    const char* teamName = myTeam == 0 ? "TEAM RED" : "TEAM BLUE";
                    g_renderer.drawText(teamName, g_screenW * 0.5f - 40, g_screenH - 55,
                                        2.0f, teamColor, g_screenW, g_screenH);

                    // Flag status indicators
                    for (int t = 0; t < 2; t++) {
                        Vec3 fColor = t == 0 ? Vec3{1, 0.3f, 0.3f} : Vec3{0.3f, 0.5f, 1};
                        const char* flagStatus;
                        if (g_flags[t].carrierId >= 0)
                            flagStatus = t == 0 ? "RED FLAG: TAKEN" : "BLU FLAG: TAKEN";
                        else if (g_flags[t].atBase)
                            flagStatus = t == 0 ? "RED FLAG: BASE" : "BLU FLAG: BASE";
                        else
                            flagStatus = t == 0 ? "RED FLAG: DROPPED" : "BLU FLAG: DROPPED";
                        g_renderer.drawText(flagStatus, 10, g_screenH - 80 - t * 20,
                                            1.8f, fColor, g_screenW, g_screenH);
                    }
                }

                // Vehicle prompt or info
                if (g_localId >= 0 && g_players[g_localId].vehicleId >= 0) {
                    int vid = g_players[g_localId].vehicleId;
                    VehicleType vt = g_vehicles[vid].type;
                    char vbuf[96];
                    if (vt == VehicleType::HELICOPTER)
                        snprintf(vbuf, sizeof(vbuf), "%s  HP:%d  Space/Ctrl=Up/Down  [E] Exit",
                                 getVehicleDef(vt).name, g_vehicles[vid].health);
                    else if (vt == VehicleType::PLANE)
                        snprintf(vbuf, sizeof(vbuf), "%s  HP:%d  Space/Ctrl=Pitch  [E] Eject",
                                 getVehicleDef(vt).name, g_vehicles[vid].health);
                    else
                        snprintf(vbuf, sizeof(vbuf), "%s  HP:%d  [E] Exit",
                                 getVehicleDef(vt).name, g_vehicles[vid].health);
                    g_renderer.drawText(vbuf, g_screenW * 0.5f - 160, 60, 2.5f, {0.5f, 1.0f, 0.5f}, g_screenW, g_screenH);
                } else if (g_localId >= 0) {
                    // Check if near a vehicle
                    for (int i = 0; i < g_numVehicles; i++) {
                        if (!g_vehicles[i].active || g_vehicles[i].driverId >= 0) continue;
                        float d = (g_players[g_localId].position - g_vehicles[i].position).length();
                        if (d < VEHICLE_ENTER_RANGE) {
                            char vbuf[64];
                            snprintf(vbuf, sizeof(vbuf), "[E] Enter %s", getVehicleDef(g_vehicles[i].type).name);
                            g_renderer.drawText(vbuf, g_screenW * 0.5f - 80, g_screenH * 0.5f - 60, 2.5f,
                                                {1, 1, 0.5f}, g_screenW, g_screenH);
                            break;
                        }
                    }
                }

                // Crosshair (with hit marker)
                g_renderer.renderCrosshair(g_screenW, g_screenH, g_hitMarkerTimer > 0);

                // Damage flash
                if (g_damageFlashTimer > 0) {
                    g_renderer.renderDamageFlash(g_screenW, g_screenH, g_damageFlashTimer);
                }

                // Kill feed
                if (g_killFeedCount > 0) {
                    const char* msgs[5];
                    for (int i = 0; i < g_killFeedCount; i++) msgs[i] = g_killFeed[i].text;
                    g_renderer.renderKillFeed(msgs, g_killFeedCount, g_screenW, g_screenH);
                }

                // Scoreboard
                if (g_showScoreboard) {
                    g_renderer.renderScoreboard(g_players, MAX_PLAYERS, g_localId,
                                                g_screenW, g_screenH);
                }

                g_renderer.endFrame();
                break;
            }

            case ClientState::DEAD: {
                receivePackets();
                sendInput(); // Keep sending so server knows we're alive

                g_renderer.updateParticles(g_deltaTime);
                g_renderer.updateFootprints(g_deltaTime);

                Vec3 camPos;
                if (g_localId >= 0) {
                    camPos = g_players[g_localId].position;
                    camPos.y += PLAYER_EYE_HEIGHT + 2.0f; // Elevated death cam
                }

                g_renderer.beginFrame(camPos, g_yaw, g_pitch - 0.3f);
                g_renderer.renderMap();
                g_renderer.renderFootprints();
                g_renderer.spawnSnow(camPos);
                g_renderer.renderParticles();

                for (int i = 0; i < MAX_PLAYERS; i++) {
                    g_renderer.renderPlayer(g_players[i], i == g_localId);
                }
                for (const auto& wp : g_weaponPickups) {
                    g_renderer.renderWeaponPickup(wp, g_time);
                }
                for (int i = 0; i < g_numVehicles; i++) {
                    g_renderer.renderVehicle(g_vehicles[i], g_time);
                }
                for (int t = 0; t < 2; t++) {
                    g_renderer.renderFlag(g_flags[t], t, g_time);
                }
                for (int i = 0; i < MAX_TORNADOS; i++) {
                    if (g_tornados[i].active) g_renderer.renderTornado(g_tornados[i], g_time);
                }

                float timer = g_localId >= 0 ? g_players[g_localId].respawnTimer : RESPAWN_TIME;
                g_renderer.renderDeathScreen(timer, g_screenW, g_screenH);

                // Kill feed
                if (g_killFeedCount > 0) {
                    const char* msgs[5];
                    for (int i = 0; i < g_killFeedCount; i++) msgs[i] = g_killFeed[i].text;
                    g_renderer.renderKillFeed(msgs, g_killFeedCount, g_screenW, g_screenH);
                }

                g_renderer.endFrame();
                break;
            }
        }

        glfwSwapBuffers(g_window);
    }

    // Cleanup
    if (g_socket.isValid()) {
        DisconnectPacket pkt;
        g_socket.sendTo(&pkt, sizeof(pkt), g_serverAddr);
        g_socket.close();
    }

    g_renderer.shutdown();
    glfwDestroyWindow(g_window);
    glfwTerminate();

    printf("Client shutdown.\n");
    return 0;
}
