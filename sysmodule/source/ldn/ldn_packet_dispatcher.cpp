/**
 * @file ldn_packet_dispatcher.cpp
 * @brief Implementation of LDN Packet Dispatcher
 *
 * This file implements the packet dispatching logic that routes incoming
 * protocol packets to their registered callback handlers. The design
 * mirrors the C# RyuLdnProtocol.DecodeAndHandle() method.
 *
 * ## Dispatch Flow
 *
 * 1. Receive packet header + data from network
 * 2. Call dispatch(header, data, size)
 * 3. Dispatcher checks header.type to determine packet kind
 * 4. For typed packets: validates size, copies to struct, calls handler
 * 5. For empty packets (ScanReplyEnd, RejectReply): calls handler directly
 * 6. For ProxyData: special handling with header + variable data
 *
 * ## Error Handling
 *
 * - Invalid/unknown packet types are silently ignored
 * - Undersized packets are silently ignored (no crash, no handler call)
 * - Null handlers result in silent no-op (packet processed but not handled)
 *
 * ## Memory Safety
 *
 * - All payload data is copied into local structs before handler call
 * - Handlers receive const references, cannot modify dispatcher state
 * - No dynamic allocation in dispatch path (stack-based parsing)
 *
 * @see RyuLdnProtocol.cs DecodeAndHandle() for reference implementation
 */

#include "ldn_packet_dispatcher.hpp"
#include <cstring>

namespace ryu_ldn::ldn {

/**
 * @brief Construct a new PacketDispatcher with no handlers registered
 *
 * All handler pointers are initialized to nullptr. Packets received
 * before handlers are registered will be silently ignored.
 */
PacketDispatcher::PacketDispatcher()
    : m_initialize_handler(nullptr)
    , m_connected_handler(nullptr)
    , m_sync_network_handler(nullptr)
    , m_scan_reply_handler(nullptr)
    , m_scan_reply_end_handler(nullptr)
    , m_disconnect_handler(nullptr)
    , m_ping_handler(nullptr)
    , m_network_error_handler(nullptr)
    , m_proxy_config_handler(nullptr)
    , m_proxy_connect_handler(nullptr)
    , m_proxy_connect_reply_handler(nullptr)
    , m_proxy_data_handler(nullptr)
    , m_proxy_disconnect_handler(nullptr)
    , m_reject_handler(nullptr)
    , m_reject_reply_handler(nullptr)
    , m_accept_policy_handler(nullptr)
{
}

// ============================================================================
// Handler Registration
// ============================================================================

/**
 * @brief Register handler for Initialize packets (PacketId::Initialize)
 *
 * Initialize packets are sent by clients after TCP connection to identify
 * themselves. Contains session ID and MAC address.
 *
 * @param handler Function to call when Initialize packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_initialize_handler(PacketHandler<protocol::InitializeMessage> handler) {
    m_initialize_handler = handler;
}

/**
 * @brief Register handler for Connected packets (PacketId::Connected)
 *
 * Connected packets are sent by server to confirm successful join.
 * Contains full NetworkInfo with all session details.
 *
 * @param handler Function to call when Connected packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_connected_handler(PacketHandler<protocol::NetworkInfo> handler) {
    m_connected_handler = handler;
}

/**
 * @brief Register handler for SyncNetwork packets (PacketId::SyncNetwork)
 *
 * SyncNetwork packets are broadcast to all clients when network state
 * changes (player join/leave, host change, etc.).
 *
 * @param handler Function to call when SyncNetwork packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_sync_network_handler(PacketHandler<protocol::NetworkInfo> handler) {
    m_sync_network_handler = handler;
}

/**
 * @brief Register handler for ScanReply packets (PacketId::ScanReply)
 *
 * ScanReply packets are sent by server for each discovered network
 * matching the scan filter. One packet per network.
 *
 * @param handler Function to call when ScanReply packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_scan_reply_handler(PacketHandler<protocol::NetworkInfo> handler) {
    m_scan_reply_handler = handler;
}

/**
 * @brief Register handler for ScanReplyEnd packets (PacketId::ScanReplyEnd)
 *
 * ScanReplyEnd is sent after all ScanReply packets to indicate
 * scan is complete. Has no payload.
 *
 * @param handler Function to call when ScanReplyEnd packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_scan_reply_end_handler(EmptyPacketHandler handler) {
    m_scan_reply_end_handler = handler;
}

/**
 * @brief Register handler for Disconnect packets (PacketId::Disconnect)
 *
 * Disconnect packets are sent when a client leaves the session.
 * Contains the IP address of the disconnecting client.
 *
 * @param handler Function to call when Disconnect packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_disconnect_handler(PacketHandler<protocol::DisconnectMessage> handler) {
    m_disconnect_handler = handler;
}

/**
 * @brief Register handler for Ping packets (PacketId::Ping)
 *
 * Ping packets are used for keepalive and latency measurement.
 * Server sends Ping with requester=0, client must echo it back.
 *
 * @param handler Function to call when Ping packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_ping_handler(PacketHandler<protocol::PingMessage> handler) {
    m_ping_handler = handler;
}

/**
 * @brief Register handler for NetworkError packets (PacketId::NetworkError)
 *
 * NetworkError packets report protocol or session errors.
 * Contains error code indicating the failure type.
 *
 * @param handler Function to call when NetworkError packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_network_error_handler(PacketHandler<protocol::NetworkErrorMessage> handler) {
    m_network_error_handler = handler;
}

/**
 * @brief Register handler for ProxyConfig packets (PacketId::ProxyConfig)
 *
 * ProxyConfig packets configure P2P proxy tunneling.
 * Contains proxy IP and subnet mask for address assignment.
 *
 * @param handler Function to call when ProxyConfig packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_proxy_config_handler(PacketHandler<protocol::ProxyConfig> handler) {
    m_proxy_config_handler = handler;
}

/**
 * @brief Register handler for ProxyConnect packets (PacketId::ProxyConnect)
 *
 * ProxyConnect requests establish P2P connections through the server.
 * Contains source/destination addressing info.
 *
 * @param handler Function to call when ProxyConnect packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_proxy_connect_handler(PacketHandler<protocol::ProxyConnectRequest> handler) {
    m_proxy_connect_handler = handler;
}

/**
 * @brief Register handler for ProxyConnectReply packets (PacketId::ProxyConnectReply)
 *
 * ProxyConnectReply confirms or denies P2P connection request.
 *
 * @param handler Function to call when ProxyConnectReply packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_proxy_connect_reply_handler(PacketHandler<protocol::ProxyConnectResponse> handler) {
    m_proxy_connect_reply_handler = handler;
}

/**
 * @brief Register handler for ProxyData packets (PacketId::ProxyData)
 *
 * ProxyData packets carry game traffic through the server proxy.
 * Contains ProxyDataHeader (20 bytes) followed by variable-length data.
 *
 * @param handler Function to call when ProxyData packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_proxy_data_handler(ProxyDataHandler handler) {
    m_proxy_data_handler = handler;
}

/**
 * @brief Register handler for ProxyDisconnect packets (PacketId::ProxyDisconnect)
 *
 * ProxyDisconnect notifies that a proxied P2P connection was closed.
 * Contains connection info and disconnect reason.
 *
 * @param handler Function to call when ProxyDisconnect packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_proxy_disconnect_handler(PacketHandler<protocol::ProxyDisconnectMessage> handler) {
    m_proxy_disconnect_handler = handler;
}

/**
 * @brief Register handler for Reject packets (PacketId::Reject)
 *
 * Reject packets are sent by host to kick/reject a player.
 * Contains node ID and disconnect reason.
 *
 * @param handler Function to call when Reject packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_reject_handler(PacketHandler<protocol::RejectRequest> handler) {
    m_reject_handler = handler;
}

/**
 * @brief Register handler for RejectReply packets (PacketId::RejectReply)
 *
 * RejectReply confirms that rejection was processed.
 * Has no payload.
 *
 * @param handler Function to call when RejectReply packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_reject_reply_handler(EmptyPacketHandler handler) {
    m_reject_reply_handler = handler;
}

/**
 * @brief Register handler for SetAcceptPolicy packets (PacketId::SetAcceptPolicy)
 *
 * SetAcceptPolicy changes who can join the session.
 * Contains AcceptPolicy enum value.
 *
 * @param handler Function to call when SetAcceptPolicy packet is received.
 *                Pass nullptr to unregister.
 */
void PacketDispatcher::set_accept_policy_handler(PacketHandler<protocol::SetAcceptPolicyRequest> handler) {
    m_accept_policy_handler = handler;
}

// ============================================================================
// Dispatch Implementation
// ============================================================================

/**
 * @brief Helper to safely parse and dispatch typed packets
 *
 * This template method handles the common pattern for most packet types:
 * 1. Check if handler is registered
 * 2. Validate payload size is sufficient
 * 3. Copy data into typed struct (safe alignment)
 * 4. Call handler with struct reference
 *
 * @tparam T The payload struct type (e.g., PingMessage, NetworkInfo)
 * @param header The packet header
 * @param data Pointer to payload bytes
 * @param data_size Size of payload data
 * @param handler Function pointer to call (may be nullptr)
 *
 * @note If handler is nullptr, returns immediately (no-op)
 * @note If data_size < sizeof(T), returns immediately (undersized packet)
 */
template<typename T>
void PacketDispatcher::dispatch_typed(const protocol::LdnHeader& header,
                                       const uint8_t* data,
                                       size_t data_size,
                                       PacketHandler<T> handler) {
    // No handler registered for this packet type
    if (!handler) {
        return;
    }

    // Validate payload size is sufficient for this packet type
    if (data_size < sizeof(T)) {
        // Packet too small, ignore (malformed packet)
        return;
    }

    // Copy data into local struct for safe alignment and lifetime
    // Using memcpy ensures correct handling even if data is unaligned
    T payload;
    std::memcpy(&payload, data, sizeof(T));

    // Invoke the registered handler
    handler(header, payload);
}

/**
 * @brief Dispatch a received packet to the appropriate handler
 *
 * Main dispatch entry point. Routes packets to handlers based on
 * the packet type in header.type.
 *
 * ## Supported Packet Types
 *
 * | PacketId         | Handler Type    | Payload Type               |
 * |------------------|-----------------|----------------------------|
 * | Initialize       | PacketHandler   | InitializeMessage (22B)    |
 * | Connected        | PacketHandler   | NetworkInfo (0x480)        |
 * | SyncNetwork      | PacketHandler   | NetworkInfo (0x480)        |
 * | ScanReply        | PacketHandler   | NetworkInfo (0x480)        |
 * | ScanReplyEnd     | EmptyHandler    | (none)                     |
 * | Disconnect       | PacketHandler   | DisconnectMessage (4B)     |
 * | Ping             | PacketHandler   | PingMessage (2B)           |
 * | NetworkError     | PacketHandler   | NetworkErrorMessage (4B)   |
 * | ProxyConfig      | PacketHandler   | ProxyConfig (8B)           |
 * | ProxyConnect     | PacketHandler   | ProxyConnectRequest (16B)  |
 * | ProxyConnectReply| PacketHandler   | ProxyConnectResponse (16B) |
 * | ProxyData        | ProxyDataHandler| ProxyDataHeader + data     |
 * | ProxyDisconnect  | PacketHandler   | ProxyDisconnectMessage(20B)|
 * | Reject           | PacketHandler   | RejectRequest (8B)         |
 * | RejectReply      | EmptyHandler    | (none)                     |
 * | SetAcceptPolicy  | PacketHandler   | SetAcceptPolicyRequest (4B)|
 *
 * @param header The packet header (already parsed from wire format)
 * @param data Pointer to payload data following header
 * @param data_size Size of payload data in bytes
 *
 * @note Unknown packet types are silently ignored
 * @note Packets with insufficient data are silently ignored
 */
void PacketDispatcher::dispatch(const protocol::LdnHeader& header,
                                 const uint8_t* data,
                                 size_t data_size) {
    using namespace protocol;

    // Route packet based on type
    switch (static_cast<PacketId>(header.type)) {

        // === Session Management Packets ===

        case PacketId::Initialize:
            dispatch_typed(header, data, data_size, m_initialize_handler);
            break;

        case PacketId::Connected:
            dispatch_typed(header, data, data_size, m_connected_handler);
            break;

        case PacketId::SyncNetwork:
            dispatch_typed(header, data, data_size, m_sync_network_handler);
            break;

        // === Network Discovery Packets ===

        case PacketId::ScanReply:
            dispatch_typed(header, data, data_size, m_scan_reply_handler);
            break;

        case PacketId::ScanReplyEnd:
            // Empty packet - no payload validation needed
            if (m_scan_reply_end_handler) {
                m_scan_reply_end_handler(header);
            }
            break;

        case PacketId::Disconnect:
            dispatch_typed(header, data, data_size, m_disconnect_handler);
            break;

        // === Utility Packets ===

        case PacketId::Ping:
            dispatch_typed(header, data, data_size, m_ping_handler);
            break;

        case PacketId::NetworkError:
            dispatch_typed(header, data, data_size, m_network_error_handler);
            break;

        // === Proxy Packets (P2P Tunneling) ===

        case PacketId::ProxyConfig:
            dispatch_typed(header, data, data_size, m_proxy_config_handler);
            break;

        case PacketId::ProxyConnect:
            dispatch_typed(header, data, data_size, m_proxy_connect_handler);
            break;

        case PacketId::ProxyConnectReply:
            dispatch_typed(header, data, data_size, m_proxy_connect_reply_handler);
            break;

        case PacketId::ProxyData:
            // Special handling: header struct + variable-length data
            if (m_proxy_data_handler) {
                // Validate minimum size for header
                if (data_size < sizeof(ProxyDataHeader)) {
                    return;  // Packet too small
                }

                // Parse proxy header
                ProxyDataHeader proxy_header;
                std::memcpy(&proxy_header, data, sizeof(ProxyDataHeader));

                // Calculate data pointer and size
                const uint8_t* proxy_data = data + sizeof(ProxyDataHeader);
                size_t available_data_size = data_size - sizeof(ProxyDataHeader);

                // Validate declared data_length fits in remaining packet
                if (available_data_size < proxy_header.data_length) {
                    return;  // Declared length exceeds actual data
                }

                // Dispatch with validated data
                m_proxy_data_handler(header, proxy_header, proxy_data, proxy_header.data_length);
            }
            break;

        case PacketId::ProxyDisconnect:
            dispatch_typed(header, data, data_size, m_proxy_disconnect_handler);
            break;

        // === Control Packets (Host Actions) ===

        case PacketId::Reject:
            dispatch_typed(header, data, data_size, m_reject_handler);
            break;

        case PacketId::RejectReply:
            // Empty packet - no payload validation needed
            if (m_reject_reply_handler) {
                m_reject_reply_handler(header);
            }
            break;

        case PacketId::SetAcceptPolicy:
            dispatch_typed(header, data, data_size, m_accept_policy_handler);
            break;

        // === Unknown/Unhandled Packets ===

        default:
            // Unknown packet type - silently ignore
            // This allows forward compatibility with future protocol extensions
            break;
    }
}

} // namespace ryu_ldn::ldn
