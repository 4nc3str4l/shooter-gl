#include "renderer.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <algorithm>

// ============================================================================
// Shader Sources
// ============================================================================

static const char* worldVertSrc = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec3 aColor;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec3 vNormal;
out vec3 vColor;
out vec3 vWorldPos;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal = mat3(uModel) * aNormal;
    vColor = aColor;
    vWorldPos = (uModel * vec4(aPos, 1.0)).xyz;
}
)";

static const char* worldFragSrc = R"(
#version 330 core
in vec3 vNormal;
in vec3 vColor;
in vec3 vWorldPos;
out vec4 FragColor;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbient;
void main() {
    vec3 N = normalize(vNormal);
    float NdotL = max(dot(N, uSunDir), 0.0);
    vec3 lit = vColor * (uAmbient + uSunColor * NdotL);
    float dist = length(vWorldPos);
    float fog = clamp((dist - 60.0) / 80.0, 0.0, 0.65);
    vec3 fogColor = vec3(0.82, 0.85, 0.92);
    FragColor = vec4(mix(lit, fogColor, fog), 1.0);
}
)";

static const char* hudVertSrc = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uProj;
out vec2 vUV;
void main() {
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
    vUV = aUV;
}
)";

static const char* hudFragSrc = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform vec4 uColor;
uniform sampler2D uTex;
uniform int uUseTexture;
void main() {
    if (uUseTexture == 1) {
        float a = texture(uTex, vUV).r;
        FragColor = vec4(uColor.rgb, uColor.a * a);
    } else {
        FragColor = uColor;
    }
}
)";

// ============================================================================
// Particle Shader
// ============================================================================

static const char* particleVertSrc = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
layout(location=2) in float aSize;
uniform mat4 uVP;
out vec4 vColor;
void main() {
    gl_Position = uVP * vec4(aPos, 1.0);
    gl_PointSize = aSize / gl_Position.w * 400.0;
    vColor = aColor;
}
)";

static const char* particleFragSrc = R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() {
    // Soft circle shape
    vec2 coord = gl_PointCoord - vec2(0.5);
    float dist = length(coord);
    if (dist > 0.5) discard;
    float alpha = smoothstep(0.5, 0.2, dist) * vColor.a;
    FragColor = vec4(vColor.rgb, alpha);
}
)";

static float pRandf() { return (float)rand() / RAND_MAX; }
static float pRandf(float mn, float mx) { return mn + pRandf() * (mx - mn); }

// ============================================================================
// 8x8 Bitmap Font (CP437 subset, printable ASCII 32-126)
// ============================================================================

static const uint8_t FONT_DATA[] = {
    // Each character is 8 bytes (8 rows of 8 pixels, MSB left)
    // Space (32)
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // ! (33)
    0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00,
    // " (34)
    0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00,
    // # (35)
    0x6C,0xFE,0x6C,0x6C,0xFE,0x6C,0x00,0x00,
    // $ (36)
    0x18,0x7E,0x58,0x7E,0x1A,0x7E,0x18,0x00,
    // % (37)
    0x62,0x64,0x08,0x10,0x26,0x46,0x00,0x00,
    // & (38)
    0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00,
    // ' (39)
    0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00,
    // ( (40)
    0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00,
    // ) (41)
    0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00,
    // * (42)
    0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,
    // + (43)
    0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,
    // , (44)
    0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30,
    // - (45)
    0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,
    // . (46)
    0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,
    // / (47)
    0x02,0x06,0x0C,0x18,0x30,0x60,0x40,0x00,
    // 0 (48)
    0x3C,0x66,0x6E,0x7E,0x76,0x66,0x3C,0x00,
    // 1 (49)
    0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00,
    // 2 (50)
    0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00,
    // 3 (51)
    0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00,
    // 4 (52)
    0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00,
    // 5 (53)
    0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00,
    // 6 (54)
    0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00,
    // 7 (55)
    0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00,
    // 8 (56)
    0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00,
    // 9 (57)
    0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00,
    // : (58)
    0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00,
    // ; (59)
    0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00,
    // < (60)
    0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00,
    // = (61)
    0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00,
    // > (62)
    0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00,
    // ? (63)
    0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00,
    // @ (64)
    0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x3E,0x00,
    // A (65)
    0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00,
    // B (66)
    0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00,
    // C (67)
    0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00,
    // D (68)
    0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00,
    // E (69)
    0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00,
    // F (70)
    0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00,
    // G (71)
    0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0x00,
    // H (72)
    0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00,
    // I (73)
    0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,
    // J (74)
    0x1E,0x0C,0x0C,0x0C,0x6C,0x6C,0x38,0x00,
    // K (75)
    0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00,
    // L (76)
    0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00,
    // M (77)
    0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00,
    // N (78)
    0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00,
    // O (79)
    0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,
    // P (80)
    0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00,
    // Q (81)
    0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00,
    // R (82)
    0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00,
    // S (83)
    0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00,
    // T (84)
    0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00,
    // U (85)
    0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,
    // V (86)
    0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00,
    // W (87)
    0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00,
    // X (88)
    0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00,
    // Y (89)
    0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00,
    // Z (90)
    0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00,
    // [ (91)
    0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,
    // backslash (92)
    0x40,0x60,0x30,0x18,0x0C,0x06,0x02,0x00,
    // ] (93)
    0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,
    // ^ (94)
    0x10,0x38,0x6C,0x00,0x00,0x00,0x00,0x00,
    // _ (95)
    0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00,
    // ` (96)
    0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,
    // a (97)
    0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00,
    // b (98)
    0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00,
    // c (99)
    0x00,0x00,0x3C,0x66,0x60,0x66,0x3C,0x00,
    // d (100)
    0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00,
    // e (101)
    0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00,
    // f (102)
    0x1C,0x36,0x30,0x7C,0x30,0x30,0x30,0x00,
    // g (103)
    0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C,
    // h (104)
    0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x00,
    // i (105)
    0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00,
    // j (106)
    0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x6C,0x38,
    // k (107)
    0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00,
    // l (108)
    0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,
    // m (109)
    0x00,0x00,0xCC,0xFE,0xD6,0xC6,0xC6,0x00,
    // n (110)
    0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00,
    // o (111)
    0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00,
    // p (112)
    0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60,
    // q (113)
    0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06,
    // r (114)
    0x00,0x00,0x6C,0x76,0x60,0x60,0x60,0x00,
    // s (115)
    0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00,
    // t (116)
    0x30,0x30,0x7C,0x30,0x30,0x36,0x1C,0x00,
    // u (117)
    0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00,
    // v (118)
    0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00,
    // w (119)
    0x00,0x00,0xC6,0xC6,0xD6,0xFE,0x6C,0x00,
    // x (120)
    0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00,
    // y (121)
    0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C,
    // z (122)
    0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00,
    // { (123)
    0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00,
    // | (124)
    0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,
    // } (125)
    0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00,
    // ~ (126)
    0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,
};

// ============================================================================
// Shader Compilation
// ============================================================================

GLuint Renderer::compileShader(GLenum type, const char* source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);

    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "Shader compile error: %s\n", log);
    }
    return s;
}

GLuint Renderer::linkProgram(GLuint vert, GLuint frag) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vert);
    glAttachShader(p, frag);
    glLinkProgram(p);

    int ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        fprintf(stderr, "Program link error: %s\n", log);
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return p;
}

// ============================================================================
// Initialization
// ============================================================================

void Renderer::init(int width, int height) {
    width_ = width;
    height_ = height;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.82f, 0.85f, 0.92f, 1.0f); // Arctic sky color

    // Compile shaders
    worldShader_ = linkProgram(
        compileShader(GL_VERTEX_SHADER, worldVertSrc),
        compileShader(GL_FRAGMENT_SHADER, worldFragSrc));

    hudShader_ = linkProgram(
        compileShader(GL_VERTEX_SHADER, hudVertSrc),
        compileShader(GL_FRAGMENT_SHADER, hudFragSrc));

    particleShader_ = linkProgram(
        compileShader(GL_VERTEX_SHADER, particleVertSrc),
        compileShader(GL_FRAGMENT_SHADER, particleFragSrc));

    glEnable(GL_PROGRAM_POINT_SIZE);

    buildPrimitiveMeshes();
    buildParticleMesh();
    buildFontTexture();

    particles_.reserve(MAX_PARTICLES);
    footprints_.reserve(MAX_FOOTPRINTS);
}

void Renderer::shutdown() {
    if (mapVAO_) { glDeleteVertexArrays(1, &mapVAO_); glDeleteBuffers(1, &mapVBO_); }
    if (cubeVAO_) { glDeleteVertexArrays(1, &cubeVAO_); glDeleteBuffers(1, &cubeVBO_); }
    if (sphereVAO_) { glDeleteVertexArrays(1, &sphereVAO_); glDeleteBuffers(1, &sphereVBO_); }
    if (cylinderVAO_) { glDeleteVertexArrays(1, &cylinderVAO_); glDeleteBuffers(1, &cylinderVBO_); }
    if (quadVAO_) { glDeleteVertexArrays(1, &quadVAO_); glDeleteBuffers(1, &quadVBO_); }
    if (particleVAO_) { glDeleteVertexArrays(1, &particleVAO_); glDeleteBuffers(1, &particleVBO_); }
    if (fontTexture_) glDeleteTextures(1, &fontTexture_);
    if (worldShader_) glDeleteProgram(worldShader_);
    if (hudShader_) glDeleteProgram(hudShader_);
    if (particleShader_) glDeleteProgram(particleShader_);
}

void Renderer::resize(int width, int height) {
    width_ = width;
    height_ = height;
    glViewport(0, 0, width, height);
}

// ============================================================================
// Mesh Generation
// ============================================================================

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float cr, cg, cb;
};

static void addQuadFace(std::vector<Vertex>& verts,
                        Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, Vec3 normal, Vec3 color) {
    Vertex v;
    v.nx = normal.x; v.ny = normal.y; v.nz = normal.z;
    v.cr = color.x; v.cg = color.y; v.cb = color.z;

    // Triangle 1
    v.px = p0.x; v.py = p0.y; v.pz = p0.z; verts.push_back(v);
    v.px = p1.x; v.py = p1.y; v.pz = p1.z; verts.push_back(v);
    v.px = p2.x; v.py = p2.y; v.pz = p2.z; verts.push_back(v);
    // Triangle 2
    v.px = p0.x; v.py = p0.y; v.pz = p0.z; verts.push_back(v);
    v.px = p2.x; v.py = p2.y; v.pz = p2.z; verts.push_back(v);
    v.px = p3.x; v.py = p3.y; v.pz = p3.z; verts.push_back(v);
}

static void addBox(std::vector<Vertex>& verts, const Vec3& mn, const Vec3& mx, const Vec3& color) {
    Vec3 c = color;
    // Front (+Z)
    addQuadFace(verts, {mn.x,mn.y,mx.z},{mx.x,mn.y,mx.z},{mx.x,mx.y,mx.z},{mn.x,mx.y,mx.z}, {0,0,1}, c);
    // Back (-Z)
    addQuadFace(verts, {mx.x,mn.y,mn.z},{mn.x,mn.y,mn.z},{mn.x,mx.y,mn.z},{mx.x,mx.y,mn.z}, {0,0,-1}, c);
    // Right (+X)
    addQuadFace(verts, {mx.x,mn.y,mx.z},{mx.x,mn.y,mn.z},{mx.x,mx.y,mn.z},{mx.x,mx.y,mx.z}, {1,0,0}, c);
    // Left (-X)
    addQuadFace(verts, {mn.x,mn.y,mn.z},{mn.x,mn.y,mx.z},{mn.x,mx.y,mx.z},{mn.x,mx.y,mn.z}, {-1,0,0}, c);
    // Top (+Y)
    Vec3 tc = {c.x*1.1f, c.y*1.1f, c.z*1.1f}; // Slightly brighter top
    addQuadFace(verts, {mn.x,mx.y,mx.z},{mx.x,mx.y,mx.z},{mx.x,mx.y,mn.z},{mn.x,mx.y,mn.z}, {0,1,0}, tc);
    // Bottom (-Y)
    Vec3 bc = {c.x*0.7f, c.y*0.7f, c.z*0.7f}; // Darker bottom
    addQuadFace(verts, {mn.x,mn.y,mn.z},{mx.x,mn.y,mn.z},{mx.x,mn.y,mx.z},{mn.x,mn.y,mx.z}, {0,-1,0}, bc);
}

void Renderer::buildMapMesh(const GameMap& map) {
    std::vector<Vertex> verts;
    verts.reserve(map.blocks().size() * 36);

    for (const auto& b : map.blocks()) {
        addBox(verts, b.bounds.min, b.bounds.max, b.color);
    }

    mapVertexCount_ = (int)verts.size();

    glGenVertexArrays(1, &mapVAO_);
    glGenBuffers(1, &mapVBO_);
    glBindVertexArray(mapVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, mapVBO_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(6*sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void Renderer::buildPrimitiveMeshes() {
    // --- Unit Cube (centered at origin, size 1x1x1) ---
    {
        std::vector<Vertex> verts;
        Vec3 white = {1,1,1};
        addBox(verts, {-0.5f,-0.5f,-0.5f}, {0.5f,0.5f,0.5f}, white);
        cubeVertexCount_ = (int)verts.size();

        glGenVertexArrays(1, &cubeVAO_);
        glGenBuffers(1, &cubeVBO_);
        glBindVertexArray(cubeVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO_);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(6*sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }

    // --- UV Sphere (radius 1, centered at origin) ---
    {
        std::vector<Vertex> verts;
        int lonSegs = 12, latSegs = 8;
        for (int lat = 0; lat < latSegs; lat++) {
            float theta1 = PI * lat / latSegs;
            float theta2 = PI * (lat + 1) / latSegs;
            for (int lon = 0; lon < lonSegs; lon++) {
                float phi1 = 2 * PI * lon / lonSegs;
                float phi2 = 2 * PI * (lon + 1) / lonSegs;

                Vec3 p1 = {sinf(theta1)*cosf(phi1), cosf(theta1), sinf(theta1)*sinf(phi1)};
                Vec3 p2 = {sinf(theta1)*cosf(phi2), cosf(theta1), sinf(theta1)*sinf(phi2)};
                Vec3 p3 = {sinf(theta2)*cosf(phi2), cosf(theta2), sinf(theta2)*sinf(phi2)};
                Vec3 p4 = {sinf(theta2)*cosf(phi1), cosf(theta2), sinf(theta2)*sinf(phi1)};

                Vertex v;
                v.cr = 1; v.cg = 1; v.cb = 1;

                // Triangle 1
                v.px=p1.x; v.py=p1.y; v.pz=p1.z; v.nx=p1.x; v.ny=p1.y; v.nz=p1.z; verts.push_back(v);
                v.px=p2.x; v.py=p2.y; v.pz=p2.z; v.nx=p2.x; v.ny=p2.y; v.nz=p2.z; verts.push_back(v);
                v.px=p3.x; v.py=p3.y; v.pz=p3.z; v.nx=p3.x; v.ny=p3.y; v.nz=p3.z; verts.push_back(v);
                // Triangle 2
                v.px=p1.x; v.py=p1.y; v.pz=p1.z; v.nx=p1.x; v.ny=p1.y; v.nz=p1.z; verts.push_back(v);
                v.px=p3.x; v.py=p3.y; v.pz=p3.z; v.nx=p3.x; v.ny=p3.y; v.nz=p3.z; verts.push_back(v);
                v.px=p4.x; v.py=p4.y; v.pz=p4.z; v.nx=p4.x; v.ny=p4.y; v.nz=p4.z; verts.push_back(v);
            }
        }
        sphereVertexCount_ = (int)verts.size();
        glGenVertexArrays(1, &sphereVAO_);
        glGenBuffers(1, &sphereVBO_);
        glBindVertexArray(sphereVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, sphereVBO_);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(6*sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }

    // --- Cylinder (radius 1, height 1, along Y axis) ---
    {
        std::vector<Vertex> verts;
        int segs = 12;
        for (int i = 0; i < segs; i++) {
            float a1 = 2 * PI * i / segs;
            float a2 = 2 * PI * (i + 1) / segs;
            float c1 = cosf(a1), s1 = sinf(a1);
            float c2 = cosf(a2), s2 = sinf(a2);

            Vec3 n1 = {c1, 0, s1};
            Vec3 n2 = {c2, 0, s2};

            Vertex v;
            v.cr = 1; v.cg = 1; v.cb = 1;

            // Side quad (2 triangles)
            v.px=c1; v.py=-0.5f; v.pz=s1; v.nx=n1.x; v.ny=0; v.nz=n1.z; verts.push_back(v);
            v.px=c2; v.py=-0.5f; v.pz=s2; v.nx=n2.x; v.ny=0; v.nz=n2.z; verts.push_back(v);
            v.px=c2; v.py=0.5f;  v.pz=s2; v.nx=n2.x; v.ny=0; v.nz=n2.z; verts.push_back(v);

            v.px=c1; v.py=-0.5f; v.pz=s1; v.nx=n1.x; v.ny=0; v.nz=n1.z; verts.push_back(v);
            v.px=c2; v.py=0.5f;  v.pz=s2; v.nx=n2.x; v.ny=0; v.nz=n2.z; verts.push_back(v);
            v.px=c1; v.py=0.5f;  v.pz=s1; v.nx=n1.x; v.ny=0; v.nz=n1.z; verts.push_back(v);

            // Top cap
            v.nx=0; v.ny=1; v.nz=0;
            v.px=0; v.py=0.5f; v.pz=0; verts.push_back(v);
            v.px=c1; v.py=0.5f; v.pz=s1; verts.push_back(v);
            v.px=c2; v.py=0.5f; v.pz=s2; verts.push_back(v);

            // Bottom cap
            v.nx=0; v.ny=-1; v.nz=0;
            v.px=0; v.py=-0.5f; v.pz=0; verts.push_back(v);
            v.px=c2; v.py=-0.5f; v.pz=s2; verts.push_back(v);
            v.px=c1; v.py=-0.5f; v.pz=s1; verts.push_back(v);
        }
        cylinderVertexCount_ = (int)verts.size();
        glGenVertexArrays(1, &cylinderVAO_);
        glGenBuffers(1, &cylinderVBO_);
        glBindVertexArray(cylinderVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, cylinderVBO_);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(6*sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }

    // --- Screen quad for HUD ---
    {
        float quadData[] = {
            // pos (x,y), uv (u,v)
            0, 0,  0, 0,
            1, 0,  1, 0,
            1, 1,  1, 1,
            0, 0,  0, 0,
            1, 1,  1, 1,
            0, 1,  0, 1,
        };
        glGenVertexArrays(1, &quadVAO_);
        glGenBuffers(1, &quadVBO_);
        glBindVertexArray(quadVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadData), quadData, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }
}

void Renderer::buildFontTexture() {
    // Create a 128x64 texture atlas (16 chars x 6 rows, each 8x8)
    int atlasW = 128, atlasH = 64;
    std::vector<uint8_t> pixels(atlasW * atlasH, 0);

    for (int ch = 32; ch <= 126; ch++) {
        int idx = ch - 32;
        int cx = (idx % 16) * 8;
        int cy = (idx / 16) * 8;
        const uint8_t* glyph = &FONT_DATA[idx * 8];
        for (int row = 0; row < 8; row++) {
            for (int bit = 0; bit < 8; bit++) {
                if (glyph[row] & (0x80 >> bit)) {
                    pixels[(cy + row) * atlasW + cx + bit] = 255;
                }
            }
        }
    }

    glGenTextures(1, &fontTexture_);
    glBindTexture(GL_TEXTURE_2D, fontTexture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlasW, atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// ============================================================================
// Drawing Helpers
// ============================================================================

void Renderer::drawCube(const Mat4& model, const Vec3& color) {
    Mat4 mvp = projectionMatrix_ * viewMatrix_ * model;
    glUseProgram(worldShader_);
    glUniformMatrix4fv(glGetUniformLocation(worldShader_, "uMVP"), 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(glGetUniformLocation(worldShader_, "uModel"), 1, GL_FALSE, model.m);

    // Override color via a uniform trick: we multiply vertex color (white) by our desired color
    // Actually our cube mesh has white color, so we set sun/ambient to produce desired color
    // Simpler: use the existing lighting but set uniform for color override
    Vec3 sunDir = Vec3{0.4f, 0.8f, 0.3f}.normalize();
    glUniform3f(glGetUniformLocation(worldShader_, "uSunDir"), sunDir.x, sunDir.y, sunDir.z);
    glUniform3f(glGetUniformLocation(worldShader_, "uSunColor"), 0.6f * color.x, 0.6f * color.y, 0.6f * color.z);
    glUniform3f(glGetUniformLocation(worldShader_, "uAmbient"), 0.4f * color.x, 0.4f * color.y, 0.4f * color.z);

    glBindVertexArray(cubeVAO_);
    glDrawArrays(GL_TRIANGLES, 0, cubeVertexCount_);
}

void Renderer::drawSphere(const Mat4& model, const Vec3& color) {
    Mat4 mvp = projectionMatrix_ * viewMatrix_ * model;
    glUseProgram(worldShader_);
    glUniformMatrix4fv(glGetUniformLocation(worldShader_, "uMVP"), 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(glGetUniformLocation(worldShader_, "uModel"), 1, GL_FALSE, model.m);

    Vec3 sunDir = Vec3{0.4f, 0.8f, 0.3f}.normalize();
    glUniform3f(glGetUniformLocation(worldShader_, "uSunDir"), sunDir.x, sunDir.y, sunDir.z);
    glUniform3f(glGetUniformLocation(worldShader_, "uSunColor"), 0.6f * color.x, 0.6f * color.y, 0.6f * color.z);
    glUniform3f(glGetUniformLocation(worldShader_, "uAmbient"), 0.4f * color.x, 0.4f * color.y, 0.4f * color.z);

    glBindVertexArray(sphereVAO_);
    glDrawArrays(GL_TRIANGLES, 0, sphereVertexCount_);
}

void Renderer::drawCylinder(const Mat4& model, const Vec3& color) {
    Mat4 mvp = projectionMatrix_ * viewMatrix_ * model;
    glUseProgram(worldShader_);
    glUniformMatrix4fv(glGetUniformLocation(worldShader_, "uMVP"), 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(glGetUniformLocation(worldShader_, "uModel"), 1, GL_FALSE, model.m);

    Vec3 sunDir = Vec3{0.4f, 0.8f, 0.3f}.normalize();
    glUniform3f(glGetUniformLocation(worldShader_, "uSunDir"), sunDir.x, sunDir.y, sunDir.z);
    glUniform3f(glGetUniformLocation(worldShader_, "uSunColor"), 0.6f * color.x, 0.6f * color.y, 0.6f * color.z);
    glUniform3f(glGetUniformLocation(worldShader_, "uAmbient"), 0.4f * color.x, 0.4f * color.y, 0.4f * color.z);

    glBindVertexArray(cylinderVAO_);
    glDrawArrays(GL_TRIANGLES, 0, cylinderVertexCount_);
}

// ============================================================================
// Frame Rendering
// ============================================================================

void Renderer::beginFrame(const Vec3& cameraPos, float yaw, float pitch) {
    cameraPos_ = cameraPos;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = (float)width_ / (float)height_;
    projectionMatrix_ = Mat4::perspective(70.0f * PI / 180.0f, aspect, 0.1f, 500.0f);

    Vec3 forward = {
        sinf(yaw) * cosf(pitch),
        sinf(pitch),
        cosf(yaw) * cosf(pitch)
    };
    Vec3 target = cameraPos + forward;
    viewMatrix_ = Mat4::lookAt(cameraPos, target, {0, 1, 0});
}

void Renderer::renderMap() {
    Mat4 model = Mat4::identity();
    Mat4 mvp = projectionMatrix_ * viewMatrix_ * model;

    glUseProgram(worldShader_);
    glUniformMatrix4fv(glGetUniformLocation(worldShader_, "uMVP"), 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(glGetUniformLocation(worldShader_, "uModel"), 1, GL_FALSE, model.m);

    Vec3 sunDir = Vec3{0.4f, 0.8f, 0.3f}.normalize();
    glUniform3f(glGetUniformLocation(worldShader_, "uSunDir"), sunDir.x, sunDir.y, sunDir.z);
    glUniform3f(glGetUniformLocation(worldShader_, "uSunColor"), 0.95f, 0.92f, 0.85f);
    glUniform3f(glGetUniformLocation(worldShader_, "uAmbient"), 0.35f, 0.38f, 0.45f);

    glBindVertexArray(mapVAO_);
    glDrawArrays(GL_TRIANGLES, 0, mapVertexCount_);
}

void Renderer::renderPlayer(const PlayerData& p, bool isLocalPlayer) {
    if (p.state != PlayerState::ALIVE || isLocalPlayer) return;

    Vec3 pos = p.position;

    // Team colors
    Vec3 bodyColor = {0.2f, 0.35f, 0.6f};  // Blue team look
    Vec3 skinColor = {0.85f, 0.72f, 0.6f};
    Vec3 legColor  = {0.25f, 0.25f, 0.3f};

    // Torso
    Mat4 torso = Mat4::translate({pos.x, pos.y + 0.9f, pos.z}) *
                 Mat4::rotateY(-p.yaw) *
                 Mat4::scale({0.6f, 0.8f, 0.35f});
    drawCube(torso, bodyColor);

    // Head
    Mat4 head = Mat4::translate({pos.x, pos.y + 1.55f, pos.z}) *
                Mat4::scale({0.22f, 0.22f, 0.22f});
    drawSphere(head, skinColor);

    // Legs
    float legOffX = 0.15f;
    for (int i = -1; i <= 1; i += 2) {
        float lx = pos.x + sinf(p.yaw + PI * 0.5f) * legOffX * i;
        float lz = pos.z + cosf(p.yaw + PI * 0.5f) * legOffX * i;
        Mat4 leg = Mat4::translate({lx, pos.y + 0.25f, lz}) *
                   Mat4::scale({0.12f, 0.5f, 0.12f});
        drawCylinder(leg, legColor);
    }

    // Arms
    float armOffX = 0.38f;
    for (int i = -1; i <= 1; i += 2) {
        float ax = pos.x + sinf(p.yaw + PI * 0.5f) * armOffX * i;
        float az = pos.z + cosf(p.yaw + PI * 0.5f) * armOffX * i;
        Mat4 arm = Mat4::translate({ax, pos.y + 0.85f, az}) *
                   Mat4::rotateY(-p.yaw) *
                   Mat4::rotateX(-0.3f) *
                   Mat4::scale({0.1f, 0.55f, 0.1f});
        drawCylinder(arm, bodyColor);
    }
}

void Renderer::renderWeaponPickup(const WeaponPickup& w, float time) {
    if (!w.active) return;

    Vec3 pos = w.position;

    // Color per weapon type
    Vec3 color;
    switch (w.type) {
        case WeaponType::SHOTGUN: color = {0.7f, 0.4f, 0.2f}; break;
        case WeaponType::RIFLE:   color = {0.3f, 0.5f, 0.3f}; break;
        case WeaponType::SNIPER:  color = {0.3f, 0.3f, 0.6f}; break;
        default:                  color = {0.5f, 0.5f, 0.5f}; break;
    }

    // Weapon as a small elongated cube floating, bobbing, and rotating
    float bob = sinf(time * 2.0f + pos.x * 0.5f + pos.z * 0.3f) * 0.15f;
    float rot = time * 1.5f + pos.x * 1.1f + pos.z * 0.7f;

    Mat4 model = Mat4::translate({pos.x, pos.y + 0.4f + bob, pos.z}) *
                 Mat4::rotateY(rot) *
                 Mat4::scale({0.15f, 0.15f, 0.5f});
    drawCube(model, color);

    // Small base platform
    Mat4 base = Mat4::translate({pos.x, pos.y + 0.02f, pos.z}) *
                Mat4::scale({0.4f, 0.04f, 0.4f});
    drawCube(base, {0.8f, 0.8f, 0.2f});
}

void Renderer::renderFirstPersonWeapon(WeaponType type, float fireCooldown, float time) {
    // Render weapon viewmodel in front of camera
    // We use a separate projection with smaller FOV to prevent clipping

    Mat4 savedProj = projectionMatrix_;
    Mat4 savedView = viewMatrix_;

    float aspect = (float)width_ / (float)height_;
    projectionMatrix_ = Mat4::perspective(55.0f * PI / 180.0f, aspect, 0.01f, 10.0f);
    viewMatrix_ = Mat4::identity(); // View-space rendering

    // Weapon bob
    float bobX = sinf(time * 5.0f) * 0.02f;
    float bobY = sinf(time * 10.0f) * 0.01f;

    // Recoil kick
    float recoil = 0;
    if (fireCooldown > 0) {
        const auto& def = getWeaponDef(type);
        float t = fireCooldown / def.fireRate;
        recoil = t * 0.05f;
    }

    Vec3 basePos = {0.3f + bobX, -0.25f + bobY - recoil * 0.5f, -0.5f + recoil};

    Vec3 weapColor;
    switch (type) {
        case WeaponType::PISTOL:  weapColor = {0.3f, 0.3f, 0.35f}; break;
        case WeaponType::SHOTGUN: weapColor = {0.55f, 0.35f, 0.2f}; break;
        case WeaponType::RIFLE:   weapColor = {0.25f, 0.3f, 0.25f}; break;
        case WeaponType::SNIPER:  weapColor = {0.2f, 0.2f, 0.3f}; break;
        default:                  weapColor = {0.4f, 0.4f, 0.4f}; break;
    }

    glClear(GL_DEPTH_BUFFER_BIT); // Clear depth so weapon renders on top

    switch (type) {
        case WeaponType::PISTOL: {
            // Handle
            Mat4 handle = Mat4::translate(basePos) * Mat4::scale({0.06f, 0.12f, 0.08f});
            drawCube(handle, weapColor);
            // Barrel
            Mat4 barrel = Mat4::translate(basePos + Vec3{0, 0.04f, -0.12f}) * Mat4::scale({0.04f, 0.04f, 0.18f});
            drawCube(barrel, weapColor * 0.8f);
            break;
        }
        case WeaponType::SHOTGUN: {
            // Stock
            Mat4 stock = Mat4::translate(basePos + Vec3{0, 0, 0.1f}) * Mat4::scale({0.06f, 0.08f, 0.2f});
            drawCube(stock, weapColor);
            // Barrel
            Mat4 barrel = Mat4::translate(basePos + Vec3{0, 0.02f, -0.2f}) * Mat4::scale({0.04f, 0.04f, 0.4f});
            drawCube(barrel, weapColor * 0.7f);
            // Pump
            Mat4 pump = Mat4::translate(basePos + Vec3{0, -0.03f, -0.1f}) * Mat4::scale({0.05f, 0.04f, 0.1f});
            drawCube(pump, {0.3f, 0.3f, 0.35f});
            break;
        }
        case WeaponType::RIFLE: {
            // Body
            Mat4 body = Mat4::translate(basePos) * Mat4::scale({0.06f, 0.1f, 0.15f});
            drawCube(body, weapColor);
            // Barrel
            Mat4 barrel = Mat4::translate(basePos + Vec3{0, 0.02f, -0.25f}) * Mat4::scale({0.03f, 0.03f, 0.35f});
            drawCube(barrel, weapColor * 0.7f);
            // Magazine
            Mat4 mag = Mat4::translate(basePos + Vec3{0, -0.08f, -0.02f}) * Mat4::scale({0.04f, 0.1f, 0.04f});
            drawCube(mag, {0.25f, 0.25f, 0.25f});
            // Stock
            Mat4 stock = Mat4::translate(basePos + Vec3{0, 0, 0.15f}) * Mat4::scale({0.05f, 0.07f, 0.15f});
            drawCube(stock, weapColor * 0.9f);
            break;
        }
        case WeaponType::SNIPER: {
            // Body
            Mat4 body = Mat4::translate(basePos) * Mat4::scale({0.05f, 0.08f, 0.12f});
            drawCube(body, weapColor);
            // Long barrel
            Mat4 barrel = Mat4::translate(basePos + Vec3{0, 0.02f, -0.35f}) * Mat4::scale({0.025f, 0.025f, 0.5f});
            drawCube(barrel, weapColor * 0.7f);
            // Scope
            Mat4 scope = Mat4::translate(basePos + Vec3{0, 0.06f, -0.1f}) * Mat4::scale({0.03f, 0.03f, 0.12f});
            drawCylinder(scope, {0.15f, 0.15f, 0.15f});
            // Stock
            Mat4 stock = Mat4::translate(basePos + Vec3{0, -0.01f, 0.14f}) * Mat4::scale({0.04f, 0.06f, 0.18f});
            drawCube(stock, weapColor * 0.9f);
            break;
        }
        default: break;
    }

    projectionMatrix_ = savedProj;
    viewMatrix_ = savedView;
}

// ============================================================================
// HUD Rendering
// ============================================================================

void Renderer::drawText(const char* text, float x, float y, float scale,
                        const Vec3& color, int screenW, int screenH) {
    glUseProgram(hudShader_);
    Mat4 proj = Mat4::ortho(0, (float)screenW, 0, (float)screenH, -1, 1);
    glUniformMatrix4fv(glGetUniformLocation(hudShader_, "uProj"), 1, GL_FALSE, proj.m);
    glUniform4f(glGetUniformLocation(hudShader_, "uColor"), color.x, color.y, color.z, 1.0f);
    glUniform1i(glGetUniformLocation(hudShader_, "uUseTexture"), 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTexture_);
    glUniform1i(glGetUniformLocation(hudShader_, "uTex"), 0);

    float charW = 8 * scale;
    float charH = 8 * scale;

    glBindVertexArray(quadVAO_);

    for (int i = 0; text[i]; i++) {
        int ch = text[i];
        if (ch < 32 || ch > 126) ch = '?';
        int idx = ch - 32;
        int col = idx % 16;
        int row = idx / 16;
        float u0 = col * 8.0f / 128.0f;
        float u1 = u0 + 8.0f / 128.0f;
        // OpenGL textures: row 0 of pixel data = v=0 (bottom). Our atlas has
        // glyph row 0 (top of char) at lower pixel rows. So lower v = glyph top.
        // Bottom of quad needs glyph bottom (higher v), top needs glyph top (lower v).
        float vBot = (row + 1) * 8.0f / 64.0f; // glyph bottom row
        float vTop = row * 8.0f / 64.0f;        // glyph top row

        float qx = x + i * charW;
        float qy = y;

        // Update quad positions and UVs
        float quadData[] = {
            qx,          qy,          u0, vBot,
            qx + charW,  qy,          u1, vBot,
            qx + charW,  qy + charH,  u1, vTop,
            qx,          qy,          u0, vBot,
            qx + charW,  qy + charH,  u1, vTop,
            qx,          qy + charH,  u0, vTop,
        };
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quadData), quadData);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
}

void Renderer::drawRect(float x, float y, float w, float h,
                        const Vec3& color, float alpha, int screenW, int screenH) {
    glUseProgram(hudShader_);
    Mat4 proj = Mat4::ortho(0, (float)screenW, 0, (float)screenH, -1, 1);
    glUniformMatrix4fv(glGetUniformLocation(hudShader_, "uProj"), 1, GL_FALSE, proj.m);
    glUniform4f(glGetUniformLocation(hudShader_, "uColor"), color.x, color.y, color.z, alpha);
    glUniform1i(glGetUniformLocation(hudShader_, "uUseTexture"), 0);

    float quadData[] = {
        x,     y,     0, 0,
        x + w, y,     1, 0,
        x + w, y + h, 1, 1,
        x,     y,     0, 0,
        x + w, y + h, 1, 1,
        x,     y + h, 0, 1,
    };

    glBindVertexArray(quadVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quadData), quadData);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void Renderer::renderCrosshair(int screenW, int screenH, bool hitMarker) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float cx = screenW * 0.5f;
    float cy = screenH * 0.5f;

    if (hitMarker) {
        // Hit marker: red X
        float size = 12;
        float thick = 2.5f;
        Vec3 red = {1.0f, 0.2f, 0.2f};
        // Diagonal lines forming an X
        for (float t = -size; t < size; t += 1.0f) {
            drawRect(cx + t - thick/2, cy + t - thick/2, thick, thick, red, 0.9f, screenW, screenH);
            drawRect(cx + t - thick/2, cy - t - thick/2, thick, thick, red, 0.9f, screenW, screenH);
        }
    } else {
        // Normal white crosshair with gap
        float size = 10;
        float thick = 2;
        Vec3 white = {1, 1, 1};
        drawRect(cx - size, cy - thick/2, size - 3, thick, white, 0.8f, screenW, screenH);
        drawRect(cx + 3, cy - thick/2, size - 3, thick, white, 0.8f, screenW, screenH);
        drawRect(cx - thick/2, cy - size, thick, size - 3, white, 0.8f, screenW, screenH);
        drawRect(cx - thick/2, cy + 3, thick, size - 3, white, 0.8f, screenW, screenH);
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderMuzzleFlash(int screenW, int screenH, float timer) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float cx = screenW * 0.5f;
    float cy = screenH * 0.5f;

    // Bright flash near center-bottom (where weapon barrel is)
    float alpha = timer / 0.06f; // Fade out
    float flashSize = 30 + (1.0f - alpha) * 20;
    drawRect(cx - flashSize/2 + 40, cy - flashSize/2 - 60,
             flashSize, flashSize, {1.0f, 0.9f, 0.5f}, alpha * 0.7f, screenW, screenH);
    // Inner bright core
    float coreSize = flashSize * 0.4f;
    drawRect(cx - coreSize/2 + 40, cy - coreSize/2 - 60,
             coreSize, coreSize, {1.0f, 1.0f, 0.8f}, alpha * 0.9f, screenW, screenH);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderDamageFlash(int screenW, int screenH, float timer) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float alpha = (timer / 0.3f) * 0.35f;
    drawRect(0, 0, (float)screenW, (float)screenH, {0.8f, 0.0f, 0.0f}, alpha, screenW, screenH);

    // Vignette edges (darker red on borders)
    float edge = 60;
    drawRect(0, 0, edge, (float)screenH, {0.6f, 0.0f, 0.0f}, alpha * 1.5f, screenW, screenH);
    drawRect((float)screenW - edge, 0, edge, (float)screenH, {0.6f, 0.0f, 0.0f}, alpha * 1.5f, screenW, screenH);
    drawRect(0, 0, (float)screenW, edge, {0.6f, 0.0f, 0.0f}, alpha * 1.5f, screenW, screenH);
    drawRect(0, (float)screenH - edge, (float)screenW, edge, {0.6f, 0.0f, 0.0f}, alpha * 1.5f, screenW, screenH);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderHUD(int health, int ammo, WeaponType weapon, int screenW, int screenH) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float scale = 2.5f;
    float padding = 20;

    // Health bar background
    float barW = 200, barH = 20;
    float barX = padding, barY = padding;
    drawRect(barX, barY, barW, barH, {0.2f, 0.2f, 0.2f}, 0.6f, screenW, screenH);

    // Health bar fill
    float healthFrac = std::clamp((float)health / MAX_HEALTH, 0.0f, 1.0f);
    Vec3 healthColor = healthFrac > 0.5f ? Vec3{0.2f, 0.8f, 0.3f} :
                       healthFrac > 0.25f ? Vec3{0.9f, 0.7f, 0.1f} :
                       Vec3{0.9f, 0.2f, 0.1f};
    drawRect(barX, barY, barW * healthFrac, barH, healthColor, 0.85f, screenW, screenH);

    // Health text
    char buf[64];
    snprintf(buf, sizeof(buf), "HP: %d", health);
    drawText(buf, barX + 5, barY + 3, scale, {1, 1, 1}, screenW, screenH);

    // Ammo
    const auto& def = getWeaponDef(weapon);
    snprintf(buf, sizeof(buf), "%s  %d/%d", def.name, ammo, def.magSize);
    drawText(buf, padding, barY + barH + 10, scale, {1, 1, 1}, screenW, screenH);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderMenu(int screenW, int screenH, int selectedField,
                          const char* ipBuf, const char* portBuf,
                          const char* statusMsg, bool connecting) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float cx = screenW * 0.5f;
    float cy = screenH * 0.5f;

    // Background
    drawRect(0, 0, (float)screenW, (float)screenH, {0.1f, 0.12f, 0.18f}, 1.0f, screenW, screenH);

    // Title
    float titleScale = 5.0f;
    drawText("ARCTIC ASSAULT", cx - 280, cy + 180, titleScale, {0.9f, 0.92f, 1.0f}, screenW, screenH);

    float fieldW = 300, fieldH = 35;
    float labelScale = 2.5f;
    float textScale = 2.5f;

    // Server IP field
    float ipY = cy + 60;
    drawText("Server IP:", cx - 150, ipY + 40, labelScale, {0.7f, 0.7f, 0.8f}, screenW, screenH);
    Vec3 ipFieldColor = selectedField == 0 ? Vec3{0.3f, 0.35f, 0.5f} : Vec3{0.2f, 0.22f, 0.3f};
    drawRect(cx - 150, ipY, fieldW, fieldH, ipFieldColor, 0.9f, screenW, screenH);
    drawText(ipBuf, cx - 145, ipY + 8, textScale, {1, 1, 1}, screenW, screenH);

    // Port field
    float portY = cy - 20;
    drawText("Port:", cx - 150, portY + 40, labelScale, {0.7f, 0.7f, 0.8f}, screenW, screenH);
    Vec3 portFieldColor = selectedField == 1 ? Vec3{0.3f, 0.35f, 0.5f} : Vec3{0.2f, 0.22f, 0.3f};
    drawRect(cx - 150, portY, fieldW, fieldH, portFieldColor, 0.9f, screenW, screenH);
    drawText(portBuf, cx - 145, portY + 8, textScale, {1, 1, 1}, screenW, screenH);

    // Connect button
    float btnY = cy - 100;
    Vec3 btnColor = selectedField == 2 ? Vec3{0.2f, 0.6f, 0.3f} : Vec3{0.15f, 0.4f, 0.2f};
    if (connecting) btnColor = {0.5f, 0.5f, 0.2f};
    drawRect(cx - 150, btnY, fieldW, fieldH + 5, btnColor, 0.95f, screenW, screenH);
    drawText(connecting ? "CONNECTING..." : "CONNECT", cx - 130, btnY + 10, 3.0f, {1, 1, 1}, screenW, screenH);

    // Quit button
    float quitY = cy - 160;
    Vec3 quitColor = selectedField == 3 ? Vec3{0.6f, 0.2f, 0.2f} : Vec3{0.4f, 0.15f, 0.15f};
    drawRect(cx - 150, quitY, fieldW, fieldH + 5, quitColor, 0.95f, screenW, screenH);
    drawText("QUIT", cx - 100, quitY + 10, 3.0f, {1, 1, 1}, screenW, screenH);

    // Status message
    if (statusMsg && statusMsg[0]) {
        drawText(statusMsg, cx - 150, cy - 220, 2.0f, {1.0f, 0.4f, 0.3f}, screenW, screenH);
    }

    // Instructions
    drawText("Click fields to edit. Tab to switch. Enter to connect.", 20, 20, 1.8f, {0.5f, 0.5f, 0.6f}, screenW, screenH);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderDeathScreen(float timer, int screenW, int screenH) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    drawRect(0, 0, (float)screenW, (float)screenH, {0.5f, 0.0f, 0.0f}, 0.3f, screenW, screenH);

    char buf[64];
    snprintf(buf, sizeof(buf), "YOU DIED - Respawning in %.1f", timer);
    drawText(buf, screenW * 0.5f - 200, screenH * 0.5f, 3.0f, {1, 0.3f, 0.3f}, screenW, screenH);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderScoreboard(const PlayerData players[], int numPlayers,
                                int localId, int screenW, int screenH) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float w = 400, h = 30 * numPlayers + 60;
    float x = (screenW - w) * 0.5f;
    float y = (screenH - h) * 0.5f;

    drawRect(x, y, w, h, {0.1f, 0.1f, 0.15f}, 0.85f, screenW, screenH);
    drawText("SCOREBOARD", x + 120, y + h - 35, 3.0f, {1, 1, 1}, screenW, screenH);

    int row = 0;
    for (int i = 0; i < numPlayers; i++) {
        if (players[i].state == PlayerState::DISCONNECTED) continue;

        float ry = y + h - 65 - row * 30;
        Vec3 col = (i == localId) ? Vec3{1.0f, 1.0f, 0.5f} : Vec3{0.8f, 0.8f, 0.8f};

        char line[128];
        snprintf(line, sizeof(line), "%-16s  HP:%3d  %s",
                 players[i].name[0] ? players[i].name : (players[i].isBot ? "Bot" : "Player"),
                 players[i].health,
                 players[i].state == PlayerState::DEAD ? "[DEAD]" : "");
        drawText(line, x + 15, ry, 2.0f, col, screenW, screenH);
        row++;
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderKillFeed(const char* messages[], int count, int screenW, int screenH) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (int i = 0; i < count; i++) {
        float y = screenH - 40 - i * 25;
        drawText(messages[i], screenW - 400, y, 2.0f, {1, 0.9f, 0.5f}, screenW, screenH);
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::endFrame() {
    // Nothing needed - swap is done by GLFW
}

// ============================================================================
// Particle System
// ============================================================================

struct ParticleVertex {
    float x, y, z;    // position
    float r, g, b, a; // color
    float size;        // point size
};

void Renderer::buildParticleMesh() {
    glGenVertexArrays(1, &particleVAO_);
    glGenBuffers(1, &particleVBO_);
    glBindVertexArray(particleVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO_);
    // Allocate buffer for max particles
    glBufferData(GL_ARRAY_BUFFER, MAX_PARTICLES * sizeof(ParticleVertex), nullptr, GL_DYNAMIC_DRAW);
    // position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex), (void*)0);
    glEnableVertexAttribArray(0);
    // color (rgba)
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // size
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex), (void*)(7 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

void Renderer::updateParticles(float dt) {
    for (auto it = particles_.begin(); it != particles_.end();) {
        it->life -= dt;
        if (it->life <= 0) {
            it = particles_.erase(it);
            continue;
        }

        // Physics
        it->velocity.y -= it->gravity * dt;
        it->position += it->velocity * dt;

        // Ground collision for non-snow particles
        if (it->position.y < 0.01f && it->type != ParticleType::SNOW) {
            it->position.y = 0.01f;
            it->velocity.y *= -0.3f;
            it->velocity.x *= 0.8f;
            it->velocity.z *= 0.8f;
        }

        // Snow wraps when hitting ground
        if (it->type == ParticleType::SNOW && it->position.y < 0) {
            it->life = 0; // Remove
        }

        ++it;
    }
}

void Renderer::renderParticles() {
    if (particles_.empty()) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE); // Don't write to depth buffer

    std::vector<ParticleVertex> verts;
    verts.reserve(particles_.size());

    for (const auto& p : particles_) {
        float lifeFrac = p.life / p.maxLife;
        float alpha = lifeFrac;

        // Snow fades differently
        if (p.type == ParticleType::SNOW) {
            alpha = 0.7f;
        } else if (p.type == ParticleType::MUZZLE_SPARK) {
            alpha = lifeFrac * 2.0f; // Brighter
        }

        verts.push_back({
            p.position.x, p.position.y, p.position.z,
            p.color.x, p.color.y, p.color.z, std::min(alpha, 1.0f),
            p.size
        });
    }

    Mat4 vp = projectionMatrix_ * viewMatrix_;
    glUseProgram(particleShader_);
    glUniformMatrix4fv(glGetUniformLocation(particleShader_, "uVP"), 1, GL_FALSE, vp.m);

    glBindVertexArray(particleVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(ParticleVertex), verts.data());
    glDrawArrays(GL_POINTS, 0, (int)verts.size());

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Renderer::spawnSnow(const Vec3& cameraPos) {
    snowSpawnAccum_ += 1.0f; // Spawn rate per frame call
    int toSpawn = (int)snowSpawnAccum_;
    snowSpawnAccum_ -= toSpawn;

    float radius = 50.0f;
    for (int i = 0; i < toSpawn && (int)particles_.size() < MAX_PARTICLES; i++) {
        Particle p;
        p.type = ParticleType::SNOW;
        p.position = {
            cameraPos.x + pRandf(-radius, radius),
            cameraPos.y + pRandf(5.0f, 30.0f),
            cameraPos.z + pRandf(-radius, radius)
        };
        p.velocity = {pRandf(-0.5f, 0.5f), pRandf(-2.0f, -0.8f), pRandf(-0.5f, 0.5f)};
        p.color = {pRandf(0.9f, 1.0f), pRandf(0.9f, 1.0f), 1.0f};
        p.life = pRandf(8.0f, 15.0f);
        p.maxLife = p.life;
        p.size = pRandf(0.02f, 0.06f);
        p.gravity = 0.0f; // Snow doesn't accelerate
        particles_.push_back(p);
    }
}

void Renderer::spawnBulletImpact(const Vec3& pos, const Vec3& normal) {
    int count = 8;
    for (int i = 0; i < count && (int)particles_.size() < MAX_PARTICLES; i++) {
        Particle p;
        p.type = ParticleType::BULLET_IMPACT;
        p.position = pos + normal * 0.05f;
        // Sparks fly out in hemisphere around normal
        Vec3 vel = {pRandf(-3, 3), pRandf(-3, 3), pRandf(-3, 3)};
        if (vel.dot(normal) < 0) vel = vel - normal * 2.0f * vel.dot(normal);
        p.velocity = vel * pRandf(0.5f, 2.0f);
        p.color = {pRandf(0.8f, 1.0f), pRandf(0.6f, 0.9f), pRandf(0.2f, 0.5f)};
        p.life = pRandf(0.2f, 0.6f);
        p.maxLife = p.life;
        p.size = pRandf(0.03f, 0.08f);
        p.gravity = 8.0f;
        particles_.push_back(p);
    }
    // Dust cloud
    for (int i = 0; i < 4 && (int)particles_.size() < MAX_PARTICLES; i++) {
        Particle p;
        p.type = ParticleType::BULLET_IMPACT;
        p.position = pos;
        p.velocity = normal * pRandf(0.5f, 1.5f) + Vec3{pRandf(-0.5f, 0.5f), pRandf(0, 1), pRandf(-0.5f, 0.5f)};
        p.color = {0.7f, 0.7f, 0.7f};
        p.life = pRandf(0.3f, 0.8f);
        p.maxLife = p.life;
        p.size = pRandf(0.08f, 0.15f);
        p.gravity = 1.0f;
        particles_.push_back(p);
    }
}

void Renderer::spawnBloodSplatter(const Vec3& pos) {
    for (int i = 0; i < 12 && (int)particles_.size() < MAX_PARTICLES; i++) {
        Particle p;
        p.type = ParticleType::BLOOD;
        p.position = pos + Vec3{0, PLAYER_HEIGHT * 0.5f, 0};
        p.velocity = {pRandf(-3, 3), pRandf(0, 4), pRandf(-3, 3)};
        p.color = {pRandf(0.5f, 0.8f), pRandf(0.0f, 0.1f), pRandf(0.0f, 0.05f)};
        p.life = pRandf(0.3f, 1.0f);
        p.maxLife = p.life;
        p.size = pRandf(0.04f, 0.1f);
        p.gravity = 10.0f;
        particles_.push_back(p);
    }
}

void Renderer::spawnMuzzleSpark(const Vec3& pos, const Vec3& dir) {
    for (int i = 0; i < 6 && (int)particles_.size() < MAX_PARTICLES; i++) {
        Particle p;
        p.type = ParticleType::MUZZLE_SPARK;
        p.position = pos;
        p.velocity = dir * pRandf(5, 15) + Vec3{pRandf(-2, 2), pRandf(-1, 2), pRandf(-2, 2)};
        p.color = {1.0f, pRandf(0.7f, 1.0f), pRandf(0.2f, 0.5f)};
        p.life = pRandf(0.05f, 0.15f);
        p.maxLife = p.life;
        p.size = pRandf(0.02f, 0.05f);
        p.gravity = 3.0f;
        particles_.push_back(p);
    }
}

void Renderer::spawnFootprintDust(const Vec3& pos) {
    for (int i = 0; i < 3 && (int)particles_.size() < MAX_PARTICLES; i++) {
        Particle p;
        p.type = ParticleType::FOOTPRINT_DUST;
        p.position = pos + Vec3{pRandf(-0.2f, 0.2f), 0.05f, pRandf(-0.2f, 0.2f)};
        p.velocity = {pRandf(-0.3f, 0.3f), pRandf(0.2f, 0.8f), pRandf(-0.3f, 0.3f)};
        p.color = {0.85f, 0.87f, 0.9f}; // Snow-white dust
        p.life = pRandf(0.3f, 0.7f);
        p.maxLife = p.life;
        p.size = pRandf(0.05f, 0.12f);
        p.gravity = 1.0f;
        particles_.push_back(p);
    }
}

// ============================================================================
// Footprints
// ============================================================================

void Renderer::addFootprint(const Vec3& pos, float yaw, bool isLeft) {
    if ((int)footprints_.size() >= MAX_FOOTPRINTS) {
        footprints_.erase(footprints_.begin());
    }
    footprints_.push_back({pos, yaw, 20.0f, isLeft}); // 20 second lifetime
}

void Renderer::updateFootprints(float dt) {
    for (auto it = footprints_.begin(); it != footprints_.end();) {
        it->life -= dt;
        if (it->life <= 0) {
            it = footprints_.erase(it);
        } else {
            ++it;
        }
    }
}

void Renderer::renderFootprints() {
    if (footprints_.empty()) return;

    glDisable(GL_CULL_FACE); // Footprints are flat on ground

    for (const auto& fp : footprints_) {
        float alpha = std::min(fp.life / 5.0f, 1.0f); // Fade in last 5 seconds
        Vec3 color = {0.75f * alpha, 0.77f * alpha, 0.82f * alpha}; // Slightly darker than snow

        float sideOff = fp.isLeft ? -0.15f : 0.15f;
        float offX = sinf(fp.yaw + PI * 0.5f) * sideOff;
        float offZ = cosf(fp.yaw + PI * 0.5f) * sideOff;

        Mat4 model = Mat4::translate({fp.position.x + offX, fp.position.y + 0.01f, fp.position.z + offZ}) *
                     Mat4::rotateY(-fp.yaw) *
                     Mat4::scale({0.12f, 0.01f, 0.25f});
        drawCube(model, color);
    }

    glEnable(GL_CULL_FACE);
}
