#pragma once

#include "common.h"
#include <netinet/in.h>

// ============================================================================
// Packet Types
// ============================================================================

enum class ClientPacket : uint8_t {
    JOIN       = 1,
    INPUT      = 2,
    DISCONNECT = 3,
};

enum class ServerPacket : uint8_t {
    JOIN_ACK     = 1,
    SNAPSHOT     = 2,
    PLAYER_HIT   = 3,
    PLAYER_DIED  = 4,
    SPAWN_WEAPON = 5,
};

// ============================================================================
// Network Packet Structures (POD, sent raw over UDP)
// ============================================================================

#pragma pack(push, 1)

struct PacketHeader {
    uint8_t type;
};

// Client -> Server: Join request
struct JoinPacket {
    uint8_t type = (uint8_t)ClientPacket::JOIN;
    char    name[32] = {};
};

// Client -> Server: Input per tick
struct InputPacket {
    uint8_t  type = (uint8_t)ClientPacket::INPUT;
    uint32_t seq = 0;
    uint16_t keys = 0;
    float    yaw = 0;
    float    pitch = 0;
};

// Client -> Server: Disconnect
struct DisconnectPacket {
    uint8_t type = (uint8_t)ClientPacket::DISCONNECT;
};

// Server -> Client: Join acknowledgement
struct JoinAckPacket {
    uint8_t  type = (uint8_t)ServerPacket::JOIN_ACK;
    uint8_t  playerId = 0;
    uint8_t  numBots = 0;
};

// Per-player state in snapshot
struct NetPlayerState {
    uint8_t  playerId = 0;
    uint8_t  state = 0;
    float    x = 0, y = 0, z = 0;
    float    yaw = 0, pitch = 0;
    uint8_t  health = 0;
    uint8_t  weapon = 0;
    uint8_t  ammo = 0;
    int16_t  vehicleId = -1;
    uint8_t  teamId = 0;
};

// Per-vehicle state in snapshot
struct NetVehicleState {
    uint8_t  id = 0;
    uint8_t  type = 0;
    float    x = 0, y = 0, z = 0;
    float    yaw = 0;
    float    pitch = 0;
    float    turretYaw = 0;
    int16_t  health = 0;
    int16_t  driverId = -1;
    uint8_t  active = 1;
    float    rotorAngle = 0;
};

// Per-flag state in snapshot
struct NetFlagState {
    uint8_t  teamId = 0;
    float    x = 0, y = 0, z = 0;
    int16_t  carrierId = -1;
    uint8_t  atBase = 1;
};

// Per-tornado state in snapshot
struct NetTornadoState {
    float    x = 0, y = 0, z = 0;
    float    radius = 15.0f;
    float    rotation = 0;
    uint8_t  active = 0;
};

// Per-weapon-pickup state in snapshot
struct NetWeaponState {
    uint16_t id = 0;
    uint8_t  type = 0;
    float    x = 0, y = 0, z = 0;
    uint8_t  active = 0;
};

// Server -> Client: Full world snapshot
struct SnapshotPacket {
    uint8_t  type = (uint8_t)ServerPacket::SNAPSHOT;
    uint32_t serverTick = 0;
    uint32_t ackInputSeq = 0;
    uint8_t  numPlayers = 0;
    uint8_t  teamScores[2] = {0, 0}; // CTF scores
    // Followed by: NetPlayerState[numPlayers]
    // Then: uint8_t numWeapons
    // Then: NetWeaponState[numWeapons]
    // Then: uint8_t numVehicles
    // Then: NetVehicleState[numVehicles]
    // Then: 2x NetFlagState (team 0, team 1)
    // Then: uint8_t numTornados
    // Then: NetTornadoState[numTornados]
};

// Server -> Client: Hit notification
struct PlayerHitPacket {
    uint8_t type = (uint8_t)ServerPacket::PLAYER_HIT;
    uint8_t attackerId = 0;
    uint8_t victimId = 0;
    int16_t damage = 0;
};

// Server -> Client: Death notification
struct PlayerDiedPacket {
    uint8_t type = (uint8_t)ServerPacket::PLAYER_DIED;
    uint8_t victimId = 0;
    uint8_t killerId = 0;
};

#pragma pack(pop)

// ============================================================================
// UDP Socket Wrapper
// ============================================================================

class UDPSocket {
public:
    bool bind(uint16_t port);
    bool open();
    void setNonBlocking(bool enable);
    int  sendTo(const void* data, size_t len, const sockaddr_in& addr);
    int  recvFrom(void* buf, size_t maxLen, sockaddr_in& fromAddr);
    void close();
    bool isValid() const { return fd_ >= 0; }

    static sockaddr_in makeAddr(const char* ip, uint16_t port);
    static bool addrEqual(const sockaddr_in& a, const sockaddr_in& b);

private:
    int fd_ = -1;
};
