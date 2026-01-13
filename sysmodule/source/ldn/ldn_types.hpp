/**
 * @file ldn_types.hpp
 * @brief Nintendo LDN (Local Data Network) protocol types
 *
 * This module defines the data structures used by Nintendo's LDN service
 * for local wireless communication between Switch consoles. These types
 * are used by games when calling the ldn:u service.
 *
 * Based on reverse engineering from ldn_mitm and switchbrew documentation.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>

namespace ams::mitm::ldn {

// ============================================================================
// Constants
// ============================================================================

/// Maximum length of SSID (network name)
constexpr size_t SsidLengthMax = 32;

/// Maximum size of advertise data
constexpr size_t AdvertiseDataSizeMax = 384;

/// Maximum length of user name
constexpr size_t UserNameBytesMax = 32;

/// Maximum number of nodes in a network
constexpr int NodeCountMax = 8;

/// Maximum number of stations (clients) - one less than nodes (host excluded)
constexpr int StationCountMax = NodeCountMax - 1;

/// Maximum length of passphrase
constexpr size_t PassphraseLengthMax = 64;

// ============================================================================
// Enumerations
// ============================================================================

/**
 * @brief LDN communication state
 *
 * Represents the current state of the LDN service for a client.
 */
enum class CommState : u32 {
    None = 0,               ///< Not initialized
    Initialized = 1,        ///< Initialized, ready to open AP or Station
    AccessPoint = 2,        ///< Access point mode, ready to create network
    AccessPointCreated = 3, ///< Network created, accepting connections
    Station = 4,            ///< Station mode, ready to scan/connect
    StationConnected = 5,   ///< Connected to a network
    Error = 6               ///< Error state
};

/**
 * @brief Node state change types
 *
 * Used to notify applications of player join/leave events.
 */
enum class NodeStateChange : u8 {
    None = 0,                   ///< No change
    Connect = 1,                ///< Node connected
    Disconnect = 2,             ///< Node disconnected
    DisconnectAndConnect = 3    ///< Node disconnected then reconnected
};

/**
 * @brief Scan filter flags
 *
 * Flags to control what networks are returned by Scan().
 */
enum ScanFilterFlag : u32 {
    ScanFilterFlag_LocalCommunicationId = 1 << 0,
    ScanFilterFlag_SessionId = 1 << 1,
    ScanFilterFlag_NetworkType = 1 << 2,
    ScanFilterFlag_Ssid = 1 << 4,
    ScanFilterFlag_SceneId = 1 << 5,
    ScanFilterFlag_IntentId = ScanFilterFlag_LocalCommunicationId | ScanFilterFlag_SceneId,
    ScanFilterFlag_NetworkId = ScanFilterFlag_IntentId | ScanFilterFlag_SessionId
};

/**
 * @brief Disconnect reason codes
 */
enum class DisconnectReason : u32 {
    None = 0,
    User = 1,               ///< User requested disconnect
    SystemRequest = 2,      ///< System requested disconnect
    DestroyedByUser = 3,    ///< Network destroyed by host
    DestroyedBySystem = 4,  ///< Network destroyed by system
    Rejected = 5,           ///< Connection rejected by host
    SignalLost = 6          ///< Connection lost
};

// ============================================================================
// Basic Types
// ============================================================================

/**
 * @brief MAC address structure
 */
struct MacAddress {
    u8 raw[6];

    bool operator==(const MacAddress& b) const {
        return std::memcmp(raw, b.raw, sizeof(raw)) == 0;
    }
};
static_assert(sizeof(MacAddress) == 6);

/**
 * @brief Network SSID (name)
 */
struct Ssid {
    u8 length;
    char raw[SsidLengthMax + 1];

    bool operator==(const Ssid& b) const {
        if (length != b.length) return false;
        return std::memcmp(raw, b.raw, length) == 0;
    }

    Ssid& operator=(const char* s) {
        size_t len = std::strlen(s);
        if (len > SsidLengthMax) len = SsidLengthMax;
        length = static_cast<u8>(len);
        std::memcpy(raw, s, len);
        raw[len] = '\0';
        return *this;
    }
};
static_assert(sizeof(Ssid) == 34);

/**
 * @brief Session identifier
 */
struct SessionId {
    u64 high;
    u64 low;

    bool operator==(const SessionId& b) const {
        return high == b.high && low == b.low;
    }
};
static_assert(sizeof(SessionId) == 16);

/**
 * @brief Intent identifier (game + scene)
 */
struct IntentId {
    u64 localCommunicationId;   ///< Title ID / Game ID
    u8 _unk1[2];
    u16 sceneId;                ///< Scene ID within game
    u8 _unk2[4];
};
static_assert(sizeof(IntentId) == 16);

/**
 * @brief Network identifier
 */
struct NetworkId {
    IntentId intentId;      ///< 16 bytes
    SessionId sessionId;    ///< 16 bytes
};
static_assert(sizeof(NetworkId) == 32);

// ============================================================================
// Network Structures
// ============================================================================

/**
 * @brief Common network information
 */
struct CommonNetworkInfo {
    MacAddress bssid;
    Ssid ssid;
    s16 channel;
    s8 linkLevel;
    u8 networkType;
    u32 _unk;
};
static_assert(sizeof(CommonNetworkInfo) == 48);

/**
 * @brief Information about a node (player) in the network
 */
struct NodeInfo {
    u32 ipv4Address;
    MacAddress macAddress;
    s8 nodeId;
    s8 isConnected;
    char userName[UserNameBytesMax + 1];
    u8 _unk1;
    s16 localCommunicationVersion;
    u8 _unk2[16];
};
static_assert(sizeof(NodeInfo) == 64);

/**
 * @brief LDN-specific network information
 */
struct LdnNetworkInfo {
    u8 unkRandom[16];
    u16 securityMode;
    u8 stationAcceptPolicy;
    u8 _unk1[3];
    u8 nodeCountMax;
    u8 nodeCount;
    NodeInfo nodes[NodeCountMax];
    u16 _unk2;
    u16 advertiseDataSize;
    u8 advertiseData[AdvertiseDataSizeMax];
    u8 _unk3[148];
};
static_assert(sizeof(LdnNetworkInfo) == 1072);

/**
 * @brief Complete network information
 *
 * Returned by Scan() and used for Connect().
 */
struct NetworkInfo : public ams::sf::LargeData {
    NetworkId networkId;
    CommonNetworkInfo common;
    LdnNetworkInfo ldn;
};
static_assert(sizeof(NetworkInfo) == 0x480);

// ============================================================================
// Configuration Structures
// ============================================================================

/**
 * @brief Security configuration
 */
struct SecurityConfig {
    u16 securityMode;
    u16 passphraseSize;
    u8 passphrase[PassphraseLengthMax];
};
static_assert(sizeof(SecurityConfig) == 68);

/**
 * @brief User configuration (player name)
 */
struct UserConfig {
    char userName[UserNameBytesMax + 1];
    u8 _unk[15];
};
static_assert(sizeof(UserConfig) == 48);

/**
 * @brief Network configuration for creating a network
 */
struct NetworkConfig {
    IntentId intentId;          ///< 16 bytes
    u16 channel;
    u8 nodeCountMax;
    u8 _unk1;
    u16 localCommunicationVersion;
    u8 _unk2[10];
};
static_assert(sizeof(NetworkConfig) == 32);

/**
 * @brief Combined configuration for CreateNetwork()
 */
struct CreateNetworkConfig {
    SecurityConfig securityConfig;
    UserConfig userConfig;
    u8 _unk[4];
    NetworkConfig networkConfig;
};
static_assert(sizeof(CreateNetworkConfig) == 152);

/**
 * @brief Connection data for Connect()
 */
struct ConnectNetworkData {
    SecurityConfig securityConfig;
    UserConfig userConfig;
    u32 localCommunicationVersion;
    u32 option;
};
static_assert(sizeof(ConnectNetworkData) == 124);

/**
 * @brief Node update notification
 */
struct NodeLatestUpdate : public ams::sf::PrefersPointerTransferMode {
    u8 stateChange;
    u8 _unk[7];
};
static_assert(sizeof(NodeLatestUpdate) == 8);

/**
 * @brief Security parameters from a network
 */
struct SecurityParameter {
    u8 unkRandom[16];
    SessionId sessionId;
};
static_assert(sizeof(SecurityParameter) == 32);

/**
 * @brief Scan filter configuration
 */
struct ScanFilter {
    NetworkId networkId;
    u32 networkType;
    MacAddress bssid;
    Ssid ssid;
    u8 unk[16];
    u32 flag;
};
static_assert(sizeof(ScanFilter) == 96);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Extract NetworkConfig from NetworkInfo
 *
 * @param info Source network info
 * @param out Destination network config
 */
inline void NetworkInfo2NetworkConfig(const NetworkInfo* info, NetworkConfig* out) {
    out->intentId = info->networkId.intentId;
    out->channel = info->common.channel;
    out->nodeCountMax = info->ldn.nodeCountMax;
    out->_unk1 = 0;
    out->localCommunicationVersion = info->ldn.nodes[0].localCommunicationVersion;
    std::memset(out->_unk2, 0, sizeof(out->_unk2));
}

/**
 * @brief Extract SecurityParameter from NetworkInfo
 *
 * @param info Source network info
 * @param out Destination security parameter
 */
inline void NetworkInfo2SecurityParameter(const NetworkInfo* info, SecurityParameter* out) {
    std::memcpy(out->unkRandom, info->ldn.unkRandom, sizeof(out->unkRandom));
    out->sessionId = info->networkId.sessionId;
}

/**
 * @brief Convert CommState to string for logging
 *
 * @param state State to convert
 * @return Human-readable state name
 */
inline const char* CommStateToString(CommState state) {
    switch (state) {
        case CommState::None:               return "None";
        case CommState::Initialized:        return "Initialized";
        case CommState::AccessPoint:        return "AccessPoint";
        case CommState::AccessPointCreated: return "AccessPointCreated";
        case CommState::Station:            return "Station";
        case CommState::StationConnected:   return "StationConnected";
        case CommState::Error:              return "Error";
        default:                            return "Unknown";
    }
}

} // namespace ams::mitm::ldn
