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
    teamSpawns_[0].clear();
    teamSpawns_[1].clear();
    pickups_.clear();
    waypoints_.clear();
    vehicleSpawns_.clear();

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
    Vec3 asphalt   = {0.30f, 0.30f, 0.32f};
    Vec3 dirt      = {0.45f, 0.38f, 0.28f};


    // --- Ground plane (huge snow field, 400x400) ---
    addBlock({-200, -0.5f, -200}, {200, 0.0f, 200}, snow, true);

    // === ROADS (cross pattern) ===
    // Main road North-South
    addBlock({-4, 0.01f, -180}, {4, 0.06f, 180}, asphalt, true);
    // Main road East-West
    addBlock({-180, 0.01f, -4}, {180, 0.06f, 4}, asphalt, true);
    // Diagonal road NW-SE
    for (int i = -35; i < 35; i++) {
        float cx = i * 5.0f;
        float cz = i * 5.0f;
        addBlock({cx - 3, 0.01f, cz - 3}, {cx + 3, 0.05f, cz + 3}, asphalt, true);
    }

    // ================================================================
    // ZONE A: Military Base (Northwest, -140 to -80, -140 to -80)
    // ================================================================
    // Large warehouse
    addBuilding(-130, -130, 30, 15, 5, 1, concrete, concDark);
    // Barracks
    addBuilding(-130, -110, 20, 8, 3.5f, 1, concrete, concDark);
    addBuilding(-105, -110, 20, 8, 3.5f, 1, concrete, concDark);
    // Guard tower
    float gTx = -85, gTz = -135;
    addBlock({gTx, 0, gTz}, {gTx+0.4f, 6, gTz+0.4f}, metal);
    addBlock({gTx+4.6f, 0, gTz}, {gTx+5, 6, gTz+0.4f}, metal);
    addBlock({gTx, 0, gTz+4.6f}, {gTx+0.4f, 6, gTz+5}, metal);
    addBlock({gTx+4.6f, 0, gTz+4.6f}, {gTx+5, 6, gTz+5}, metal);
    addBlock({gTx, 6, gTz}, {gTx+5, 6.2f, gTz+5}, metal, true);
    addBlock({gTx, 6.2f, gTz}, {gTx+5, 7, gTz+0.15f}, metal);
    addBlock({gTx, 6.2f, gTz+4.85f}, {gTx+5, 7, gTz+5}, metal);
    addBlock({gTx, 6.2f, gTz}, {gTx+0.15f, 7, gTz+5}, metal);
    for (int i = 0; i < 12; i++)
        addBlock({gTx+4.6f, i*0.5f, gTz+2}, {gTx+5.4f, i*0.5f+0.3f, gTz+3}, metal);
    // Sandbag walls
    addBlock({-130, 0, -95}, {-85, 1.2f, -94}, dirt);
    addBlock({-130, 0, -135}, {-130, 1.2f, -94}, dirt);
    // Crates
    addBlock({-120, 0, -100}, {-118, 1.5f, -98}, crate);
    addBlock({-100, 0, -100}, {-98, 1.5f, -98}, crate);

    // ================================================================
    // ZONE B: Village (Northeast, 80 to 140, -140 to -80)
    // ================================================================
    // Houses
    addBuilding(85, -130, 10, 8, 3.5f, 1, wood, woodDark);
    addBuilding(100, -130, 10, 8, 3.5f, 1, wood, woodDark);
    addBuilding(115, -130, 10, 8, 3.5f, 1, wood, woodDark);
    addBuilding(85, -115, 10, 8, 6, 2, wood, woodDark);
    addBuilding(100, -115, 12, 10, 3.5f, 1, concrete, concDark);
    addBuilding(118, -115, 10, 8, 3.5f, 1, wood, woodDark);
    // Church-like tall building
    addBuilding(100, -100, 14, 10, 8, 2, concrete, concDark);
    // Stairs for 2-story buildings
    for (int i = 0; i < 6; i++) {
        addBlock({86, i*0.5f, -114.5f}, {87.5f, i*0.5f+0.5f, -113.5f}, wood);
        addBlock({101, i*0.5f+4, -99.5f}, {102.5f, i*0.5f+4.5f, -98.5f}, wood);
    }
    // Fences
    addBlock({80, 0, -85}, {135, 0.8f, -84.5f}, wood);
    // Market stalls
    addBlock({90, 0, -90}, {95, 2.5f, -88}, wood);
    addBlock({105, 0, -90}, {110, 2.5f, -88}, wood);
    addBlock({120, 0, -90}, {125, 2.5f, -88}, wood);

    // ================================================================
    // ZONE C: Industrial (Southwest, -140 to -80, 80 to 140)
    // ================================================================
    // Factories
    addBuilding(-135, 85, 25, 18, 6, 1, metal, concDark);
    addBuilding(-105, 85, 20, 18, 5, 1, metal, concDark);
    // Smokestacks (tall cylinders as blocks)
    addBlock({-128, 0, 108}, {-126, 12, 110}, metal);
    addBlock({-120, 0, 108}, {-118, 10, 110}, metal);
    // Pipe racks
    addBlock({-135, 3, 106}, {-105, 3.5f, 107}, metal);
    addBlock({-135, 5, 106}, {-105, 5.5f, 107}, metal);
    // Loading dock
    addBlock({-100, 0, 108}, {-85, 1.2f, 120}, concDark, true);
    // Container yard
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 4; col++) {
            Vec3 c = (row + col) % 2 == 0 ? red : Vec3{0.2f, 0.4f, 0.6f};
            float bx = -135 + col * 8;
            float bz = 120 + row * 5;
            addBlock({bx, 0, bz}, {bx + 6, 2.8f, bz + 3}, c);
        }
    }

    // ================================================================
    // ZONE D: Airfield (Southeast, 80 to 180, 80 to 150)
    // ================================================================
    // Runway
    addBlock({60, 0.02f, 95}, {180, 0.07f, 105}, asphalt, true);
    // Hangar 1
    addBuilding(85, 110, 25, 20, 7, 1, metal, concDark);
    // Hangar 2
    addBuilding(120, 110, 25, 20, 7, 1, metal, concDark);
    // Control tower
    addBlock({155, 0, 110}, {160, 8, 115}, concrete);
    addBlock({154, 8, 109}, {161, 8.3f, 116}, concrete, true);
    addBlock({154, 8.3f, 109}, {161, 9.5f, 109.3f}, concrete);
    addBlock({154, 8.3f, 115.7f}, {161, 9.5f, 116}, concrete);
    addBlock({154, 8.3f, 109}, {154.3f, 9.5f, 116}, concrete);
    addBlock({160.7f, 8.3f, 109}, {161, 9.5f, 116}, concrete);
    for (int i = 0; i < 16; i++)
        addBlock({159.6f, i*0.5f, 112}, {160.4f, i*0.5f+0.3f, 113}, metal);
    // Fuel tanks
    addBlock({170, 0, 115}, {174, 3, 119}, metal);
    addBlock({170, 0, 122}, {174, 3, 126}, metal);
    // Parked plane (decorative)
    addBlock({100, 0, 98}, {108, 1.5f, 102}, metal); // fuselage
    addBlock({102, 0, 93}, {106, 0.4f, 107}, metal); // wings

    // ================================================================
    // ZONE E: Central Town (around origin, -40 to 40)
    // ================================================================
    // Town hall (large 2-story)
    addBuilding(-15, -15, 30, 20, 7, 2, concrete, concDark);
    for (int i = 0; i < 7; i++) {
        addBlock({13, i*0.5f, -14}, {14.5f, i*0.5f+0.5f, -13}, wood);
    }
    // Market buildings
    addBuilding(-35, -10, 12, 8, 3.5f, 1, wood, woodDark);
    addBuilding(-35, 5, 12, 8, 3.5f, 1, wood, woodDark);
    addBuilding(25, -10, 12, 8, 3.5f, 1, concrete, concDark);
    addBuilding(25, 5, 12, 10, 6, 2, concrete, concDark);
    for (int i = 0; i < 6; i++)
        addBlock({26, i*0.5f, 6}, {27.5f, i*0.5f+0.5f, 7}, wood);
    // Central plaza
    addBlock({-10, 0.01f, -8}, {10, 0.06f, 8}, concDark, true);
    // Fountain (decorative center)
    addBlock({-2, 0, -2}, {2, 0.5f, 2}, concrete);
    addBlock({-1, 0.5f, -1}, {1, 1.2f, 1}, concrete);
    // Cover around plaza
    addBlock({-8, 0, -6}, {-6.5f, 1.5f, -4.5f}, crate);
    addBlock({6.5f, 0, 4.5f}, {8, 1.5f, 6}, crate);
    addBlock({-8, 0, 4.5f}, {-6.5f, 1.5f, 6}, crate);
    addBlock({6.5f, 0, -6}, {8, 1.5f, -4.5f}, crate);

    // ================================================================
    // ZONE F: Farm (North, -40 to 40, -100 to -60)
    // ================================================================
    // Barn
    addBuilding(-10, -95, 20, 12, 5, 1, red, woodDark);
    // Farmhouse
    addBuilding(15, -80, 12, 10, 6, 2, wood, woodDark);
    for (int i = 0; i < 6; i++)
        addBlock({25.5f, i*0.5f, -79}, {27, i*0.5f+0.5f, -78}, wood);
    // Hay bales
    for (int i = 0; i < 5; i++) {
        addBlock({-30 + i*6.0f, 0, -70}, {-28 + i*6.0f, 1.2f, -68}, Vec3{0.7f, 0.65f, 0.3f});
    }
    // Fenced area
    addBlock({-35, 0, -100}, {40, 0.6f, -99.7f}, wood);
    addBlock({-35, 0, -60}, {40, 0.6f, -59.7f}, wood);
    addBlock({-35, 0, -100}, {-34.7f, 0.6f, -60}, wood);
    addBlock({39.7f, 0, -100}, {40, 0.6f, -60}, wood);

    // ================================================================
    // Additional scattered cover across the large map
    // ================================================================
    // Snow mounds
    addBlock({-80, 0, -50}, {-75, 1.5f, -45}, snow);
    addBlock({60, 0, -50}, {65, 1.2f, -45}, snow);
    addBlock({-60, 0, 50}, {-55, 1.0f, 55}, snow);
    addBlock({50, 0, 50}, {55, 1.3f, 55}, snow);
    addBlock({-50, 0, -150}, {-45, 2.0f, -145}, snow);
    addBlock({50, 0, 150}, {55, 1.8f, 155}, snow);

    // Rocky outcrops (hills)
    addBlock({-70, 0, 140}, {-55, 3.5f, 155}, snowDark);
    addBlock({-65, 3.5f, 142}, {-58, 5.0f, 152}, snowDark);
    addBlock({70, 0, -150}, {85, 4.0f, -140}, snowDark);
    addBlock({73, 4.0f, -148}, {82, 6.0f, -142}, snowDark);
    addBlock({140, 0, -40}, {155, 3.0f, -25}, snowDark);
    addBlock({-160, 0, 30}, {-145, 2.5f, 45}, snowDark);

    // Barrel/crate clusters along roads
    addBlock({6, 0, -50}, {8, 1.2f, -48}, red);
    addBlock({-8, 0, 50}, {-6, 1.2f, 52}, crate);
    addBlock({50, 0, 6}, {52, 1.5f, 8}, crate);
    addBlock({-50, 0, -6}, {-48, 1.2f, -4}, red);

    // Checkpoints along roads
    addBlock({-3, 0, -60}, {3, 2.5f, -59}, concrete);
    addBlock({-3, 0, 60}, {3, 2.5f, 61}, concrete);
    addBlock({-60, 0, -3}, {-59, 2.5f, 3}, concrete);
    addBlock({60, 0, -3}, {61, 2.5f, 3}, concrete);

    // Small bunkers
    addBlock({-50, 0, -30}, {-44, 0.1f, -24}, concDark, true);
    addWall(-50, -30, -44, -30, 2.5f, 0, concrete);
    addWall(-50, -24, -44, -24, 2.5f, 0, concrete);
    addWall(-50, -30, -50, -24, 2.5f, 0, concrete);
    addBlock({-50, 2.4f, -30}, {-44, 2.5f, -24}, concrete, true);

    addBlock({44, 0, 24}, {50, 0.1f, 30}, concDark, true);
    addWall(44, 24, 50, 24, 2.5f, 0, concrete);
    addWall(44, 30, 50, 30, 2.5f, 0, concrete);
    addWall(50, 24, 50, 30, 2.5f, 0, concrete);
    addBlock({44, 2.4f, 24}, {50, 2.5f, 30}, concrete, true);

    // === OUTER WALLS (boundary, much bigger) ===
    float bnd = 195;
    addWall(-bnd, -bnd, bnd, -bnd, 5, 0, snowDark);
    addWall(-bnd, bnd, bnd, bnd, 5, 0, snowDark);
    addWall(-bnd, -bnd, -bnd, bnd, 5, 0, snowDark);
    addWall(bnd, -bnd, bnd, bnd, 5, 0, snowDark);

    // === SPAWN POINTS (many, spread across map) ===
    // Zone A spawns
    spawns_.push_back({{-120, 0.1f, -120}, 0.8f});
    spawns_.push_back({{-100, 0.1f, -115}, 0.5f});
    spawns_.push_back({{-115, 0.1f, -100}, 0.3f});
    spawns_.push_back({{-90, 0.1f, -130}, 1.0f});
    // Zone B spawns
    spawns_.push_back({{90, 0.1f, -125}, -0.8f});
    spawns_.push_back({{110, 0.1f, -110}, -0.5f});
    spawns_.push_back({{120, 0.1f, -125}, -1.0f});
    spawns_.push_back({{95, 0.1f, -95}, -0.3f});
    // Zone C spawns
    spawns_.push_back({{-120, 0.1f, 90}, 2.3f});
    spawns_.push_back({{-100, 0.1f, 100}, 2.0f});
    spawns_.push_back({{-110, 0.1f, 120}, 1.8f});
    spawns_.push_back({{-90, 0.1f, 95}, 2.5f});
    // Zone D spawns
    spawns_.push_back({{100, 0.1f, 100}, -2.3f});
    spawns_.push_back({{130, 0.1f, 115}, -2.0f});
    spawns_.push_back({{90, 0.1f, 115}, -1.8f});
    spawns_.push_back({{150, 0.1f, 110}, -2.5f});
    // Zone E spawns (center)
    spawns_.push_back({{0, 0.1f, 0}, 0.0f});
    spawns_.push_back({{-10, 0.1f, -10}, 0.7f});
    spawns_.push_back({{10, 0.1f, 10}, -0.7f});
    spawns_.push_back({{-20, 0.1f, 5}, 1.2f});
    // Zone F spawns (farm)
    spawns_.push_back({{0, 0.1f, -80}, 0.0f});
    spawns_.push_back({{20, 0.1f, -75}, -0.5f});
    spawns_.push_back({{-10, 0.1f, -90}, 0.5f});
    // Outer spawns
    spawns_.push_back({{-150, 0.1f, 0}, 0.0f});
    spawns_.push_back({{150, 0.1f, 0}, PI});
    spawns_.push_back({{0, 0.1f, -150}, 0.0f});
    spawns_.push_back({{0, 0.1f, 150}, PI});

    // === WEAPON PICKUPS (many, spread across zones) ===
    uint16_t pickId = 0;
    auto addPickup = [&](WeaponType t, float x, float y, float z) {
        pickups_.push_back({pickId++, t, {x, y, z}, true, 0});
    };
    // Zone A
    addPickup(WeaponType::RIFLE, -120, 0.5f, -120);
    addPickup(WeaponType::SNIPER, -82.5f, 6.5f, -132.5f);
    addPickup(WeaponType::SHOTGUN, -110, 0.5f, -100);
    // Zone B
    addPickup(WeaponType::RIFLE, 110, 0.5f, -120);
    addPickup(WeaponType::SHOTGUN, 95, 0.5f, -90);
    addPickup(WeaponType::SNIPER, 105, 4.5f, -95);
    // Zone C
    addPickup(WeaponType::RIFLE, -120, 0.5f, 95);
    addPickup(WeaponType::SHOTGUN, -100, 0.5f, 115);
    // Zone D
    addPickup(WeaponType::SNIPER, 157, 8.5f, 112);
    addPickup(WeaponType::RIFLE, 100, 0.5f, 115);
    // Zone E (center)
    addPickup(WeaponType::SHOTGUN, 0, 0.5f, 0);
    addPickup(WeaponType::RIFLE, -30, 0.5f, 0);
    addPickup(WeaponType::RIFLE, 30, 0.5f, 0);
    addPickup(WeaponType::SNIPER, 0, 3.6f, -10);
    // Zone F (farm)
    addPickup(WeaponType::SHOTGUN, 0, 0.5f, -85);
    addPickup(WeaponType::RIFLE, 20, 0.5f, -75);
    // Road intersections
    addPickup(WeaponType::SHOTGUN, 0, 0.5f, -60);
    addPickup(WeaponType::RIFLE, 60, 0.5f, 0);
    addPickup(WeaponType::SHOTGUN, -60, 0.5f, 0);
    addPickup(WeaponType::RIFLE, 0, 0.5f, 60);

    // === VEHICLE SPAWNS ===
    // Zone A: military base - tanks
    vehicleSpawns_.push_back({{-115, 0.1f, -95}, 0, VehicleType::TANK});
    vehicleSpawns_.push_back({{-95, 0.1f, -95}, 0, VehicleType::TANK});
    // Zone A: jeeps
    vehicleSpawns_.push_back({{-130, 0.1f, -100}, PI*0.5f, VehicleType::JEEP});
    vehicleSpawns_.push_back({{-85, 0.1f, -100}, -PI*0.5f, VehicleType::JEEP});
    // Zone D: airfield vehicles
    vehicleSpawns_.push_back({{150, 0.1f, 100}, PI, VehicleType::TANK});
    vehicleSpawns_.push_back({{80, 0.1f, 100}, 0, VehicleType::JEEP});
    vehicleSpawns_.push_back({{160, 0.1f, 130}, PI, VehicleType::JEEP});
    // Zone E: center
    vehicleSpawns_.push_back({{-40, 0.1f, 0}, 0, VehicleType::JEEP});
    vehicleSpawns_.push_back({{40, 0.1f, 0}, PI, VehicleType::JEEP});
    // Road vehicles
    vehicleSpawns_.push_back({{0, 0.1f, -100}, 0, VehicleType::JEEP});
    vehicleSpawns_.push_back({{0, 0.1f, 100}, PI, VehicleType::JEEP});
    vehicleSpawns_.push_back({{-100, 0.1f, 0}, PI*0.5f, VehicleType::JEEP});
    vehicleSpawns_.push_back({{100, 0.1f, 0}, -PI*0.5f, VehicleType::JEEP});
    // Zone B/C
    vehicleSpawns_.push_back({{80, 0.1f, -100}, PI, VehicleType::TANK});
    vehicleSpawns_.push_back({{-80, 0.1f, 130}, 0, VehicleType::TANK});

    // Helicopters (airfield and military base)
    vehicleSpawns_.push_back({{140, 0.1f, 120}, 0, VehicleType::HELICOPTER});
    vehicleSpawns_.push_back({{160, 0.1f, 120}, 0, VehicleType::HELICOPTER});
    vehicleSpawns_.push_back({{-130, 0.1f, -120}, 0, VehicleType::HELICOPTER});
    // Planes (airfield runway)
    vehicleSpawns_.push_back({{120, 0.1f, 105}, PI*0.25f, VehicleType::PLANE});
    vehicleSpawns_.push_back({{170, 0.1f, 105}, PI*0.25f, VehicleType::PLANE});

    // === TEAM SPAWNS (west=team0/red, east=team1/blue) ===
    // Team 0 (Red) - west side spawns
    teamSpawns_[0].push_back({{-150, 0.1f, -20}, 0.0f});
    teamSpawns_[0].push_back({{-150, 0.1f, 0}, 0.0f});
    teamSpawns_[0].push_back({{-150, 0.1f, 20}, 0.0f});
    teamSpawns_[0].push_back({{-140, 0.1f, -10}, 0.2f});
    teamSpawns_[0].push_back({{-140, 0.1f, 10}, -0.2f});
    teamSpawns_[0].push_back({{-160, 0.1f, 0}, 0.0f});
    teamSpawns_[0].push_back({{-160, 0.1f, -15}, 0.3f});
    teamSpawns_[0].push_back({{-160, 0.1f, 15}, -0.3f});
    // Team 1 (Blue) - east side spawns
    teamSpawns_[1].push_back({{150, 0.1f, -20}, PI});
    teamSpawns_[1].push_back({{150, 0.1f, 0}, PI});
    teamSpawns_[1].push_back({{150, 0.1f, 20}, PI});
    teamSpawns_[1].push_back({{140, 0.1f, -10}, PI+0.2f});
    teamSpawns_[1].push_back({{140, 0.1f, 10}, PI-0.2f});
    teamSpawns_[1].push_back({{160, 0.1f, 0}, PI});
    teamSpawns_[1].push_back({{160, 0.1f, -15}, PI+0.3f});
    teamSpawns_[1].push_back({{160, 0.1f, 15}, PI-0.3f});

    // === FLAG BASE POSITIONS ===
    flagBasePos_[0] = {-170, 0.5f, 0}; // Red flag (west)
    flagBasePos_[1] = {170, 0.5f, 0};  // Blue flag (east)

    // Add flag base platforms
    addBlock({-173, 0.0f, -3}, {-167, 0.3f, 3}, red, true);   // Red base platform
    Vec3 blue = {0.15f, 0.20f, 0.65f};
    addBlock({167, 0.0f, -3}, {173, 0.3f, 3}, blue, true);    // Blue base platform

    // === WAYPOINTS for bot navigation (many more for big map) ===
    auto addWP = [&](float x, float y, float z) -> int {
        int idx = (int)waypoints_.size();
        waypoints_.push_back({{x, y, z}, {}});
        return idx;
    };
    auto link = [&](int a, int b) {
        waypoints_[a].neighbors.push_back(b);
        waypoints_[b].neighbors.push_back(a);
    };

    // Road grid waypoints (every ~40 units along roads)
    std::vector<int> nsRoad, ewRoad;
    for (int z = -160; z <= 160; z += 40) nsRoad.push_back(addWP(0, 0.1f, (float)z));
    for (int x = -160; x <= 160; x += 40) ewRoad.push_back(addWP((float)x, 0.1f, 0));
    for (int i = 1; i < (int)nsRoad.size(); i++) link(nsRoad[i-1], nsRoad[i]);
    for (int i = 1; i < (int)ewRoad.size(); i++) link(ewRoad[i-1], ewRoad[i]);
    // Cross-link at origin
    int wpOrigin = addWP(0, 0.1f, 0);
    for (int w : nsRoad) if (fabsf(waypoints_[w].position.z) < 1) link(wpOrigin, w);
    for (int w : ewRoad) if (fabsf(waypoints_[w].position.x) < 1) link(wpOrigin, w);

    // Zone A waypoints
    int wpA1 = addWP(-115, 0.1f, -120);
    int wpA2 = addWP(-100, 0.1f, -110);
    int wpA3 = addWP(-90, 0.1f, -95);
    int wpA4 = addWP(-120, 0.1f, -95);
    int wpA5 = addWP(-82, 6.3f, -132);  // Guard tower top
    int wpA6 = addWP(-82, 0.1f, -130);
    link(wpA1, wpA2); link(wpA2, wpA3); link(wpA3, wpA4); link(wpA4, wpA1);
    link(wpA5, wpA6); link(wpA6, wpA1);

    // Zone B waypoints
    int wpB1 = addWP(95, 0.1f, -125);
    int wpB2 = addWP(115, 0.1f, -115);
    int wpB3 = addWP(105, 0.1f, -95);
    int wpB4 = addWP(90, 0.1f, -90);
    link(wpB1, wpB2); link(wpB2, wpB3); link(wpB3, wpB4); link(wpB4, wpB1);

    // Zone C waypoints
    int wpC1 = addWP(-120, 0.1f, 90);
    int wpC2 = addWP(-100, 0.1f, 100);
    int wpC3 = addWP(-110, 0.1f, 125);
    int wpC4 = addWP(-90, 0.1f, 115);
    link(wpC1, wpC2); link(wpC2, wpC3); link(wpC3, wpC4); link(wpC4, wpC1);

    // Zone D waypoints
    int wpD1 = addWP(100, 0.1f, 100);
    int wpD2 = addWP(130, 0.1f, 115);
    int wpD3 = addWP(155, 0.1f, 112);
    int wpD4 = addWP(155, 8.4f, 112);  // Control tower top
    int wpD5 = addWP(90, 0.1f, 115);
    link(wpD1, wpD2); link(wpD2, wpD3); link(wpD3, wpD4);
    link(wpD1, wpD5); link(wpD5, wpD2);

    // Zone E waypoints (center)
    int wpE1 = addWP(-15, 0.1f, 0);
    int wpE2 = addWP(15, 0.1f, 0);
    int wpE3 = addWP(0, 0.1f, -15);
    int wpE4 = addWP(0, 0.1f, 15);
    int wpE5 = addWP(-30, 0.1f, -5);
    int wpE6 = addWP(30, 0.1f, 5);
    int wpE7 = addWP(0, 3.6f, -12);  // Town hall 2nd floor
    link(wpE1, wpE2); link(wpE1, wpE3); link(wpE1, wpE4);
    link(wpE2, wpE3); link(wpE2, wpE4);
    link(wpE3, wpE4); link(wpE5, wpE1); link(wpE6, wpE2);
    link(wpE3, wpE7);

    // Zone F waypoints (farm)
    int wpF1 = addWP(0, 0.1f, -85);
    int wpF2 = addWP(20, 0.1f, -75);
    int wpF3 = addWP(-10, 0.1f, -70);
    link(wpF1, wpF2); link(wpF2, wpF3); link(wpF3, wpF1);

    // Inter-zone connections along roads
    // Connect zones to road grid
    for (int w : nsRoad) {
        float wz = waypoints_[w].position.z;
        if (fabsf(wz - (-120)) < 41) { link(w, wpA3); }
        if (fabsf(wz - (-80)) < 41) { link(w, wpF1); }
        if (fabsf(wz - 100) < 41) { link(w, wpC2); }
    }
    for (int w : ewRoad) {
        float wx = waypoints_[w].position.x;
        if (fabsf(wx - (-120)) < 41) { link(w, wpA4); }
        if (fabsf(wx - 100) < 41) { link(w, wpB4); link(w, wpD1); }
    }

    // Connect center zone to road
    link(wpE1, wpOrigin); link(wpE2, wpOrigin);
    link(wpE3, wpOrigin); link(wpE4, wpOrigin);

    // Open field waypoints (for crossing between zones)
    int wpField1 = addWP(-60, 0.1f, -60);
    int wpField2 = addWP(60, 0.1f, -60);
    int wpField3 = addWP(-60, 0.1f, 60);
    int wpField4 = addWP(60, 0.1f, 60);
    link(wpField1, wpA3); link(wpField1, wpE5); link(wpField1, wpF3);
    link(wpField2, wpB4); link(wpField2, wpE6); link(wpField2, wpF2);
    link(wpField3, wpC1); link(wpField3, wpE5);
    link(wpField4, wpD1); link(wpField4, wpE6);
    link(wpField1, wpField2); link(wpField3, wpField4);
    link(wpField1, wpField3); link(wpField2, wpField4);
    link(wpField1, wpOrigin); link(wpField2, wpOrigin);
    link(wpField3, wpOrigin); link(wpField4, wpOrigin);

    // Bunker waypoints
    int wpBunkerW = addWP(-47, 0.1f, -27);
    int wpBunkerE = addWP(47, 0.1f, 27);
    link(wpBunkerW, wpField1); link(wpBunkerW, wpE5);
    link(wpBunkerE, wpField4); link(wpBunkerE, wpE6);
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
    float bnd = 194.5f;
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

    // Apply class speed multiplier
    float speed = PLAYER_SPEED * getClassDef(p.playerClass).speedMult;

    bool onGround = map.isOnGround(p.position, PLAYER_RADIUS, PLAYER_HEIGHT);

    if (onGround) {
        p.velocity.x = wishDir.x * speed;
        p.velocity.z = wishDir.z * speed;
        if (input.keys & InputState::KEY_JUMP) {
            p.velocity.y = JUMP_VELOCITY;
        }
    } else {
        // Air control (limited)
        p.velocity.x += wishDir.x * speed * 0.05f * dt * 60.0f;
        p.velocity.z += wishDir.z * speed * 0.05f * dt * 60.0f;
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
