/**
 * @file ldn_proxy_handler.cpp
 * @brief Implementation of LDN Proxy Handler
 *
 * This file implements the P2P proxy management logic that handles
 * virtual network connections tunneled through the RyuLDN server.
 *
 * ## Connection Table
 *
 * The handler maintains a vector of ProxyConnection entries. Each entry
 * uniquely identifies a virtual P2P connection by:
 * - Source IP and port
 * - Destination IP and port
 * - Protocol type (TCP/UDP)
 *
 * This allows multiple simultaneous connections between the same hosts
 * on different ports or protocols.
 *
 * ## Data Flow
 *
 * ```
 * Game sends packet to peer:
 *     Game -> LDN Service -> ProxyHandler -> Server -> Peer
 *
 * Game receives packet from peer:
 *     Peer -> Server -> ProxyHandler -> data_callback -> Game
 * ```
 *
 * @see ldn_proxy_handler.hpp for interface documentation
 */

#include "ldn_proxy_handler.hpp"
#include <algorithm>

namespace ryu_ldn::ldn {

// ============================================================================
// Constructor
// ============================================================================

/**
 * @brief Construct a new LdnProxyHandler
 *
 * Initializes handler in unconfigured state with empty connection table.
 * No callbacks are registered initially.
 */
LdnProxyHandler::LdnProxyHandler()
    : m_configured(false)
    , m_proxy_ip(0)
    , m_proxy_subnet_mask(0)
    , m_connections()
    , m_config_callback(nullptr)
    , m_connect_callback(nullptr)
    , m_connect_reply_callback(nullptr)
    , m_data_callback(nullptr)
    , m_disconnect_callback(nullptr)
{
}

// ============================================================================
// Callback Registration
// ============================================================================

/**
 * @brief Set callback for proxy configuration
 *
 * The callback is invoked when ProxyConfig packet is received.
 * Use this to initialize the virtual network interface.
 *
 * @param callback Function pointer or nullptr to disable
 */
void LdnProxyHandler::set_config_callback(ProxyConfigCallback callback) {
    m_config_callback = callback;
}

/**
 * @brief Set callback for incoming connect requests
 *
 * Called when a peer initiates a P2P connection to us.
 * The application should prepare to receive data from this peer.
 *
 * @param callback Function pointer or nullptr to disable
 */
void LdnProxyHandler::set_connect_callback(ProxyConnectCallback callback) {
    m_connect_callback = callback;
}

/**
 * @brief Set callback for connect replies
 *
 * Called when a peer responds to our connect request.
 * A successful reply means the connection is established.
 *
 * @param callback Function pointer or nullptr to disable
 */
void LdnProxyHandler::set_connect_reply_callback(ProxyConnectReplyCallback callback) {
    m_connect_reply_callback = callback;
}

/**
 * @brief Set callback for proxy data
 *
 * Called when game data arrives through the proxy tunnel.
 * This is the main data path for P2P communication.
 *
 * @param callback Function pointer or nullptr to disable
 */
void LdnProxyHandler::set_data_callback(ProxyDataCallback callback) {
    m_data_callback = callback;
}

/**
 * @brief Set callback for disconnect events
 *
 * Called when a proxied connection is closed by peer or server.
 * The application should clean up resources for this connection.
 *
 * @param callback Function pointer or nullptr to disable
 */
void LdnProxyHandler::set_disconnect_callback(ProxyDisconnectCallback callback) {
    m_disconnect_callback = callback;
}

// ============================================================================
// Packet Handlers
// ============================================================================

/**
 * @brief Handle ProxyConfig packet
 *
 * Server sends ProxyConfig when we join a session to tell us
 * the virtual network settings:
 * - proxy_ip: Our assigned IP on the virtual network
 * - proxy_subnet_mask: Subnet mask for the virtual network
 *
 * This information is used to configure the virtual network interface
 * that the game communicates through.
 *
 * @param header Packet header (unused but kept for consistency)
 * @param config Proxy configuration with network settings
 */
void LdnProxyHandler::handle_proxy_config(const protocol::LdnHeader& header,
                                           const protocol::ProxyConfig& config) {
    (void)header;  // Unused

    // Store configuration
    m_proxy_ip = config.proxy_ip;
    m_proxy_subnet_mask = config.proxy_subnet_mask;
    m_configured = true;

    // Notify application
    if (m_config_callback) {
        m_config_callback(config);
    }
}

/**
 * @brief Handle ProxyConnect packet
 *
 * Server sends ProxyConnect when a peer wants to establish a
 * P2P connection to us. The info contains:
 * - source_ipv4/port: Peer's address on virtual network
 * - dest_ipv4/port: Our address (where peer is connecting)
 * - protocol: TCP or UDP
 *
 * We add this to our connection table to track the active connection.
 *
 * @param header Packet header (unused)
 * @param req Connect request with connection info
 */
void LdnProxyHandler::handle_proxy_connect(const protocol::LdnHeader& header,
                                            const protocol::ProxyConnectRequest& req) {
    (void)header;  // Unused

    // Add connection to table
    add_connection(req.info);

    // Notify application
    if (m_connect_callback) {
        m_connect_callback(req.info);
    }
}

/**
 * @brief Handle ProxyConnectReply packet
 *
 * Server sends ProxyConnectReply in response to our connect request.
 * The info echoes back the connection details, confirming establishment.
 *
 * Note: We don't add to connection table here because we already
 * added it when we sent the connect request.
 *
 * @param header Packet header (unused)
 * @param resp Connect response with connection info
 */
void LdnProxyHandler::handle_proxy_connect_reply(const protocol::LdnHeader& header,
                                                  const protocol::ProxyConnectResponse& resp) {
    (void)header;  // Unused

    // Notify application
    if (m_connect_reply_callback) {
        m_connect_reply_callback(resp.info);
    }
}

/**
 * @brief Handle ProxyData packet
 *
 * Server relays game data from peers through ProxyData packets.
 * Each packet contains:
 * - data_header.info: Connection info (identifies sender)
 * - data_header.data_length: Length of payload
 * - payload: Actual game data
 *
 * The application callback receives the raw payload to forward
 * to the game's virtual network interface.
 *
 * @param header Packet header (unused)
 * @param data_header Proxy data header with connection info and length
 * @param payload Pointer to game data (may be nullptr if length is 0)
 * @param payload_length Length of payload in bytes
 */
void LdnProxyHandler::handle_proxy_data(const protocol::LdnHeader& header,
                                         const protocol::ProxyDataHeader& data_header,
                                         const uint8_t* payload, size_t payload_length) {
    (void)header;  // Unused

    // Notify application with data
    if (m_data_callback) {
        m_data_callback(data_header.info, payload, payload_length);
    }
}

/**
 * @brief Handle ProxyDisconnect packet
 *
 * Server sends ProxyDisconnect when a P2P connection is closed:
 * - Peer closed the connection
 * - Network error
 * - Session ended
 *
 * We remove the connection from our table and notify the application.
 *
 * @param header Packet header (unused)
 * @param msg Disconnect message with connection info and reason
 */
void LdnProxyHandler::handle_proxy_disconnect(const protocol::LdnHeader& header,
                                               const protocol::ProxyDisconnectMessage& msg) {
    (void)header;  // Unused

    // Remove connection from table
    remove_connection(msg.info);

    // Notify application
    if (m_disconnect_callback) {
        m_disconnect_callback(msg.info, msg.disconnect_reason);
    }
}

// ============================================================================
// State Queries
// ============================================================================

/**
 * @brief Check if a connection exists in the table
 *
 * Searches the connection table for an entry matching all parameters.
 * Note that protocol type matters - TCP and UDP connections are distinct.
 *
 * @param src_ip Source IPv4 address
 * @param src_port Source port
 * @param dest_ip Destination IPv4 address
 * @param dest_port Destination port
 * @param proto Protocol type (default UDP)
 * @return true if a matching connection exists
 */
bool LdnProxyHandler::has_connection(uint32_t src_ip, uint16_t src_port,
                                      uint32_t dest_ip, uint16_t dest_port,
                                      protocol::ProtocolType proto) const {
    for (const auto& conn : m_connections) {
        if (conn.matches(src_ip, src_port, dest_ip, dest_port, proto)) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Actions
// ============================================================================

/**
 * @brief Reset handler to initial state
 *
 * Clears:
 * - Configuration (proxy_ip, subnet_mask)
 * - All connections in the table
 *
 * Does NOT clear callbacks - they persist across resets.
 */
void LdnProxyHandler::reset() {
    m_configured = false;
    m_proxy_ip = 0;
    m_proxy_subnet_mask = 0;
    m_connections.clear();
}

// ============================================================================
// Internal Methods
// ============================================================================

/**
 * @brief Add a connection to the table
 *
 * Creates a new ProxyConnection entry from the given ProxyInfo
 * and appends it to the connection vector.
 *
 * Note: Does not check for duplicates. The server should not send
 * duplicate connect requests, but if it does we'll have duplicates.
 *
 * @param info Connection info from ProxyConnect packet
 */
void LdnProxyHandler::add_connection(const protocol::ProxyInfo& info) {
    ProxyConnection conn;
    conn.source_ipv4 = info.source_ipv4;
    conn.source_port = info.source_port;
    conn.dest_ipv4 = info.dest_ipv4;
    conn.dest_port = info.dest_port;
    conn.protocol = info.protocol;

    m_connections.push_back(conn);
}

/**
 * @brief Remove a connection from the table
 *
 * Searches for and removes the first matching connection entry.
 * Uses std::remove_if with erase to efficiently remove from vector.
 *
 * If no matching connection is found, does nothing (no error).
 *
 * @param info Connection info identifying which connection to remove
 */
void LdnProxyHandler::remove_connection(const protocol::ProxyInfo& info) {
    auto it = std::remove_if(m_connections.begin(), m_connections.end(),
        [&info](const ProxyConnection& conn) {
            return conn.matches(info);
        });

    m_connections.erase(it, m_connections.end());
}

} // namespace ryu_ldn::ldn
