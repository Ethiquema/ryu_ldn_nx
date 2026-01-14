/**
 * @file ldn_proxy_handler.hpp
 * @brief LDN Proxy Handler - Manages P2P connections tunneled through RyuLDN
 *
 * This module provides a handler for P2P proxy connections. The Nintendo Switch
 * LDN protocol normally uses direct local communication, but when playing online
 * through RyuLDN, connections are tunneled through the server.
 *
 * ## Architecture
 *
 * ```
 * +------------------+     +--------------------+     +------------------+
 * | Game (Switch)    | --> | LdnProxyHandler    | --> | RyuLDN Server    |
 * | send_to(peer)    |     | (tunnel via TCP)   |     | (relay to peer)  |
 * +------------------+     +--------------------+     +------------------+
 * ```
 *
 * ## Connection Model
 *
 * The proxy handler maintains a table of virtual P2P connections:
 * - Each connection is identified by (src_ip, src_port, dest_ip, dest_port, protocol)
 * - ProxyConnect establishes a new virtual connection
 * - ProxyData sends/receives game data through the tunnel
 * - ProxyDisconnect tears down the virtual connection
 *
 * ## Protocol Flow
 *
 * 1. Server sends ProxyConfig with virtual network settings
 * 2. When game wants to connect to peer:
 *    - Client sends ProxyConnect request
 *    - Server relays to peer
 *    - Peer responds with ProxyConnectReply
 * 3. Game data flows through ProxyData packets
 * 4. Connection ends with ProxyDisconnect
 *
 * @see ldn_packet_dispatcher.hpp for packet routing
 * @see protocol/types.hpp for ProxyInfo, ProxyDataHeader structures
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include "../protocol/types.hpp"

namespace ryu_ldn::ldn {

// ============================================================================
// Callback Types
// ============================================================================

/**
 * @brief Callback for proxy configuration received
 *
 * Called when ProxyConfig packet arrives with virtual network settings.
 *
 * @param config Proxy configuration (IP, subnet mask)
 */
using ProxyConfigCallback = void (*)(const protocol::ProxyConfig& config);

/**
 * @brief Callback for incoming proxy connection request
 *
 * Called when a peer wants to establish a P2P connection.
 *
 * @param info Connection info (source/dest addresses)
 */
using ProxyConnectCallback = void (*)(const protocol::ProxyInfo& info);

/**
 * @brief Callback for proxy connection reply
 *
 * Called when the peer responds to our connect request.
 *
 * @param info Connection info
 */
using ProxyConnectReplyCallback = void (*)(const protocol::ProxyInfo& info);

/**
 * @brief Callback for proxy data received
 *
 * Called when game data arrives through the proxy tunnel.
 *
 * @param info Connection info (identifies the sender)
 * @param data Pointer to payload data
 * @param length Length of payload in bytes
 */
using ProxyDataCallback = void (*)(const protocol::ProxyInfo& info,
                                    const uint8_t* data, size_t length);

/**
 * @brief Callback for proxy disconnect
 *
 * Called when a proxied connection is closed.
 *
 * @param info Connection that was closed
 * @param reason Disconnect reason code
 */
using ProxyDisconnectCallback = void (*)(const protocol::ProxyInfo& info, int32_t reason);

// ============================================================================
// Connection Entry
// ============================================================================

/**
 * @brief Entry in the proxy connection table
 *
 * Represents a single virtual P2P connection being tunneled.
 */
struct ProxyConnection {
    uint32_t source_ipv4;           ///< Source IPv4 address
    uint16_t source_port;           ///< Source port
    uint32_t dest_ipv4;             ///< Destination IPv4 address
    uint16_t dest_port;             ///< Destination port
    protocol::ProtocolType protocol; ///< TCP or UDP

    /**
     * @brief Check if this connection matches the given parameters
     */
    bool matches(uint32_t src_ip, uint16_t src_port,
                 uint32_t dst_ip, uint16_t dst_port,
                 protocol::ProtocolType proto = protocol::ProtocolType::Udp) const {
        return source_ipv4 == src_ip && source_port == src_port &&
               dest_ipv4 == dst_ip && dest_port == dst_port &&
               protocol == proto;
    }

    /**
     * @brief Check if this connection matches a ProxyInfo
     */
    bool matches(const protocol::ProxyInfo& info) const {
        return matches(info.source_ipv4, info.source_port,
                       info.dest_ipv4, info.dest_port, info.protocol);
    }
};

// ============================================================================
// LdnProxyHandler Class
// ============================================================================

/**
 * @brief LDN Proxy Handler
 *
 * Manages P2P connections tunneled through the RyuLDN server.
 * Maintains a table of active virtual connections and provides
 * callbacks for connection events and data reception.
 *
 * ## Thread Safety
 *
 * NOT thread-safe. All methods should be called from the same thread.
 *
 * ## Usage Example
 *
 * ```cpp
 * LdnProxyHandler proxy;
 *
 * proxy.set_config_callback([](const ProxyConfig& cfg) {
 *     printf("Proxy IP: %08X\n", cfg.proxy_ip);
 * });
 *
 * proxy.set_data_callback([](const ProxyInfo& info, const uint8_t* data, size_t len) {
 *     // Forward to game
 * });
 *
 * // Handler is connected to packet dispatcher
 * dispatcher.set_proxy_config_handler([&](auto& h, auto& c) {
 *     proxy.handle_proxy_config(h, c);
 * });
 * ```
 */
class LdnProxyHandler {
public:
    /**
     * @brief Default constructor
     *
     * Creates handler in unconfigured state with no connections.
     */
    LdnProxyHandler();

    /**
     * @brief Destructor
     */
    ~LdnProxyHandler() = default;

    // Non-copyable
    LdnProxyHandler(const LdnProxyHandler&) = delete;
    LdnProxyHandler& operator=(const LdnProxyHandler&) = delete;

    // ========================================================================
    // Callback Registration
    // ========================================================================

    /**
     * @brief Set callback for proxy configuration
     *
     * @param callback Function to call when config received (nullptr to disable)
     */
    void set_config_callback(ProxyConfigCallback callback);

    /**
     * @brief Set callback for incoming connect requests
     *
     * @param callback Function to call for connect requests
     */
    void set_connect_callback(ProxyConnectCallback callback);

    /**
     * @brief Set callback for connect replies
     *
     * @param callback Function to call for connect replies
     */
    void set_connect_reply_callback(ProxyConnectReplyCallback callback);

    /**
     * @brief Set callback for proxy data
     *
     * @param callback Function to call when data arrives
     */
    void set_data_callback(ProxyDataCallback callback);

    /**
     * @brief Set callback for disconnect events
     *
     * @param callback Function to call on disconnect
     */
    void set_disconnect_callback(ProxyDisconnectCallback callback);

    // ========================================================================
    // Packet Handlers
    // ========================================================================

    /**
     * @brief Handle ProxyConfig packet
     *
     * Configures the proxy with virtual network settings.
     * Marks the handler as configured.
     *
     * @param header Packet header
     * @param config Proxy configuration
     */
    void handle_proxy_config(const protocol::LdnHeader& header,
                              const protocol::ProxyConfig& config);

    /**
     * @brief Handle ProxyConnect packet
     *
     * Handles incoming P2P connection request from peer.
     * Adds entry to connection table.
     *
     * @param header Packet header
     * @param req Connect request
     */
    void handle_proxy_connect(const protocol::LdnHeader& header,
                               const protocol::ProxyConnectRequest& req);

    /**
     * @brief Handle ProxyConnectReply packet
     *
     * Handles response to our connect request.
     *
     * @param header Packet header
     * @param resp Connect response
     */
    void handle_proxy_connect_reply(const protocol::LdnHeader& header,
                                     const protocol::ProxyConnectResponse& resp);

    /**
     * @brief Handle ProxyData packet
     *
     * Handles game data received through proxy tunnel.
     *
     * @param header Packet header
     * @param data_header Proxy data header
     * @param payload Pointer to payload data
     * @param payload_length Length of payload in bytes
     */
    void handle_proxy_data(const protocol::LdnHeader& header,
                            const protocol::ProxyDataHeader& data_header,
                            const uint8_t* payload, size_t payload_length);

    /**
     * @brief Handle ProxyDisconnect packet
     *
     * Handles notification that a proxied connection was closed.
     * Removes entry from connection table.
     *
     * @param header Packet header
     * @param msg Disconnect message
     */
    void handle_proxy_disconnect(const protocol::LdnHeader& header,
                                  const protocol::ProxyDisconnectMessage& msg);

    // ========================================================================
    // State Queries
    // ========================================================================

    /**
     * @brief Check if proxy is configured
     *
     * @return true if ProxyConfig has been received
     */
    bool is_configured() const { return m_configured; }

    /**
     * @brief Get configured proxy IP
     *
     * @return Proxy server IP address (0 if not configured)
     */
    uint32_t get_proxy_ip() const { return m_proxy_ip; }

    /**
     * @brief Get configured subnet mask
     *
     * @return Proxy subnet mask (0 if not configured)
     */
    uint32_t get_proxy_subnet_mask() const { return m_proxy_subnet_mask; }

    /**
     * @brief Get number of active connections
     *
     * @return Count of entries in connection table
     */
    size_t get_connection_count() const { return m_connections.size(); }

    /**
     * @brief Check if a connection exists
     *
     * @param src_ip Source IPv4 address
     * @param src_port Source port
     * @param dest_ip Destination IPv4 address
     * @param dest_port Destination port
     * @param proto Protocol type (default UDP)
     * @return true if connection exists in table
     */
    bool has_connection(uint32_t src_ip, uint16_t src_port,
                        uint32_t dest_ip, uint16_t dest_port,
                        protocol::ProtocolType proto = protocol::ProtocolType::Udp) const;

    // ========================================================================
    // Actions
    // ========================================================================

    /**
     * @brief Reset handler to initial state
     *
     * Clears configuration and all connections.
     * Does not clear callbacks.
     */
    void reset();

private:
    // ========================================================================
    // Internal State
    // ========================================================================

    bool m_configured;                ///< Whether ProxyConfig received
    uint32_t m_proxy_ip;              ///< Configured proxy IP
    uint32_t m_proxy_subnet_mask;     ///< Configured subnet mask

    std::vector<ProxyConnection> m_connections; ///< Active connection table

    // Callbacks
    ProxyConfigCallback m_config_callback;
    ProxyConnectCallback m_connect_callback;
    ProxyConnectReplyCallback m_connect_reply_callback;
    ProxyDataCallback m_data_callback;
    ProxyDisconnectCallback m_disconnect_callback;

    // ========================================================================
    // Internal Methods
    // ========================================================================

    /**
     * @brief Add a connection to the table
     *
     * @param info Connection info
     */
    void add_connection(const protocol::ProxyInfo& info);

    /**
     * @brief Remove a connection from the table
     *
     * @param info Connection info
     */
    void remove_connection(const protocol::ProxyInfo& info);
};

} // namespace ryu_ldn::ldn
