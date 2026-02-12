#include "game.h"

// ============================================================================
// Map Building Helpers
// ============================================================================

void GameMap::addBlock(const Vec3& min, const Vec3& max, const Vec3& color, bool isFloor) {
    blocks_.push_back({AABB{min, max}, color, isFloor});
}

void GameMap::addWall(float x1, float z1, float x2, float z2, float height, float baseY, const Vec3& color) {
    float minX = std::min(x1, x2), maxX = std::max(x1, x2);
    float minZ = std::min(z1, z2), maxZ = std::max(z1, z2);
    // Ensure wall has some thickness
    if (maxX - minX < 0.2f) maxX = minX + 0.2f;
    if (maxZ - minZ < 0.2f) maxZ = minZ + 0.2f;
    addBlock({minX, baseY, minZ}, {maxX, baseY + height, maxZ}, color);
}

void GameMap::addBuilding(float x, float z, float w, float d, float h,
                          int stories, const Vec3& wallColor, const Vec3& floorColor) {
    float storyH = h / stories;
    float doorW = 1.5f;
    float doorH = 2.2f;

    for (int s = 0; s < stories; s++) {
        float baseY = s * storyH;

        // Floor
        addBlock({x, baseY, z}, {x + w, baseY + 0.1f, z + d}, floorColor, true);

        // Front wall (with door opening on ground floor)
        if (s == 0) {
            // Left of door
            addWall(x, z, x + w * 0.35f, z, storyH, baseY, wallColor);
            // Right of door
            addWall(x + w * 0.35f + doorW, z, x + w, z, storyH, baseY, wallColor);
            // Above door
            addWall(x + w * 0.35f, z, x + w * 0.35f + doorW, z, storyH - doorH, baseY + doorH, wallColor);
        } else {
            addWall(x, z, x + w, z, storyH, baseY, wallColor);
        }

        // Back wall
        addWall(x, z + d, x + w, z + d, storyH, baseY, wallColor);

        // Left wall
        addWall(x, z, x, z + d, storyH, baseY, wallColor);

        // Right wall (with opening for corridor connections)
        if (s == 0) {
            addWall(x + w, z, x + w, z + d * 0.3f, storyH, baseY, wallColor);
            addWall(x + w, z + d * 0.3f + doorW, x + w, z + d, storyH, baseY, wallColor);
            addWall(x + w, z + d * 0.3f, x + w, z + d * 0.3f + doorW, storyH - doorH, baseY + doorH, wallColor);
        } else {
            addWall(x + w, z, x + w, z + d, storyH, baseY, wallColor);
        }
    }

    // Roof
    addBlock({x, stories * storyH - 0.1f, z}, {x + w, (float)stories * storyH, z + d}, wallColor * 0.8f, true);
}

// ============================================================================
// Arctic Map Layout
// ============================================================================

void GameMap::buildArcticMap() {
    blocks_.clear();
    spawns_.clear();
    pickups_.clear();
    waypoints_.clear();

    // Colors
    Vec3 snow      = {0.92f, 0.93f, 0.96f};
    Vec3 snowDark  = {0.80f, 0.82f, 0.88f};
    Vec3 concrete  = {0.55f, 0.55f, 0.53f};
    Vec3 concDark  = {0.40f, 0.40f, 0.38f};
    Vec3 wood      = {0.55f, 0.38f, 0.22f};
    Vec3 woodDark  = {0.42f, 0.28f, 0.15f};
    Vec3 metal     = {0.50f, 0.52f, 0.55f};
    Vec3 crate     = {0.50f, 0.40f, 0.25f};
    Vec3 red       = {0.65f, 0.20f, 0.15f};

    // --- Ground plane (large snow field) ---
    addBlock({-60, -0.5f, -60}, {60, 0.0f, 60}, snow, true);

    // === BUILDING A (Northwest, 2-story) ===
    addBuilding(-25, -25, 12, 10, 6, 2, concrete, concDark);
    // Interior stairs (stepped blocks)
    for (int i = 0; i < 6; i++) {
        float step = i * 0.5f;
        addBlock({-24.5f, step, -24.5f}, {-23.0f, step + 0.5f, -23.5f}, wood);
    }

    // === BUILDING B (Northeast, 1-story) ===
    addBuilding(10, -25, 14, 10, 3.5f, 1, concrete, concDark);
    // Interior crates for cover
    addBlock({12, 0, -23}, {13.5f, 1.2f, -22}, crate);
    addBlock({18, 0, -20}, {19.5f, 1.0f, -19}, crate);

    // === BUILDING C (Southwest, 1-story) ===
    addBuilding(-25, 15, 12, 10, 3.5f, 1, wood, woodDark);
    // Some crates inside
    addBlock({-22, 0, 18}, {-20.5f, 1.2f, 19.5f}, crate);

    // === BUILDING D (Southeast, 2-story) ===
    addBuilding(10, 15, 14, 10, 6, 2, concrete, concDark);
    // Stairs
    for (int i = 0; i < 6; i++) {
        float step = i * 0.5f;
        addBlock({22.5f, step, 16.0f}, {24.0f, step + 0.5f, 17.0f}, wood);
    }

    // === CORRIDORS connecting buildings ===
    // Corridor A-B (north side)
    addBlock({-13, 0, -22}, {10, 0.1f, -18}, concDark, true);  // floor
    addWall(-13, -22, 10, -22, 3.0f, 0, concrete);              // north wall
    addWall(-13, -18, 10, -18, 3.0f, 0, concrete);              // south wall
    addBlock({-13, 2.9f, -22}, {10, 3.0f, -18}, concDark, true); // roof

    // Corridor C-D (south side)
    addBlock({-13, 0, 18}, {10, 0.1f, 22}, concDark, true);
    addWall(-13, 18, 10, 18, 3.0f, 0, concrete);
    addWall(-13, 22, 10, 22, 3.0f, 0, concrete);
    addBlock({-13, 2.9f, 18}, {10, 3.0f, 22}, concDark, true);

    // Central corridor (N-S connecting the two corridors)
    addBlock({-3, 0, -18}, {3, 0.1f, 18}, concDark, true);
    addWall(-3, -18, -3, 18, 3.0f, 0, concrete);
    addWall(3, -18, 3, 18, 3.0f, 0, concrete);
    addBlock({-3, 2.9f, -18}, {3, 3.0f, 18}, concDark, true);

    // === MID PLAZA (open area in center) ===
    addBlock({-8, 0, -5}, {8, 0.05f, 5}, snowDark, true);

    // Cover crates in mid plaza
    addBlock({-6, 0, -1}, {-4.5f, 1.5f, 1}, crate);
    addBlock({4.5f, 0, -1}, {6, 1.5f, 1}, crate);
    addBlock({-1, 0, 3}, {1, 1.0f, 4.5f}, crate);
    addBlock({-1, 0, -4.5f}, {1, 1.0f, -3}, crate);

    // === SNIPER TOWER A (Northwest corner) ===
    float tAx = -35, tAz = -35;
    // Base pillars
    addBlock({tAx, 0, tAz}, {tAx + 0.5f, 5, tAz + 0.5f}, metal);
    addBlock({tAx + 4.5f, 0, tAz}, {tAx + 5, 5, tAz + 0.5f}, metal);
    addBlock({tAx, 0, tAz + 4.5f}, {tAx + 0.5f, 5, tAz + 5}, metal);
    addBlock({tAx + 4.5f, 0, tAz + 4.5f}, {tAx + 5, 5, tAz + 5}, metal);
    // Platform
    addBlock({tAx, 5, tAz}, {tAx + 5, 5.2f, tAz + 5}, metal, true);
    // Railings
    addBlock({tAx, 5.2f, tAz}, {tAx + 5, 6.0f, tAz + 0.15f}, metal);
    addBlock({tAx, 5.2f, tAz + 4.85f}, {tAx + 5, 6.0f, tAz + 5}, metal);
    addBlock({tAx, 5.2f, tAz}, {tAx + 0.15f, 6.0f, tAz + 5}, metal);
    // Open side (for shooting)
    // Ladder (stepped blocks)
    for (int i = 0; i < 10; i++) {
        float step = i * 0.5f;
        addBlock({tAx + 4.6f, step, tAz + 2.0f}, {tAx + 5.4f, step + 0.3f, tAz + 3.0f}, metal);
    }

    // === SNIPER TOWER B (Southeast corner) ===
    float tBx = 30, tBz = 30;
    addBlock({tBx, 0, tBz}, {tBx + 0.5f, 5, tBz + 0.5f}, metal);
    addBlock({tBx + 4.5f, 0, tBz}, {tBx + 5, 5, tBz + 0.5f}, metal);
    addBlock({tBx, 0, tBz + 4.5f}, {tBx + 0.5f, 5, tBz + 5}, metal);
    addBlock({tBx + 4.5f, 0, tBz + 4.5f}, {tBx + 5, 5, tBz + 5}, metal);
    addBlock({tBx, 5, tBz}, {tBx + 5, 5.2f, tBz + 5}, metal, true);
    addBlock({tBx, 5.2f, tBz}, {tBx + 5, 6.0f, tBz + 0.15f}, metal);
    addBlock({tBx, 5.2f, tBz + 4.85f}, {tBx + 5, 6.0f, tBz + 5}, metal);
    addBlock({tBx + 4.85f, 5.2f, tBz}, {tBx + 5, 6.0f, tBz + 5}, metal);
    for (int i = 0; i < 10; i++) {
        float step = i * 0.5f;
        addBlock({tBx - 0.4f, step, tBz + 2.0f}, {tBx + 0.4f, step + 0.3f, tBz + 3.0f}, metal);
    }

    // === OUTER WALLS (boundary) ===
    float bnd = 55;
    addWall(-bnd, -bnd, bnd, -bnd, 4, 0, snowDark);
    addWall(-bnd, bnd, bnd, bnd, 4, 0, snowDark);
    addWall(-bnd, -bnd, -bnd, bnd, 4, 0, snowDark);
    addWall(bnd, -bnd, bnd, bnd, 4, 0, snowDark);

    // === SCATTERED COVER (snow mounds, barrels, crates) ===
    // Snow mounds (large flat blocks)
    addBlock({-40, 0, 5}, {-37, 0.8f, 8}, snow);
    addBlock({35, 0, -10}, {38, 0.7f, -7}, snow);
    addBlock({-15, 0, 35}, {-12, 0.6f, 38}, snow);
    addBlock({15, 0, -35}, {18, 0.9f, -32}, snow);

    // Barrel clusters
    addBlock({-30, 0, 0}, {-29, 1.2f, 1}, red);
    addBlock({-29.5f, 0, 1.2f}, {-28.5f, 1.2f, 2.2f}, red);
    addBlock({28, 0, -2}, {29, 1.2f, -1}, red);

    // Extra crates near buildings
    addBlock({-28, 0, -15}, {-26.5f, 1.5f, -13.5f}, crate);
    addBlock({25, 0, 12}, {26.5f, 1.5f, 13.5f}, crate);

    // === SPAWN POINTS ===
    spawns_.push_back({{-20, 0.1f, -20}, 0.8f});
    spawns_.push_back({{-20, 0.1f, -15}, 0.5f});
    spawns_.push_back({{15, 0.1f, -20}, -0.8f});
    spawns_.push_back({{20, 0.1f, -20}, -0.5f});
    spawns_.push_back({{-20, 0.1f, 20}, 2.3f});
    spawns_.push_back({{-20, 0.1f, 17}, 2.0f});
    spawns_.push_back({{15, 0.1f, 20}, -2.3f});
    spawns_.push_back({{20, 0.1f, 20}, -2.0f});
    spawns_.push_back({{0, 0.1f, 0}, 0.0f});
    spawns_.push_back({{5, 0.1f, -5}, 1.5f});

    // === WEAPON PICKUPS ===
    uint16_t pickId = 0;
    auto addPickup = [&](WeaponType t, float x, float y, float z) {
        pickups_.push_back({pickId++, t, {x, y, z}, true, 0});
    };

    addPickup(WeaponType::SHOTGUN, 0, 0.5f, 0);           // Mid plaza center
    addPickup(WeaponType::RIFLE, -20, 0.5f, -20);          // Building A entrance
    addPickup(WeaponType::RIFLE, 15, 0.5f, 20);            // Building D entrance
    addPickup(WeaponType::SNIPER, -32.5f, 5.7f, -32.5f);   // Sniper tower A top
    addPickup(WeaponType::SNIPER, 32.5f, 5.7f, 32.5f);     // Sniper tower B top
    addPickup(WeaponType::SHOTGUN, -5, 0.5f, 20);           // South corridor
    addPickup(WeaponType::RIFLE, 5, 0.5f, -20);             // North corridor
    addPickup(WeaponType::SHOTGUN, 15, 0.5f, -20);          // Building B interior

    // === WAYPOINTS for bot navigation ===
    auto addWP = [&](float x, float y, float z) -> int {
        int idx = (int)waypoints_.size();
        waypoints_.push_back({{x, y, z}, {}});
        return idx;
    };
    auto link = [&](int a, int b) {
        waypoints_[a].neighbors.push_back(b);
        waypoints_[b].neighbors.push_back(a);
    };

    // Key navigation points
    int wpMid     = addWP(0, 0.1f, 0);
    int wpMidN    = addWP(0, 0.1f, -10);
    int wpMidS    = addWP(0, 0.1f, 10);
    int wpMidW    = addWP(-8, 0.1f, 0);
    int wpMidE    = addWP(8, 0.1f, 0);

    int wpCorrNA  = addWP(-10, 0.1f, -20);
    int wpCorrNB  = addWP(8, 0.1f, -20);
    int wpCorrNM  = addWP(0, 0.1f, -20);

    int wpCorrSC  = addWP(-10, 0.1f, 20);
    int wpCorrSD  = addWP(8, 0.1f, 20);
    int wpCorrSM  = addWP(0, 0.1f, 20);

    int wpBldA    = addWP(-19, 0.1f, -20);
    int wpBldB    = addWP(17, 0.1f, -20);
    int wpBldC    = addWP(-19, 0.1f, 20);
    int wpBldD    = addWP(17, 0.1f, 20);

    int wpTowerA  = addWP(-32.5f, 5.3f, -32.5f);
    int wpTowerAb = addWP(-30, 0.1f, -32.5f);
    int wpTowerB  = addWP(32.5f, 5.3f, 32.5f);
    int wpTowerBb = addWP(35, 0.1f, 32.5f);

    int wpOutNW   = addWP(-40, 0.1f, -30);
    int wpOutNE   = addWP(40, 0.1f, -30);
    int wpOutSW   = addWP(-40, 0.1f, 30);
    int wpOutSE   = addWP(40, 0.1f, 30);

    // Connect waypoints
    link(wpMid, wpMidN); link(wpMid, wpMidS);
    link(wpMid, wpMidW); link(wpMid, wpMidE);

    link(wpMidN, wpCorrNM); link(wpMidS, wpCorrSM);
    link(wpCorrNA, wpCorrNM); link(wpCorrNM, wpCorrNB);
    link(wpCorrSC, wpCorrSM); link(wpCorrSM, wpCorrSD);

    link(wpCorrNA, wpBldA); link(wpCorrNB, wpBldB);
    link(wpCorrSC, wpBldC); link(wpCorrSD, wpBldD);

    link(wpTowerAb, wpTowerA); link(wpTowerAb, wpOutNW);
    link(wpTowerBb, wpTowerB); link(wpTowerBb, wpOutSE);

    link(wpBldA, wpOutNW); link(wpBldB, wpOutNE);
    link(wpBldC, wpOutSW); link(wpBldD, wpOutSE);

    link(wpOutNW, wpMidW); link(wpOutNE, wpMidE);
    link(wpOutSW, wpMidW); link(wpOutSE, wpMidE);

    link(wpMidW, wpCorrNA); link(wpMidW, wpCorrSC);
    link(wpMidE, wpCorrNB); link(wpMidE, wpCorrSD);

    // === ELEVATED WAYPOINTS (for jumping and multi-level navigation) ===
    // Building A second floor (stairs go up at -24.5, -24.5 to -23, -23.5)
    int wpBldA2f = addWP(-20, 3.1f, -20);
    link(wpBldA, wpBldA2f);  // Connected via stairs

    // Building D second floor (stairs go up at 22.5, 16 to 24, 17)
    int wpBldD2f = addWP(17, 3.1f, 20);
    link(wpBldD, wpBldD2f);  // Connected via stairs

    // Crate-hop waypoints (mid plaza crates at y=1.0-1.5)
    int wpCrateW = addWP(-5.25f, 1.6f, 0);
    int wpCrateE = addWP(5.25f, 1.6f, 0);
    link(wpMidW, wpCrateW);  // Jump up from ground
    link(wpMidE, wpCrateE);  // Jump up from ground
    link(wpCrateW, wpMid);
    link(wpCrateE, wpMid);

    // Waypoints near stairs for smooth navigation
    int wpStairsA = addWP(-23.5f, 0.1f, -24);
    link(wpBldA, wpStairsA);
    link(wpStairsA, wpBldA2f);

    int wpStairsD = addWP(23.0f, 0.1f, 16.5f);
    link(wpBldD, wpStairsD);
    link(wpStairsD, wpBldD2f);

    // Additional outdoor waypoints for better coverage
    int wpNW = addWP(-35, 0.1f, -15);
    int wpNE = addWP(35, 0.1f, -15);
    int wpSW = addWP(-35, 0.1f, 15);
    int wpSE = addWP(35, 0.1f, 15);
    link(wpNW, wpOutNW); link(wpNW, wpMidW); link(wpNW, wpBldA);
    link(wpNE, wpOutNE); link(wpNE, wpMidE); link(wpNE, wpBldB);
    link(wpSW, wpOutSW); link(wpSW, wpMidW); link(wpSW, wpBldC);
    link(wpSE, wpOutSE); link(wpSE, wpMidE); link(wpSE, wpBldD);

    // Connect tower base to nearby buildings
    link(wpTowerAb, wpBldA);
    link(wpTowerBb, wpBldD);
}

// ============================================================================
// Waypoint Queries
// ============================================================================

int GameMap::findNearestWaypoint(const Vec3& pos) const {
    int best = 0;
    float bestDist = 1e30f;
    for (int i = 0; i < (int)waypoints_.size(); i++) {
        float d = (waypoints_[i].position - pos).lengthSq();
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

bool GameMap::hasObstacleAhead(const Vec3& pos, float yaw, float checkDist, float& obstacleHeight) const {
    Vec3 forward = {sinf(yaw), 0, cosf(yaw)};
    // Check at knee height (0.3) and waist height (0.8)
    for (float h = 0.3f; h <= 1.2f; h += 0.3f) {
        Vec3 probe = pos + Vec3{0, h, 0};
        Vec3 hitPt;
        float hitDist;
        if (raycast(probe, forward, checkDist, hitPt, hitDist)) {
            // Found obstacle - check how tall it is
            // Probe upward from the hit point to find the top
            Vec3 upProbe = hitPt + Vec3{-forward.x * 0.1f, 0, -forward.z * 0.1f};
            for (float testH = h; testH <= 3.0f; testH += 0.2f) {
                Vec3 testPos = {upProbe.x, pos.y + testH, upProbe.z};
                AABB testBox = {{testPos.x - 0.1f, testPos.y - 0.05f, testPos.z - 0.1f},
                                {testPos.x + 0.1f, testPos.y + 0.05f, testPos.z + 0.1f}};
                bool blocked = false;
                for (const auto& b : blocks_) {
                    if (testBox.intersects(b.bounds)) { blocked = true; break; }
                }
                if (!blocked) {
                    obstacleHeight = testH - pos.y;
                    return true;
                }
            }
            obstacleHeight = 3.0f; // Very tall obstacle, can't jump over
            return true;
        }
    }
    return false;
}

// ============================================================================
// Collision Detection
// ============================================================================

bool GameMap::isOnGround(const Vec3& pos, float radius, float height) const {
    (void)height;
    AABB feet;
    feet.min = {pos.x - radius, pos.y - 0.05f, pos.z - radius};
    feet.max = {pos.x + radius, pos.y + 0.05f, pos.z + radius};

    for (const auto& b : blocks_) {
        if (feet.intersects(b.bounds)) return true;
    }
    return pos.y <= 0.05f; // Ground plane at y=0
}

Vec3 GameMap::resolveCollision(const Vec3& oldPos, const Vec3& newPos, float radius, float height) const {
    Vec3 resolved = newPos;

    AABB playerBox;
    playerBox.min = {resolved.x - radius, resolved.y, resolved.z - radius};
    playerBox.max = {resolved.x + radius, resolved.y + height, resolved.z + radius};

    for (int iter = 0; iter < 4; iter++) {
        bool collided = false;

        playerBox.min = {resolved.x - radius, resolved.y, resolved.z - radius};
        playerBox.max = {resolved.x + radius, resolved.y + height, resolved.z + radius};

        for (const auto& b : blocks_) {
            if (!playerBox.intersects(b.bounds)) continue;

            // Compute overlap on each axis
            float overlapX1 = playerBox.max.x - b.bounds.min.x;
            float overlapX2 = b.bounds.max.x - playerBox.min.x;
            float overlapY1 = playerBox.max.y - b.bounds.min.y;
            float overlapY2 = b.bounds.max.y - playerBox.min.y;
            float overlapZ1 = playerBox.max.z - b.bounds.min.z;
            float overlapZ2 = b.bounds.max.z - playerBox.min.z;

            float minOverlapX = std::min(overlapX1, overlapX2);
            float minOverlapY = std::min(overlapY1, overlapY2);
            float minOverlapZ = std::min(overlapZ1, overlapZ2);

            if (minOverlapX < minOverlapY && minOverlapX < minOverlapZ) {
                resolved.x += (overlapX1 < overlapX2) ? -overlapX1 : overlapX2;
            } else if (minOverlapY < minOverlapZ) {
                if (overlapY1 < overlapY2) {
                    resolved.y -= overlapY1;
                } else {
                    resolved.y += overlapY2;
                }
            } else {
                resolved.z += (overlapZ1 < overlapZ2) ? -overlapZ1 : overlapZ2;
            }
            collided = true;

            // Update player box
            playerBox.min = {resolved.x - radius, resolved.y, resolved.z - radius};
            playerBox.max = {resolved.x + radius, resolved.y + height, resolved.z + radius};
        }
        if (!collided) break;
    }

    // World boundary
    float bnd = 54.5f;
    resolved.x = std::clamp(resolved.x, -bnd, bnd);
    resolved.z = std::clamp(resolved.z, -bnd, bnd);
    if (resolved.y < 0) resolved.y = 0;

    return resolved;
}

// ============================================================================
// Raycasting
// ============================================================================

bool GameMap::raycast(const Vec3& origin, const Vec3& dir, float maxDist,
                      Vec3& hitPoint, float& hitDist) const {
    float closest = maxDist;
    bool hit = false;

    for (const auto& b : blocks_) {
        float t;
        if (b.bounds.raycast(origin, dir, t) && t < closest && t >= 0) {
            closest = t;
            hit = true;
        }
    }

    if (hit) {
        hitDist = closest;
        hitPoint = origin + dir * closest;
    }
    return hit;
}

int GameMap::raycastPlayers(const Vec3& origin, const Vec3& dir, float maxDist,
                            const PlayerData players[], int numPlayers,
                            int ignorePlayer, float& hitDist) {
    float closest = maxDist;
    int hitIdx = -1;

    for (int i = 0; i < numPlayers; i++) {
        if (i == ignorePlayer) continue;
        if (players[i].state != PlayerState::ALIVE) continue;

        AABB playerBox;
        playerBox.min = {players[i].position.x - PLAYER_RADIUS,
                         players[i].position.y,
                         players[i].position.z - PLAYER_RADIUS};
        playerBox.max = {players[i].position.x + PLAYER_RADIUS,
                         players[i].position.y + PLAYER_HEIGHT,
                         players[i].position.z + PLAYER_RADIUS};

        float t;
        if (playerBox.raycast(origin, dir, t) && t < closest && t >= 0) {
            closest = t;
            hitIdx = i;
        }
    }

    if (hitIdx >= 0) hitDist = closest;
    return hitIdx;
}

// ============================================================================
// Player Physics
// ============================================================================

void tickPlayer(PlayerData& p, const InputState& input, const GameMap& map, float dt) {
    if (p.state != PlayerState::ALIVE) return;

    p.yaw = input.yaw;
    p.pitch = input.pitch;

    // Clamp pitch
    if (p.pitch > PI * 0.49f) p.pitch = PI * 0.49f;
    if (p.pitch < -PI * 0.49f) p.pitch = -PI * 0.49f;

    // Movement direction
    Vec3 forward = {sinf(p.yaw), 0, cosf(p.yaw)};
    Vec3 right   = {-cosf(p.yaw), 0, sinf(p.yaw)};

    float fwd = 0, side = 0;
    if (input.keys & InputState::KEY_W) fwd += 1;
    if (input.keys & InputState::KEY_S) fwd -= 1;
    if (input.keys & InputState::KEY_A) side -= 1;
    if (input.keys & InputState::KEY_D) side += 1;

    Vec3 wishDir = forward * fwd + right * side;
    if (wishDir.lengthSq() > 0.01f) wishDir = wishDir.normalize();

    bool onGround = map.isOnGround(p.position, PLAYER_RADIUS, PLAYER_HEIGHT);

    if (onGround) {
        p.velocity.x = wishDir.x * PLAYER_SPEED;
        p.velocity.z = wishDir.z * PLAYER_SPEED;
        if (input.keys & InputState::KEY_JUMP) {
            p.velocity.y = JUMP_VELOCITY;
        }
    } else {
        // Air control (limited)
        p.velocity.x += wishDir.x * PLAYER_SPEED * 0.05f * dt * 60.0f;
        p.velocity.z += wishDir.z * PLAYER_SPEED * 0.05f * dt * 60.0f;
    }

    // Gravity
    p.velocity.y -= GRAVITY * dt;

    // Move
    Vec3 newPos = p.position + p.velocity * dt;
    Vec3 resolved = map.resolveCollision(p.position, newPos, PLAYER_RADIUS, PLAYER_HEIGHT);

    // Stop velocity on collision axes
    if (fabsf(resolved.x - newPos.x) > 0.001f) p.velocity.x = 0;
    if (fabsf(resolved.y - newPos.y) > 0.001f) p.velocity.y = 0;
    if (fabsf(resolved.z - newPos.z) > 0.001f) p.velocity.z = 0;

    p.position = resolved;

    // Fire cooldown
    if (p.fireCooldown > 0) p.fireCooldown -= dt;
}
