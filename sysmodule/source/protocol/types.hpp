/**
 * @file types.hpp
 * @brief RyuLdn Protocol Types - Binary-compatible with C# server
 *
 * This file defines all data structures used in the RyuLdn protocol for
 * communication between Nintendo Switch clients and the ryu_ldn server.
 *
 * ## Binary Compatibility
 *
 * **CRITICAL**: All structures must maintain exact binary layout matching
 * the C# server implementation. This is achieved through:
 *
 * 1. `__attribute__((packed))` - Prevents compiler padding/alignment
 * 2. `static_assert` - Compile-time verification of structure sizes
 * 3. Fixed-size arrays - No dynamic allocation or pointers
 *
 * Any modification to these structures MUST be verified against:
 * - C# server source: LdnServer/Network/RyuLdnProtocol.cs
 * - Ryujinx client: Ryujinx.HLE/HOS/Services/Ldn/Types/
 *
 * ## Byte Order
 *
 * All multi-byte integers are in LITTLE-ENDIAN format, matching the
 * native byte order of both x86/x64 (server) and ARM (Switch).
 *
 * ## Structure Categories
 *
 * 1. **Basic Types**: MacAddress, SessionId, NetworkId, Ssid
 * 2. **Network Info**: NodeInfo, CommonNetworkInfo, LdnNetworkInfo, NetworkInfo
 * 3. **Messages**: InitializeMessage, PingMessage, DisconnectMessage
 * 4. **Requests**: CreateAccessPointRequest, ConnectRequest, ScanFilterFull
 * 5. **Proxy Types**: ProxyDataHeader, ProxyConnectRequest, ProxyConnectResponse
 *
 * ## Usage Example
 *
 * @code
 * // Create an initialize message
 * InitializeMessage msg{};
 * std::memcpy(msg.id.data, client_uuid, 16);
 * std::memcpy(msg.mac_address.data, mac, 6);
 *
 * // Encode to buffer
 * uint8_t buffer[256];
 * size_t size;
 * encode(buffer, sizeof(buffer), PacketId::Initialize, msg, size);
 * @endcode
 *
 * @see ryu_protocol.hpp for encoding/decoding functions
 * @see packet_buffer.hpp for TCP stream handling
 *
 * @copyright Ported from ryu_ldn (MIT License)
 * @license MIT
 * Reference: LdnServer/Network/RyuLdnProtocol.cs
 */

#pragma once

#include <cstdint>
#include <cstring>

namespace ryu_ldn::protocol {

// =============================================================================
// Protocol Constants
// =============================================================================

/**
 * @brief Protocol magic number: "RLDN" in little-endian (0x4E444C52)
 *
 * Every packet starts with this 4-byte magic number for identification.
 * Packets with incorrect magic are rejected as invalid.
 */
constexpr uint32_t PROTOCOL_MAGIC = ('R' << 0) | ('L' << 8) | ('D' << 16) | ('N' << 24);

/**
 * @brief Current protocol version
 *
 * Used for version negotiation during handshake. If versions don't match,
 * the connection is rejected with a version mismatch error.
 */
constexpr uint8_t PROTOCOL_VERSION = 1;

/**
 * @brief Maximum packet payload size (128 KB)
 *
 * Packets larger than this are rejected to prevent memory exhaustion.
 * Most game data packets are much smaller (typically < 1KB).
 */
constexpr size_t MAX_PACKET_SIZE = 131072;

/**
 * @brief Maximum number of nodes (players) in a network session
 *
 * LDN supports up to 8 players in a local wireless session.
 */
constexpr size_t MAX_NODES = 8;

// =============================================================================
// Packet Types
// =============================================================================

/**
 * @brief Packet type identifiers
 *
 * Each packet type has a unique ID that identifies its purpose and payload
 * structure. The ID is stored in the LdnHeader.type field.
 *
 * ## Packet Categories
 *
 * **Session Management (0-1)**:
 * - Initialize: Client identification and version check
 * - Passphrase: Private room authentication
 *
 * **Access Point Operations (2-9)**:
 * - CreateAccessPoint: Host creates a new game session
 * - SyncNetwork: Network state synchronization
 * - Reject/RejectReply: Player rejection handling
 *
 * **Network Discovery (10-16)**:
 * - Scan: Search for available networks
 * - ScanReply/ScanReplyEnd: Network list response
 * - Connect/Connected: Join a network session
 * - Disconnect: Leave a network session
 *
 * **Proxy Operations (17-21)**:
 * - ProxyConfig/Connect/Data/Disconnect: P2P proxy tunneling
 *
 * **Host Control (22-23)**:
 * - SetAcceptPolicy: Control who can join
 * - SetAdvertiseData: Update session metadata
 *
 * **Utility (254-255)**:
 * - Ping: Keepalive and latency measurement
 * - NetworkError: Error reporting
 */
enum class PacketId : uint8_t {
    // Session management
    Initialize = 0,              ///< Client sends ID and MAC to server
    Passphrase = 1,              ///< Client sends passphrase for private rooms

    // Access point operations
    CreateAccessPoint = 2,       ///< Create a public network session
    CreateAccessPointPrivate = 3,///< Create a private (passphrase) session
    ExternalProxy = 4,           ///< Configure external proxy mode
    ExternalProxyToken = 5,      ///< External proxy authentication token
    ExternalProxyState = 6,      ///< External proxy state update
    SyncNetwork = 7,             ///< Synchronize network state to clients
    Reject = 8,                  ///< Host rejects a player
    RejectReply = 9,             ///< Server confirms rejection

    // Network discovery
    Scan = 10,                   ///< Client requests available networks
    ScanReply = 11,              ///< Server sends one network info
    ScanReplyEnd = 12,           ///< Server finished sending networks
    Connect = 13,                ///< Client requests to join a network
    ConnectPrivate = 14,         ///< Client requests to join private network
    Connected = 15,              ///< Server confirms connection success
    Disconnect = 16,             ///< Client/server announces disconnect

    // Proxy operations (P2P tunneling)
    ProxyConfig = 17,            ///< Configure proxy settings
    ProxyConnect = 18,           ///< Request P2P connection through proxy
    ProxyConnectReply = 19,      ///< Proxy connection result
    ProxyData = 20,              ///< Game data through proxy
    ProxyDisconnect = 21,        ///< Close proxy connection

    // Host control
    SetAcceptPolicy = 22,        ///< Change accept policy (allow/reject)
    SetAdvertiseData = 23,       ///< Update advertise data

    // Utility
    Ping = 254,                  ///< Keepalive packet with timestamp
    NetworkError = 255           ///< Error notification
};

// =============================================================================
// Basic Types (packed structures)
// =============================================================================

/**
 * @brief LDN Protocol Header - 10 bytes
 *
 * Every packet in the RyuLdn protocol starts with this header.
 * The header contains identification, versioning, and size information.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field       Description
 * 0x00    4     magic       Protocol magic (0x4E444C52 = "RLDN")
 * 0x04    1     type        Packet type (PacketId enum)
 * 0x05    1     version     Protocol version (must be 1)
 * 0x06    4     data_size   Payload size in bytes (signed for compatibility)
 * ```
 *
 * ## Validation
 * When receiving a packet, validate:
 * 1. magic == PROTOCOL_MAGIC
 * 2. version == PROTOCOL_VERSION
 * 3. data_size >= 0 && data_size <= MAX_PACKET_SIZE
 */
struct __attribute__((packed)) LdnHeader {
    uint32_t magic;      ///< Must be PROTOCOL_MAGIC (0x4E444C52 = "RLDN")
    uint8_t  type;       ///< Packet type from PacketId enum
    uint8_t  version;    ///< Protocol version (must be PROTOCOL_VERSION = 1)
    int32_t  data_size;  ///< Size of payload following header (may be 0)
};
static_assert(sizeof(LdnHeader) == 0xA, "LdnHeader must be 10 bytes");

/**
 * @brief MAC Address - 6 bytes
 *
 * Standard IEEE 802 MAC address used to identify network interfaces.
 * In RyuLdn, this identifies Switch consoles in the network session.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field  Description
 * 0x00    6     data   MAC address bytes (network byte order)
 * ```
 *
 * ## Example
 * MAC "AA:BB:CC:DD:EE:FF" is stored as: {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
 */
struct __attribute__((packed)) MacAddress {
    uint8_t data[6];  ///< 6-byte MAC address

    /**
     * @brief Compare two MAC addresses for equality
     * @param other MAC address to compare with
     * @return true if addresses are identical
     */
    bool operator==(const MacAddress& other) const {
        return std::memcmp(data, other.data, 6) == 0;
    }

    /**
     * @brief Check if MAC address is all zeros
     * @return true if all bytes are zero
     *
     * Zero MAC address is used to indicate "unassigned" or "any"
     * in certain protocol operations (e.g., new client registration).
     */
    bool is_zero() const {
        for (int i = 0; i < 6; i++) {
            if (data[i] != 0) return false;
        }
        return true;
    }
};
static_assert(sizeof(MacAddress) == 6, "MacAddress must be 6 bytes");

/**
 * @brief Session ID - 16 bytes UUID
 *
 * Universally Unique Identifier for client sessions and networks.
 * Typically generated using random bytes or system UUID.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field  Description
 * 0x00    16    data   UUID bytes (raw format, not string)
 * ```
 *
 * ## Generation
 * On first connection, client sends zero SessionId. Server may assign
 * a new ID, or client can generate using random bytes.
 */
struct __attribute__((packed)) SessionId {
    uint8_t data[16];  ///< 16-byte UUID

    /**
     * @brief Check if session ID is all zeros
     * @return true if all bytes are zero
     *
     * Zero session ID typically means "unassigned" or "new client".
     */
    bool is_zero() const {
        for (int i = 0; i < 16; i++) {
            if (data[i] != 0) return false;
        }
        return true;
    }
};
static_assert(sizeof(SessionId) == 16, "SessionId must be 16 bytes");

/**
 * @brief Intent ID - 16 bytes
 *
 * Identifies the game and specific mode/scene for matchmaking.
 * Used to ensure players are matched with compatible game sessions.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field                      Description
 * 0x00    8     local_communication_id     Game Title ID (e.g., 0x0100152000022000 for MK8DX)
 * 0x08    2     reserved1                  Reserved (usually 0)
 * 0x0A    2     scene_id                   Scene/mode within game
 * 0x0C    4     reserved2                  Reserved (usually 0)
 * ```
 *
 * ## Game Identification
 * - local_communication_id: Nintendo Switch Title ID from game metadata
 * - scene_id: Game-specific scene number (e.g., online lobby vs local play)
 */
struct __attribute__((packed)) IntentId {
    int64_t  local_communication_id;  ///< Title ID / Game ID (e.g., Mario Kart 8 DX)
    uint16_t reserved1;               ///< Reserved, set to 0
    uint16_t scene_id;                ///< Scene/mode within game (game-specific)
    uint32_t reserved2;               ///< Reserved, set to 0
};
static_assert(sizeof(IntentId) == 0x10, "IntentId must be 16 bytes");

/**
 * @brief Network ID - 32 bytes
 *
 * Uniquely identifies a network session by combining the game intent
 * with a unique session identifier.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field       Description
 * 0x00    16    intent_id   Game identification (IntentId)
 * 0x10    16    session_id  Unique session UUID (SessionId)
 * ```
 *
 * Two networks with the same intent_id but different session_id are
 * separate game sessions for the same game/mode.
 */
struct __attribute__((packed)) NetworkId {
    IntentId  intent_id;   ///< Game and scene identification
    SessionId session_id;  ///< Unique session identifier
};
static_assert(sizeof(NetworkId) == 0x20, "NetworkId must be 32 bytes");

/**
 * @brief SSID (Service Set Identifier) - 34 bytes
 *
 * Network name, similar to WiFi SSID. Used for display purposes
 * and network identification in the UI.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field   Description
 * 0x00    1     length  Length of SSID string (0-33)
 * 0x01    33    name    SSID string (null-padded)
 * ```
 *
 * ## String Format
 * The name field is NOT necessarily null-terminated. Use the length
 * field to determine the actual string length.
 */
struct __attribute__((packed)) Ssid {
    uint8_t length;    ///< Length of SSID string (0-33 bytes)
    uint8_t name[33];  ///< SSID string (not null-terminated, use length)
};
static_assert(sizeof(Ssid) == 0x22, "Ssid must be 34 bytes");

/**
 * @brief Node Info - 64 bytes
 *
 * Information about a single player/node in the network session.
 * Each network can have up to MAX_NODES (8) players.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field                        Description
 * 0x00    4     ipv4_address                 IPv4 address (network byte order)
 * 0x04    6     mac_address                  Player's MAC address
 * 0x0A    1     node_id                      Player slot (0-7)
 * 0x0B    1     is_connected                 1 = connected, 0 = disconnected
 * 0x0C    33    user_name                    Player name (UTF-8, null-terminated)
 * 0x2D    1     reserved1                    Reserved
 * 0x2E    2     local_communication_version  Game protocol version
 * 0x30    16    reserved2                    Reserved
 * ```
 *
 * ## Node IDs
 * - Node 0: Always the host
 * - Nodes 1-7: Other players in join order
 *
 * ## Connection State
 * is_connected indicates if the slot is currently occupied.
 * Disconnected slots may retain stale data until reused.
 */
struct __attribute__((packed)) NodeInfo {
    uint32_t   ipv4_address;                ///< IPv4 address (network byte order)
    MacAddress mac_address;                 ///< Player's MAC address
    uint8_t    node_id;                     ///< Node slot (0 = host, 1-7 = clients)
    uint8_t    is_connected;                ///< Connection status (1 = connected)
    char       user_name[33];               ///< Player name (UTF-8, null-terminated)
    uint8_t    reserved1;                   ///< Reserved, set to 0
    uint16_t   local_communication_version; ///< Game's LDN protocol version
    uint8_t    reserved2[16];               ///< Reserved, set to 0
};
static_assert(sizeof(NodeInfo) == 0x40, "NodeInfo must be 64 bytes");

/**
 * @brief Common Network Info - 48 bytes
 *
 * Basic network identification and radio parameters.
 * Shared between all network info structures.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field         Description
 * 0x00    6     mac_address   Network's BSSID (host MAC)
 * 0x06    34    ssid          Network name (Ssid structure)
 * 0x28    2     channel       WiFi channel number
 * 0x2A    1     link_level    Signal strength indicator
 * 0x2B    1     network_type  Network type (NetworkType enum)
 * 0x2C    4     reserved      Reserved
 * ```
 *
 * ## Network Type
 * - 0: None
 * - 1: General (any LDN)
 * - 2: LDN (specific game)
 * - 3: All
 */
struct __attribute__((packed)) CommonNetworkInfo {
    MacAddress mac_address;   ///< Network BSSID (typically host's MAC)
    Ssid       ssid;          ///< Network name for display
    uint16_t   channel;       ///< WiFi channel (1-11/13)
    uint8_t    link_level;    ///< Signal strength (0-3)
    uint8_t    network_type;  ///< NetworkType enum value
    uint32_t   reserved;      ///< Reserved, set to 0
};
static_assert(sizeof(CommonNetworkInfo) == 0x30, "CommonNetworkInfo must be 48 bytes");

/**
 * @brief LDN Network Info - 0x430 bytes (1072 bytes)
 *
 * Extended network information specific to LDN protocol.
 * Contains player list, security settings, and game-specific data.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field                  Description
 * 0x000   16    security_parameter     Security/encryption parameters
 * 0x010   2     security_mode          SecurityMode enum
 * 0x012   1     station_accept_policy  AcceptPolicy enum
 * 0x013   1     unknown1               Unknown
 * 0x014   2     reserved1              Reserved
 * 0x016   1     node_count_max         Maximum players allowed (1-8)
 * 0x017   1     node_count             Current player count
 * 0x018   512   nodes[8]               Array of NodeInfo (8 * 64 bytes)
 * 0x218   2     reserved2              Reserved
 * 0x21A   2     advertise_data_size    Size of advertise data
 * 0x21C   384   advertise_data         Game-specific matchmaking data
 * 0x39C   140   unknown2               Unknown/reserved
 * 0x428   8     authentication_id      Network authentication ID
 * ```
 *
 * ## Advertise Data
 * Game-specific data used for matchmaking filtering.
 * Content varies by game (e.g., game mode, map, restrictions).
 */
struct __attribute__((packed)) LdnNetworkInfo {
    uint8_t  security_parameter[16];      ///< Security/encryption parameters
    uint16_t security_mode;               ///< SecurityMode enum value
    uint8_t  station_accept_policy;       ///< AcceptPolicy enum value
    uint8_t  unknown1;                    ///< Unknown field
    uint16_t reserved1;                   ///< Reserved, set to 0
    uint8_t  node_count_max;              ///< Maximum players (1-8)
    uint8_t  node_count;                  ///< Current connected players
    NodeInfo nodes[MAX_NODES];            ///< Player information (8 * 64 = 512 bytes)
    uint16_t reserved2;                   ///< Reserved, set to 0
    uint16_t advertise_data_size;         ///< Size of advertise_data (0-384)
    uint8_t  advertise_data[384];         ///< Game-specific matchmaking data
    uint8_t  unknown2[140];               ///< Unknown/reserved
    uint64_t authentication_id;           ///< Network authentication identifier
};
static_assert(sizeof(LdnNetworkInfo) == 0x430, "LdnNetworkInfo must be 0x430 bytes");

/**
 * @brief Network Info - 0x480 bytes (1152 bytes)
 *
 * Complete network information structure containing all details
 * about a network session. This is the main structure used in
 * ScanReply, Connected, and SyncNetwork packets.
 *
 * ## Wire Format
 * ```
 * Offset  Size   Field       Description
 * 0x000   32     network_id  Unique network identifier (NetworkId)
 * 0x020   48     common      Basic network info (CommonNetworkInfo)
 * 0x050   1072   ldn         Extended LDN info (LdnNetworkInfo)
 * ```
 *
 * ## Usage
 * - ScanReply: Server sends this for each discovered network
 * - Connected: Server sends this when client joins successfully
 * - SyncNetwork: Server broadcasts this to update all clients
 */
struct __attribute__((packed)) NetworkInfo {
    NetworkId         network_id;  ///< Unique network identifier
    CommonNetworkInfo common;      ///< Basic network information
    LdnNetworkInfo    ldn;         ///< Extended LDN-specific information
};
static_assert(sizeof(NetworkInfo) == 0x480, "NetworkInfo must be 0x480 bytes");

// =============================================================================
// Message Types
// =============================================================================

/**
 * @brief Initialize Message - 22 bytes
 *
 * First message sent by client after TCP connection to identify themselves.
 * Server uses this for session management and MAC address assignment.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field        Description
 * 0x00    16    id           Client session ID (SessionId)
 * 0x10    6     mac_address  Client MAC address (MacAddress)
 * ```
 *
 * ## New Client Registration
 * - id: All zeros to request new session ID from server
 * - mac_address: All zeros to request MAC assignment from server
 *
 * ## Reconnection
 * - id: Previous session ID to restore session state
 * - mac_address: Previous MAC to maintain identity
 *
 * ## Protocol Flow
 * 1. Client sends Initialize with id/mac (zeros for new)
 * 2. Server validates and may assign new id/mac
 * 3. Server sends response (typically SyncNetwork or NetworkError)
 */
struct __attribute__((packed)) InitializeMessage {
    SessionId  id;           ///< Client session ID (zeros = new client)
    MacAddress mac_address;  ///< Client MAC address (zeros = assign new)
};
static_assert(sizeof(InitializeMessage) == 0x16, "InitializeMessage must be 22 bytes");

/**
 * @brief Passphrase Message - 128 bytes (0x80)
 *
 * Sent by client after TCP connection to filter/authenticate rooms.
 * Format: "Ryujinx-[0-9a-f]{8}" or empty string for public rooms.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field       Description
 * 0x00    128   passphrase  UTF-8 passphrase (null-padded)
 * ```
 *
 * ## Protocol Flow
 * 1. Client connects via TCP
 * 2. Client sends Passphrase packet (can be empty)
 * 3. Client sends Initialize packet
 * 4. Server responds with Initialize (assigned ID/MAC)
 *
 * ## Security Note
 * Passphrase is sent in plaintext. Use TLS for transport security.
 */
struct __attribute__((packed)) PassphraseMessage {
    uint8_t passphrase[128];  ///< UTF-8 passphrase (null-padded, max 128 chars)
};
static_assert(sizeof(PassphraseMessage) == 0x80, "PassphraseMessage must be 128 bytes");

/**
 * @brief Ping Message - 2 bytes
 *
 * Keepalive packet sent periodically to detect disconnections.
 * Server sends Ping with Requester=0, client must echo it back.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field      Description
 * 0x00    1     requester  0 = server requested, 1 = client requested
 * 0x01    1     id         Ping ID for matching request/response
 * ```
 *
 * ## Protocol
 * - Server sends Ping with requester=0 and unique id
 * - Client must echo back the exact same packet
 * - Server tracks id to detect dropped connections
 */
struct __attribute__((packed)) PingMessage {
    uint8_t requester;  ///< 0 = server requested (echo back), 1 = client requested
    uint8_t id;         ///< Ping ID for matching request/response
};
static_assert(sizeof(PingMessage) == 2, "PingMessage must be 2 bytes");

/**
 * @brief Disconnect Message - 4 bytes
 *
 * Sent when leaving a network session. Contains the IP address
 * of the disconnecting client for identification.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field         Description
 * 0x00    4     disconnect_ip IPv4 address of disconnecting client
 * ```
 *
 * ## Note
 * The disconnect reason is NOT included in this message.
 * Use RejectRequest for rejection with reason.
 */
struct __attribute__((packed)) DisconnectMessage {
    uint32_t disconnect_ip;  ///< IPv4 address of disconnecting client
};
static_assert(sizeof(DisconnectMessage) == 4, "DisconnectMessage must be 4 bytes");

/**
 * @brief Network Error Message - 4 bytes
 *
 * Sent by server to report protocol or session errors.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field       Description
 * 0x00    4     error_code  Error code (implementation-defined)
 * ```
 *
 * ## Common Error Codes
 * - Version mismatch
 * - Session not found
 * - Network full
 * - Authentication failed
 */
struct __attribute__((packed)) NetworkErrorMessage {
    uint32_t error_code;  ///< Error code (see server documentation)
};
static_assert(sizeof(NetworkErrorMessage) == 4, "NetworkErrorMessage must be 4 bytes");

/**
 * @brief Scan Filter (Basic) - 36 bytes
 *
 * Basic filter for network scanning. Use ScanFilterFull for complete filtering.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field       Description
 * 0x00    32    network_id  Filter by network ID (zeros = any)
 * 0x20    4     flag        Filter flags
 * ```
 */
struct __attribute__((packed)) ScanFilter {
    NetworkId network_id;  ///< Network ID filter (zeros = match any)
    uint32_t  flag;        ///< Filter flags (implementation-defined)
};
// Note: This is a simplified filter; see ScanFilterFull for complete version

/**
 * @brief Protocol Type for proxy connections - 4 bytes
 *
 * Maps to System.Net.Sockets.ProtocolType enum values.
 */
enum class ProtocolType : int32_t {
    Unknown = -1,
    Unspecified = 0,
    IP = 0,
    Icmp = 1,
    Igmp = 2,
    Ggp = 3,
    IPv4 = 4,
    Tcp = 6,
    Pup = 12,
    Udp = 17,
    Idp = 22,
    IPv6 = 41,
    IPv6RoutingHeader = 43,
    IPv6FragmentHeader = 44,
    IPSecEncapsulatingSecurityPayload = 50,
    IPSecAuthenticationHeader = 51,
    IcmpV6 = 58,
    IPv6NoNextHeader = 59,
    IPv6DestinationOptions = 60,
    ND = 77,
    Raw = 255,
    Ipx = 1000,
    Spx = 1256,
    SpxII = 1257
};

/**
 * @brief Proxy Info - 16 bytes (0x10)
 *
 * Information included in all proxied communication.
 * Contains source and destination addressing for P2P tunneling.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field        Description
 * 0x00    4     source_ipv4  Source IPv4 address
 * 0x04    2     source_port  Source port number
 * 0x06    4     dest_ipv4    Destination IPv4 address
 * 0x0A    2     dest_port    Destination port number
 * 0x0C    4     protocol     Protocol type (ProtocolType enum)
 * ```
 */
struct __attribute__((packed)) ProxyInfo {
    uint32_t     source_ipv4;  ///< Source IPv4 address
    uint16_t     source_port;  ///< Source port number
    uint32_t     dest_ipv4;    ///< Destination IPv4 address
    uint16_t     dest_port;    ///< Destination port number
    ProtocolType protocol;     ///< Protocol type (TCP/UDP)
};
static_assert(sizeof(ProxyInfo) == 0x10, "ProxyInfo must be 16 bytes");

/**
 * @brief Proxy Data Header - 20 bytes (0x14)
 *
 * Header prepended to proxied game data packets.
 * Used for P2P communication tunneled through the server.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field        Description
 * 0x00    16    info         Proxy connection info (ProxyInfo)
 * 0x10    4     data_length  Length of data following this header
 * ```
 */
struct __attribute__((packed)) ProxyDataHeader {
    ProxyInfo info;         ///< Source/destination addressing
    uint32_t  data_length;  ///< Length of payload data
};
static_assert(sizeof(ProxyDataHeader) == 0x14, "ProxyDataHeader must be 20 bytes");

/**
 * @brief Proxy Config - 8 bytes
 *
 * Configuration for proxy tunneling mode.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field             Description
 * 0x00    4     proxy_ip          Proxy server IP (network byte order)
 * 0x04    4     proxy_subnet_mask Subnet mask for proxy network
 * ```
 */
struct __attribute__((packed)) ProxyConfig {
    uint32_t proxy_ip;          ///< Proxy server IPv4 address
    uint32_t proxy_subnet_mask; ///< Subnet mask for proxy network
};
static_assert(sizeof(ProxyConfig) == 8, "ProxyConfig must be 8 bytes");

/**
 * @brief Proxy Connect Request - 16 bytes (0x10)
 *
 * Request to establish P2P connection through proxy.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field  Description
 * 0x00    16    info   Proxy connection info (ProxyInfo)
 * ```
 */
struct __attribute__((packed)) ProxyConnectRequest {
    ProxyInfo info;  ///< Connection addressing info
};
static_assert(sizeof(ProxyConnectRequest) == 0x10, "ProxyConnectRequest must be 16 bytes");

/**
 * @brief Proxy Connect Response - 16 bytes (0x10)
 *
 * Response to ProxyConnectRequest.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field  Description
 * 0x00    16    info   Proxy connection info (ProxyInfo)
 * ```
 */
struct __attribute__((packed)) ProxyConnectResponse {
    ProxyInfo info;  ///< Connection addressing info
};
static_assert(sizeof(ProxyConnectResponse) == 0x10, "ProxyConnectResponse must be 16 bytes");

/**
 * @brief Proxy Disconnect Message - 20 bytes (0x14)
 *
 * Notification that a proxied P2P connection was closed.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field             Description
 * 0x00    16    info              Proxy connection info (ProxyInfo)
 * 0x10    4     disconnect_reason Reason for disconnection
 * ```
 */
struct __attribute__((packed)) ProxyDisconnectMessage {
    ProxyInfo info;              ///< Connection that was closed
    int32_t   disconnect_reason; ///< Reason for disconnection
};
static_assert(sizeof(ProxyDisconnectMessage) == 0x14, "ProxyDisconnectMessage must be 20 bytes");

// ============================================================================
// External Proxy Structures
// ============================================================================

/**
 * @brief External Proxy Config - 0x26 bytes (38 bytes)
 *
 * Sent by the server to point a client towards an external server being used as a proxy.
 * The client then forwards this to the external proxy after connecting, to verify the connection worked.
 */
struct __attribute__((packed)) ExternalProxyConfig {
    uint8_t  proxy_ip[16];     ///< Proxy server IP address (IPv4 or IPv6 string)
    uint32_t address_family;   ///< AddressFamily enum (2=IPv4, 23=IPv6)
    uint16_t proxy_port;       ///< Proxy server port
    uint8_t  token[16];        ///< Authentication token
};
static_assert(sizeof(ExternalProxyConfig) == 0x26, "ExternalProxyConfig must be 0x26 bytes");

/**
 * @brief External Proxy Token - 0x28 bytes (40 bytes)
 *
 * Sent by the master server to an external proxy to tell them someone is going to connect.
 * This drives authentication, and lets the proxy know what virtual IP to give to each joiner.
 */
struct __attribute__((packed)) ExternalProxyToken {
    uint32_t virtual_ip;       ///< Virtual IP assigned to the client
    uint8_t  token[16];        ///< Authentication token
    uint8_t  physical_ip[16];  ///< Client's physical IP address
    uint32_t address_family;   ///< AddressFamily enum (2=IPv4, 23=IPv6)
};
static_assert(sizeof(ExternalProxyToken) == 0x28, "ExternalProxyToken must be 0x28 bytes");

/**
 * @brief External Proxy Connection State - 0x08 bytes (8 bytes)
 *
 * Indicates a change in connection state for the given client.
 * Is sent to notify the master server when connection is first established.
 * Can be sent by the external proxy to the master server to notify it of a proxy disconnect.
 *
 * Note: Pack=4 alignment means 3 bytes padding after the bool.
 */
struct ExternalProxyConnectionState {
    uint32_t ip_address;       ///< IP address of the client
    uint8_t  connected;        ///< Connection state (0=disconnected, 1=connected)
    uint8_t  _pad[3];          ///< Padding for Pack=4 alignment
};
static_assert(sizeof(ExternalProxyConnectionState) == 0x08, "ExternalProxyConnectionState must be 0x08 bytes");

// ============================================================================
// Request/Response Structures (Story 1.2)
// ============================================================================

/**
 * @brief Security Config - 0x44 bytes (68 bytes)
 */
struct __attribute__((packed)) SecurityConfig {
    uint16_t security_mode;
    uint16_t passphrase_size;
    uint8_t  passphrase[64];
};
static_assert(sizeof(SecurityConfig) == 0x44, "SecurityConfig must be 0x44 bytes");

/**
 * @brief Security Parameter - 0x20 bytes (32 bytes)
 *
 * Random security data generated for private rooms.
 * Used in CreateAccessPointPrivate and ConnectPrivate.
 */
struct __attribute__((packed)) SecurityParameter {
    uint8_t data[16];        ///< Random security data
    uint8_t session_id[16];  ///< Session ID
};
static_assert(sizeof(SecurityParameter) == 0x20, "SecurityParameter must be 0x20 bytes");

/**
 * @brief Address Entry - 0x0C bytes (12 bytes)
 *
 * IP/MAC address pair for a node.
 */
struct __attribute__((packed)) AddressEntry {
    uint32_t   ipv4_address;  ///< IPv4 address
    MacAddress mac_address;   ///< MAC address
    uint16_t   reserved;      ///< Reserved/padding
};
static_assert(sizeof(AddressEntry) == 0x0C, "AddressEntry must be 0x0C bytes");

/**
 * @brief Address List - 0x60 bytes (96 bytes)
 *
 * List of up to 8 address entries for nodes in a network.
 */
struct __attribute__((packed)) AddressList {
    AddressEntry addresses[8];  ///< Address entries (max 8 nodes)
};
static_assert(sizeof(AddressList) == 0x60, "AddressList must be 0x60 bytes");

/**
 * @brief User Config - 0x30 bytes (48 bytes)
 */
struct __attribute__((packed)) UserConfig {
    char    user_name[33];   ///< Player name (UTF-8, null-terminated)
    uint8_t unknown1[15];    ///< Unknown/reserved
};
static_assert(sizeof(UserConfig) == 0x30, "UserConfig must be 0x30 bytes");

/**
 * @brief Network Config - 0x20 bytes (32 bytes)
 */
struct __attribute__((packed)) NetworkConfig {
    IntentId intent_id;
    uint16_t channel;
    uint8_t  node_count_max;
    uint8_t  reserved1;
    uint16_t local_communication_version;
    uint8_t  reserved2[10];
};
static_assert(sizeof(NetworkConfig) == 0x20, "NetworkConfig must be 0x20 bytes");

/**
 * @brief Ryu Network Config - 0x28 bytes (40 bytes)
 * Extended config for Ryujinx-specific features
 */
struct __attribute__((packed)) RyuNetworkConfig {
    uint8_t  game_version[16];
    uint8_t  private_ip[16];       // For external proxy LAN detection
    uint32_t address_family;       // AddressFamily enum (2=IPv4, 23=IPv6)
    uint16_t external_proxy_port;
    uint16_t internal_proxy_port;
};
static_assert(sizeof(RyuNetworkConfig) == 0x28, "RyuNetworkConfig must be 0x28 bytes");

/**
 * @brief Create Access Point Request - 0xBC bytes (188 bytes)
 * Note: Advertise data is appended after this structure
 */
struct __attribute__((packed)) CreateAccessPointRequest {
    SecurityConfig   security_config;
    UserConfig       user_config;
    NetworkConfig    network_config;
    RyuNetworkConfig ryu_network_config;
};
static_assert(sizeof(CreateAccessPointRequest) == 0xBC, "CreateAccessPointRequest must be 0xBC bytes");

/**
 * @brief Create Access Point Private Request - 0x13C bytes (316 bytes)
 *
 * Request to create a private (password-protected) network.
 * Contains additional security data compared to CreateAccessPointRequest.
 * Advertise data is appended after this structure.
 */
struct __attribute__((packed)) CreateAccessPointPrivateRequest {
    SecurityConfig     security_config;      ///< 0x00: Security config (0x44)
    SecurityParameter  security_parameter;   ///< 0x44: Security parameter (0x20)
    UserConfig         user_config;          ///< 0x64: User config (0x30)
    NetworkConfig      network_config;       ///< 0x94: Network config (0x20)
    AddressList        address_list;         ///< 0xB4: Address list (0x60)
    RyuNetworkConfig   ryu_network_config;   ///< 0x114: Ryu network config (0x28)
};
static_assert(sizeof(CreateAccessPointPrivateRequest) == 0x13C, "CreateAccessPointPrivateRequest must be 0x13C bytes");

/**
 * @brief Connect Private Request - 0xBC bytes (188 bytes)
 *
 * Request to connect to a private (password-protected) network.
 * Contains SecurityParameter for authentication.
 */
struct __attribute__((packed)) ConnectPrivateRequest {
    SecurityConfig     security_config;             ///< 0x00: Security config (0x44)
    SecurityParameter  security_parameter;          ///< 0x44: Security parameter (0x20)
    UserConfig         user_config;                 ///< 0x64: User config (0x30)
    uint32_t           local_communication_version; ///< 0x94: LDN version
    uint32_t           option_unknown;              ///< 0x98: Unknown option
    NetworkConfig      network_config;              ///< 0x9C: Network config (0x20)
};
static_assert(sizeof(ConnectPrivateRequest) == 0xBC, "ConnectPrivateRequest must be 0xBC bytes");

/**
 * @brief Scan Filter (Full) - 0x60 bytes (96 bytes)
 *
 * Complete filter for network scanning. Uses 8-byte alignment
 * to match C# StructLayout with Pack=8.
 *
 * ## Wire Format (with alignment padding)
 * ```
 * Offset  Size  Field         Description
 * 0x00    32    network_id    Network ID filter (NetworkId)
 * 0x20    1     network_type  Network type filter (NetworkType enum)
 * 0x21    6     mac_address   MAC address filter (MacAddress)
 * 0x27    1     _pad1         Alignment padding
 * 0x28    34    ssid          SSID filter (Ssid)
 * 0x4A    2     _pad2         Alignment padding
 * 0x4C    16    reserved      Reserved
 * 0x5C    4     flag          Filter flags (ScanFilterFlag)
 * ```
 *
 * Total: 96 bytes (0x60)
 */
struct __attribute__((aligned(8))) ScanFilterFull {
    NetworkId  network_id;      // 0x00: 32 bytes
    uint8_t    network_type;    // 0x20: 1 byte
    MacAddress mac_address;     // 0x21: 6 bytes
    uint8_t    _pad1;           // 0x27: 1 byte padding
    Ssid       ssid;            // 0x28: 34 bytes
    uint16_t   _pad2;           // 0x4A: 2 bytes padding
    uint8_t    reserved[16];    // 0x4C: 16 bytes
    uint32_t   flag;            // 0x5C: 4 bytes
};
static_assert(sizeof(ScanFilterFull) == 0x60, "ScanFilterFull must be 96 bytes (0x60)");

/**
 * @brief Connect Request - 0x4FC bytes (1276 bytes)
 * Request to connect to a network
 */
struct __attribute__((packed)) ConnectRequest {
    SecurityConfig security_config;
    UserConfig     user_config;
    uint32_t       local_communication_version;
    uint32_t       option_unknown;
    NetworkInfo    network_info;
};
static_assert(sizeof(ConnectRequest) == 0x4FC, "ConnectRequest must be 0x4FC bytes");

/**
 * @brief Set Accept Policy Request - 1 byte
 *
 * Sent by host to change the station accept policy.
 * Must match Ryujinx/Server exactly (1 byte, Pack=1).
 */
struct __attribute__((packed)) SetAcceptPolicyRequest {
    uint8_t accept_policy;  ///< AcceptPolicy enum (0-3)
};
static_assert(sizeof(SetAcceptPolicyRequest) == 1, "SetAcceptPolicyRequest must be 1 byte");

/**
 * @brief Reject Request - 8 bytes
 *
 * Sent by host to reject/kick a player from the session.
 *
 * ## Wire Format
 * ```
 * Offset  Size  Field             Description
 * 0x00    4     node_id           Node ID of player to reject
 * 0x04    4     disconnect_reason Reason for rejection (DisconnectReason enum)
 * ```
 */
struct __attribute__((packed)) RejectRequest {
    uint32_t node_id;           ///< Node ID of player to reject
    uint32_t disconnect_reason; ///< DisconnectReason enum value
};
static_assert(sizeof(RejectRequest) == 8, "RejectRequest must be 8 bytes");

// ============================================================================
// Enums
// ============================================================================

enum class AcceptPolicy : uint8_t {
    AcceptAll = 0,
    RejectAll = 1,
    BlackList = 2,
    WhiteList = 3
};

enum class SecurityMode : uint16_t {
    Any = 0,
    Product = 1,
    Debug = 2
};

enum class NetworkType : uint8_t {
    None = 0,
    General = 1,
    Ldn = 2,
    All = 3
};

enum class DisconnectReason : uint32_t {
    None = 0,
    User = 1,
    SystemRequest = 2,
    DestroyedByHost = 3,
    DestroyedByAdmin = 4,
    Rejected = 5,
    SignalLost = 6
};

/**
 * @brief Network error codes
 *
 * Error codes returned in NetworkErrorMessage packets from the server.
 * These indicate protocol-level errors that occurred during communication.
 *
 * ## Handshake Errors (1-99)
 * Errors during the initial handshake phase.
 *
 * ## Session Errors (100-199)
 * Errors related to session management.
 *
 * ## Network Errors (200-299)
 * Errors related to network operations.
 */
enum class NetworkErrorCode : uint32_t {
    // Success
    None = 0,                    ///< No error

    // Handshake errors (1-99)
    VersionMismatch = 1,         ///< Protocol version doesn't match server
    InvalidMagic = 2,            ///< Invalid protocol magic number
    InvalidSessionId = 3,        ///< Session ID is invalid or expired
    HandshakeTimeout = 4,        ///< Handshake didn't complete in time
    AlreadyInitialized = 5,      ///< Client already sent Initialize

    // Session errors (100-199)
    SessionNotFound = 100,       ///< Referenced session doesn't exist
    SessionFull = 101,           ///< Session has maximum players
    SessionClosed = 102,         ///< Session was closed by host
    NotInSession = 103,          ///< Operation requires being in a session
    AlreadyInSession = 104,      ///< Already in a session

    // Network errors (200-299)
    NetworkNotFound = 200,       ///< Requested network doesn't exist
    NetworkFull = 201,           ///< Network is at capacity
    ConnectionRejected = 202,    ///< Host rejected the connection
    AuthenticationFailed = 203,  ///< Passphrase authentication failed
    InvalidRequest = 204,        ///< Malformed or invalid request

    // Internal errors (900-999)
    InternalError = 900,         ///< Server internal error
    ServiceUnavailable = 901     ///< Service temporarily unavailable
};

} // namespace ryu_ldn::protocol
