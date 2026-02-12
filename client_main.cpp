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
    uint8_t buf[2048];
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

                // Client-side prediction
                if (g_localId >= 0 && g_localId < MAX_PLAYERS &&
                    g_players[g_localId].state == PlayerState::ALIVE) {
                    tickPlayer(g_players[g_localId], g_currentInput, g_map, g_deltaTime);
                }

                // Send input to server
                sendInput();

                // Receive server updates
                receivePackets();

                // Render
                Vec3 camPos;
                if (g_localId >= 0) {
                    camPos = g_players[g_localId].position;
                    camPos.y += PLAYER_EYE_HEIGHT;
                }

                g_renderer.beginFrame(camPos, g_yaw, g_pitch);
                g_renderer.renderMap();

                // Render other players
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    bool isLocal = (i == g_localId);
                    g_renderer.renderPlayer(g_players[i], isLocal);
                }

                // Render weapon pickups
                for (const auto& wp : g_weaponPickups) {
                    g_renderer.renderWeaponPickup(wp, g_time);
                }

                // First person weapon
                if (g_localId >= 0) {
                    g_renderer.renderFirstPersonWeapon(
                        g_players[g_localId].currentWeapon,
                        g_players[g_localId].fireCooldown,
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

                g_renderer.renderCrosshair(g_screenW, g_screenH);

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

                Vec3 camPos;
                if (g_localId >= 0) {
                    camPos = g_players[g_localId].position;
                    camPos.y += PLAYER_EYE_HEIGHT + 2.0f; // Elevated death cam
                }

                g_renderer.beginFrame(camPos, g_yaw, g_pitch - 0.3f);
                g_renderer.renderMap();

                for (int i = 0; i < MAX_PLAYERS; i++) {
                    g_renderer.renderPlayer(g_players[i], i == g_localId);
                }
                for (const auto& wp : g_weaponPickups) {
                    g_renderer.renderWeaponPickup(wp, g_time);
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
