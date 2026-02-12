#pragma once

#include "common.h"
#include <vector>

// ============================================================================
// Map Structures
// ============================================================================

struct MapBlock {
    AABB  bounds;
    Vec3  color;
    bool  isFloor = false;
};

struct WeaponPickup {
    uint16_t   id = 0;
    WeaponType type = WeaponType::NONE;
    Vec3       position;
    bool       active = true;
    float      respawnTimer = 0;
};

struct SpawnPoint {
    Vec3  position;
    float yaw = 0;
};

// Waypoint for bot navigation
struct Waypoint {
    Vec3 position;
    std::vector<int> neighbors; // indices into waypoint array
};

// ============================================================================
// GameMap
// ============================================================================

class GameMap {
public:
    void buildArcticMap();

    const std::vector<MapBlock>&    blocks() const { return blocks_; }
    const std::vector<SpawnPoint>&  spawns() const { return spawns_; }
    std::vector<WeaponPickup>&      weaponPickups() { return pickups_; }
    const std::vector<WeaponPickup>& weaponPickups() const { return pickups_; }
    const std::vector<Waypoint>&    waypoints() const { return waypoints_; }
    std::vector<Waypoint>&          waypoints() { return waypoints_; }

    // Find nearest waypoint to a position
    int findNearestWaypoint(const Vec3& pos) const;
    // Check if there's an obstacle at waist height in front of a position
    bool hasObstacleAhead(const Vec3& pos, float yaw, float checkDist, float& obstacleHeight) const;

    // Collision
    bool isOnGround(const Vec3& pos, float radius, float height) const;
    Vec3 resolveCollision(const Vec3& oldPos, const Vec3& newPos, float radius, float height) const;

    // Raycasting
    bool raycast(const Vec3& origin, const Vec3& dir, float maxDist,
                 Vec3& hitPoint, float& hitDist) const;

    // Raycast against player AABBs (returns player index or -1)
    static int raycastPlayers(const Vec3& origin, const Vec3& dir, float maxDist,
                              const PlayerData players[], int numPlayers,
                              int ignorePlayer, float& hitDist);

private:
    std::vector<MapBlock>      blocks_;
    std::vector<SpawnPoint>    spawns_;
    std::vector<WeaponPickup>  pickups_;
    std::vector<Waypoint>      waypoints_;

    // Helpers for map building
    void addBlock(const Vec3& min, const Vec3& max, const Vec3& color, bool isFloor = false);
    void addWall(float x1, float z1, float x2, float z2, float height, float baseY, const Vec3& color);
    void addBuilding(float x, float z, float w, float d, float h, int stories, const Vec3& wallColor, const Vec3& floorColor);
};

// ============================================================================
// Player Physics
// ============================================================================

void tickPlayer(PlayerData& p, const InputState& input, const GameMap& map, float dt);
