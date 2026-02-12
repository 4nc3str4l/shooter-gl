#pragma once

#include "common.h"
#include "game.h"
#include <GL/glew.h>

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
    void renderWeaponPickup(const WeaponPickup& w);
    void renderFirstPersonWeapon(WeaponType type, float fireCooldown, float time);
    void renderHUD(int health, int ammo, WeaponType weapon, int screenW, int screenH);
    void renderCrosshair(int screenW, int screenH);
    void renderMenu(int screenW, int screenH, int selectedField,
                    const char* ipBuf, const char* portBuf,
                    const char* statusMsg, bool connecting);
    void renderScoreboard(const PlayerData players[], int numPlayers,
                          int localId, int screenW, int screenH);
    void renderDeathScreen(float timer, int screenW, int screenH);
    void renderKillFeed(const char* messages[], int count, int screenW, int screenH);
    void endFrame();

    // Text rendering
    void drawText(const char* text, float x, float y, float scale,
                  const Vec3& color, int screenW, int screenH);
    void drawRect(float x, float y, float w, float h,
                  const Vec3& color, float alpha, int screenW, int screenH);

private:
    GLuint worldShader_ = 0, hudShader_ = 0;
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

    Mat4 projectionMatrix_;
    Mat4 viewMatrix_;

    int width_ = 1280, height_ = 720;

    GLuint compileShader(GLenum type, const char* source);
    GLuint linkProgram(GLuint vert, GLuint frag);
    void drawCube(const Mat4& model, const Vec3& color);
    void drawSphere(const Mat4& model, const Vec3& color);
    void drawCylinder(const Mat4& model, const Vec3& color);
    void buildFontTexture();
};
