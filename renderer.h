#pragma once

#include "common.h"
#include "game.h"
#include <GL/glew.h>
#include <vector>

// ============================================================================
// Particle System
// ============================================================================

enum class ParticleType : uint8_t {
    SNOW,           // Falling snow
    BULLET_IMPACT,  // Sparks/debris on wall hit
    BLOOD,          // Blood splatter on player hit
    MUZZLE_SPARK,   // Muzzle flash sparks
    FOOTPRINT_DUST, // Dust when walking on snow
};

struct Particle {
    Vec3 position;
    Vec3 velocity;
    Vec3 color;
    float life = 0;
    float maxLife = 1;
    float size = 0.1f;
    ParticleType type = ParticleType::SNOW;
    float gravity = 1.0f;
};

// ============================================================================
// Footprint
// ============================================================================

struct Footprint {
    Vec3 position;
    float yaw;
    float life;      // Fades over time
    bool isLeft;
};

// ============================================================================
// Renderer
// ============================================================================

class Renderer {
public:
    void init(int width, int height);
    void shutdown();
    void resize(int width, int height);

    // Mesh building (called once at startup)
    void buildMapMesh(const GameMap& map);
    void buildPrimitiveMeshes();

    // Per-frame rendering
    void beginFrame(const Vec3& cameraPos, float yaw, float pitch);
    void renderMap();
    void renderPlayer(const PlayerData& p, bool isLocalPlayer);
    void renderWeaponPickup(const WeaponPickup& w, float time = 0);
    void renderFirstPersonWeapon(WeaponType type, float fireCooldown, float time);
    void renderHUD(int health, int ammo, WeaponType weapon, int screenW, int screenH);
    void renderCrosshair(int screenW, int screenH, bool hitMarker = false);
    void renderMuzzleFlash(int screenW, int screenH, float timer);
    void renderDamageFlash(int screenW, int screenH, float timer);
    void renderMenu(int screenW, int screenH, int selectedField,
                    const char* ipBuf, const char* portBuf,
                    const char* statusMsg, bool connecting);
    void renderScoreboard(const PlayerData players[], int numPlayers,
                          int localId, int screenW, int screenH);
    void renderDeathScreen(float timer, int screenW, int screenH);
    void renderKillFeed(const char* messages[], int count, int screenW, int screenH);
    void endFrame();

    // Particle system
    void updateParticles(float dt);
    void renderParticles();
    void spawnSnow(const Vec3& cameraPos);
    void spawnBulletImpact(const Vec3& pos, const Vec3& normal);
    void spawnBloodSplatter(const Vec3& pos);
    void spawnMuzzleSpark(const Vec3& pos, const Vec3& dir);
    void spawnFootprintDust(const Vec3& pos);

    // Footprints
    void addFootprint(const Vec3& pos, float yaw, bool isLeft);
    void renderFootprints();
    void updateFootprints(float dt);

    // Text rendering
    void drawText(const char* text, float x, float y, float scale,
                  const Vec3& color, int screenW, int screenH);
    void drawRect(float x, float y, float w, float h,
                  const Vec3& color, float alpha, int screenW, int screenH);

private:
    GLuint worldShader_ = 0, hudShader_ = 0, particleShader_ = 0;
    GLuint mapVAO_ = 0, mapVBO_ = 0;
    int    mapVertexCount_ = 0;
    GLuint cubeVAO_ = 0, cubeVBO_ = 0;
    int    cubeVertexCount_ = 0;
    GLuint sphereVAO_ = 0, sphereVBO_ = 0;
    int    sphereVertexCount_ = 0;
    GLuint cylinderVAO_ = 0, cylinderVBO_ = 0;
    int    cylinderVertexCount_ = 0;
    GLuint quadVAO_ = 0, quadVBO_ = 0;
    GLuint fontTexture_ = 0;

    // Particle rendering
    GLuint particleVAO_ = 0, particleVBO_ = 0;
    static constexpr int MAX_PARTICLES = 4000;
    std::vector<Particle> particles_;
    float snowSpawnAccum_ = 0;

    // Footprints
    static constexpr int MAX_FOOTPRINTS = 200;
    std::vector<Footprint> footprints_;

    Mat4 projectionMatrix_;
    Mat4 viewMatrix_;
    Vec3 cameraPos_;

    int width_ = 1280, height_ = 720;

    GLuint compileShader(GLenum type, const char* source);
    GLuint linkProgram(GLuint vert, GLuint frag);
    void drawCube(const Mat4& model, const Vec3& color);
    void drawSphere(const Mat4& model, const Vec3& color);
    void drawCylinder(const Mat4& model, const Vec3& color);
    void buildFontTexture();
    void buildParticleMesh();
};
