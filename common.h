#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

// ============================================================================
// Math Primitives
// ============================================================================

struct Vec2 {
    float x = 0, y = 0;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vec3 operator-() const { return {-x, -y, -z}; }

    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    float length() const { return sqrtf(x * x + y * y + z * z); }
    float lengthSq() const { return x * x + y * y + z * z; }
    Vec3 normalize() const {
        float l = length();
        return l > 1e-6f ? Vec3{x / l, y / l, z / l} : Vec3{0, 0, 0};
    }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

// Column-major 4x4 matrix (OpenGL convention)
struct Mat4 {
    float m[16] = {};

    static Mat4 identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static Mat4 perspective(float fovY, float aspect, float near, float far) {
        Mat4 r;
        float tanHalf = tanf(fovY * 0.5f);
        r.m[0]  = 1.0f / (aspect * tanHalf);
        r.m[5]  = 1.0f / tanHalf;
        r.m[10] = -(far + near) / (far - near);
        r.m[11] = -1.0f;
        r.m[14] = -(2.0f * far * near) / (far - near);
        return r;
    }

    static Mat4 ortho(float left, float right, float bottom, float top, float near, float far) {
        Mat4 r;
        r.m[0]  = 2.0f / (right - left);
        r.m[5]  = 2.0f / (top - bottom);
        r.m[10] = -2.0f / (far - near);
        r.m[12] = -(right + left) / (right - left);
        r.m[13] = -(top + bottom) / (top - bottom);
        r.m[14] = -(far + near) / (far - near);
        r.m[15] = 1.0f;
        return r;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 f = (target - eye).normalize();
        Vec3 s = f.cross(up).normalize();
        Vec3 u = s.cross(f);
        Mat4 r = identity();
        r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;
        r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -s.dot(eye);
        r.m[13] = -u.dot(eye);
        r.m[14] = f.dot(eye);
        return r;
    }

    static Mat4 translate(const Vec3& v) {
        Mat4 r = identity();
        r.m[12] = v.x; r.m[13] = v.y; r.m[14] = v.z;
        return r;
    }

    static Mat4 scale(const Vec3& s) {
        Mat4 r;
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; r.m[15] = 1.0f;
        return r;
    }

    static Mat4 rotateY(float angle) {
        Mat4 r = identity();
        float c = cosf(angle), s = sinf(angle);
        r.m[0] = c;  r.m[8] = s;
        r.m[2] = -s; r.m[10] = c;
        return r;
    }

    static Mat4 rotateX(float angle) {
        Mat4 r = identity();
        float c = cosf(angle), s = sinf(angle);
        r.m[5] = c;  r.m[9] = -s;
        r.m[6] = s;  r.m[10] = c;
        return r;
    }

    static Mat4 rotateZ(float angle) {
        Mat4 r = identity();
        float c = cosf(angle), s = sinf(angle);
        r.m[0] = c;  r.m[4] = -s;
        r.m[1] = s;  r.m[5] = c;
        return r;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; c++)
            for (int row = 0; row < 4; row++) {
                float sum = 0;
                for (int k = 0; k < 4; k++)
                    sum += m[k * 4 + row] * o.m[c * 4 + k];
                r.m[c * 4 + row] = sum;
            }
        return r;
    }
};

// ============================================================================
// AABB (Axis-Aligned Bounding Box)
// ============================================================================

struct AABB {
    Vec3 min, max;

    bool contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x &&
               p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }

    bool intersects(const AABB& o) const {
        return min.x <= o.max.x && max.x >= o.min.x &&
               min.y <= o.max.y && max.y >= o.min.y &&
               min.z <= o.max.z && max.z >= o.min.z;
    }

    // Ray-AABB intersection, returns true if hit, sets tMin to entry distance
    bool raycast(const Vec3& origin, const Vec3& dir, float& tMin) const {
        float t1, t2;
        float tNear = -1e30f, tFar = 1e30f;

        for (int i = 0; i < 3; i++) {
            float o_i = (&origin.x)[i];
            float d_i = (&dir.x)[i];
            float mn = (&min.x)[i];
            float mx = (&max.x)[i];

            if (fabsf(d_i) < 1e-8f) {
                if (o_i < mn || o_i > mx) return false;
            } else {
                t1 = (mn - o_i) / d_i;
                t2 = (mx - o_i) / d_i;
                if (t1 > t2) std::swap(t1, t2);
                if (t1 > tNear) tNear = t1;
                if (t2 < tFar) tFar = t2;
                if (tNear > tFar || tFar < 0) return false;
            }
        }
        tMin = tNear;
        return tMin >= 0;
    }

    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 size() const { return max - min; }
};

// ============================================================================
// Game Constants
// ============================================================================

constexpr int    MAX_PLAYERS      = 128;
constexpr int    TICK_RATE        = 64;
constexpr float  TICK_DURATION    = 1.0f / TICK_RATE;
constexpr int    DEFAULT_PORT     = 27015;
constexpr float  GRAVITY          = 20.0f;
constexpr float  PLAYER_SPEED     = 7.0f;
constexpr float  JUMP_VELOCITY    = 8.0f;
constexpr float  MOUSE_SENS       = 0.002f;
constexpr float  PLAYER_HEIGHT    = 1.8f;
constexpr float  PLAYER_RADIUS    = 0.4f;
constexpr float  PLAYER_EYE_HEIGHT= 1.6f;
constexpr float  RESPAWN_TIME     = 3.0f;
constexpr float  WEAPON_RESPAWN   = 15.0f;
constexpr int    MAX_HEALTH       = 100;
constexpr float  PI               = 3.14159265358979f;

// Vehicle constants
constexpr int    MAX_VEHICLES     = 20;
constexpr float  VEHICLE_ENTER_RANGE = 3.5f;

// ============================================================================
// Weapons
// ============================================================================

enum class WeaponType : uint8_t {
    NONE = 0, PISTOL, SHOTGUN, RIFLE, SNIPER, COUNT
};

struct WeaponDef {
    const char* name;
    int   damage;
    int   magSize;
    float fireRate;       // seconds between shots
    float spread;         // radians
    int   pelletsPerShot; // >1 for shotgun
    float range;
};

inline const WeaponDef& getWeaponDef(WeaponType t) {
    static const WeaponDef defs[] = {
        {"None",    0,   0, 0.0f,  0.0f,    0, 0.0f},
        {"Pistol",  25, 12, 0.3f,  0.015f,  1, 200.0f},
        {"Shotgun", 12,  8, 0.8f,  0.08f,   8, 30.0f},
        {"Rifle",   30, 30, 0.1f,  0.02f,   1, 300.0f},
        {"Sniper",  90,  5, 1.2f,  0.002f,  1, 500.0f},
    };
    return defs[static_cast<int>(t)];
}

// ============================================================================
// Player & Input
// ============================================================================

enum class PlayerState : uint8_t {
    DISCONNECTED = 0, ALIVE, DEAD, SPECTATING
};

struct InputState {
    uint16_t keys = 0;    // bitfield
    float    yaw = 0;
    float    pitch = 0;

    // Key bits
    static constexpr uint16_t KEY_W      = 0x01;
    static constexpr uint16_t KEY_A      = 0x02;
    static constexpr uint16_t KEY_S      = 0x04;
    static constexpr uint16_t KEY_D      = 0x08;
    static constexpr uint16_t KEY_JUMP   = 0x10;
    static constexpr uint16_t KEY_SHOOT  = 0x20;
    static constexpr uint16_t KEY_RELOAD = 0x40;
    static constexpr uint16_t KEY_USE    = 0x80;  // Enter/exit vehicle
};

struct PlayerData {
    Vec3        position = {0, 0, 0};
    float       yaw = 0, pitch = 0;
    Vec3        velocity = {0, 0, 0};
    int         health = MAX_HEALTH;
    WeaponType  currentWeapon = WeaponType::PISTOL;
    int         ammo = 12;
    PlayerState state = PlayerState::DISCONNECTED;
    uint8_t     teamId = 0;
    char        name[32] = {};
    float       respawnTimer = 0;
    float       fireCooldown = 0;
    bool        isBot = false;
    int16_t     vehicleId = -1;    // -1 = on foot, >=0 = in vehicle
    bool        isDriver = false;
};

// ============================================================================
// Vehicles
// ============================================================================

enum class VehicleType : uint8_t {
    JEEP = 0, TANK, COUNT
};

struct VehicleDef {
    const char* name;
    float speed;
    float turnRate;     // rad/s
    int   maxHealth;
    int   cannonDamage; // 0 = no cannon
    float cannonRate;   // seconds between shots
    float length, width, height;
};

inline const VehicleDef& getVehicleDef(VehicleType t) {
    static const VehicleDef defs[] = {
        {"Jeep",  22.0f, 2.5f, 250,   0,  0.0f, 3.5f, 2.0f, 1.8f},
        {"Tank",   9.0f, 1.2f, 1200, 80,  2.0f, 5.0f, 3.0f, 2.5f},
    };
    return defs[static_cast<int>(t)];
}

struct VehicleData {
    Vec3        position = {0, 0, 0};
    float       yaw = 0;
    float       turretYaw = 0;    // Relative to body (tank only)
    Vec3        velocity = {0, 0, 0};
    int         health = 0;
    VehicleType type = VehicleType::JEEP;
    int16_t     driverId = -1;
    bool        active = true;
    float       fireCooldown = 0;
    float       respawnTimer = 0;
    Vec3        spawnPos;
    float       spawnYaw = 0;
};
