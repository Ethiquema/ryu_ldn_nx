/**
 * @file ldn_packet_dispatcher.hpp
 * @brief LDN Packet Dispatcher - Routes packets to handlers by type
 *
 * This module provides a dispatcher that routes incoming protocol packets
 * to registered callback handlers based on PacketId.
 *
 * ## Architecture (matching RyuLdnProtocol.cs)
 *
 * The dispatcher follows the same event-based pattern as the C# server:
 * - Register handlers for specific packet types
 * - Call dispatch() with received data
 * - Dispatcher parses and routes to appropriate handler
 *
 * ## Usage Example
 *
 * ```cpp
 * PacketDispatcher dispatcher;
 *
 * dispatcher.set_ping_handler([](const LdnHeader& h, const PingMessage& m) {
 *     // Handle ping
 * });
 *
 * dispatcher.set_connected_handler([](const LdnHeader& h, const NetworkInfo& i) {
 *     // Handle connection confirmation
 * });
 *
 * // In receive loop:
 * dispatcher.dispatch(header, payload_data, payload_size);
 * ```
 *
 * @see RyuLdnProtocol.cs for reference implementation
 */

#pragma once

#include <cstdint>
#include <cstddef>

#include "../protocol/types.hpp"

namespace ryu_ldn::ldn {

/**
 * @brief Handler type for packets with payload struct
 */
template<typename T>
using PacketHandler = void (*)(const protocol::LdnHeader& header, const T& payload);

/**
 * @brief Handler type for packets with no payload
 */
using EmptyPacketHandler = void (*)(const protocol::LdnHeader& header);

/**
 * @brief Handler type for ProxyData packets (header + variable data)
 */
using ProxyDataHandler = void (*)(const protocol::LdnHeader& header,
                                   const protocol::ProxyDataHeader& proxy_header,
                                   const uint8_t* data,
                                   size_t data_size);

/**
 * @brief Packet dispatcher for routing received packets to handlers
 *
 * Routes incoming packets to registered callback handlers based on
 * the packet type in the LdnHeader.
 *
 * ## Thread Safety
 *
 * NOT thread-safe. Do not call dispatch() from multiple threads
 * simultaneously. Handler registration should be done before
 * starting the receive loop.
 */
class PacketDispatcher {
public:
    PacketDispatcher();
    ~PacketDispatcher() = default;

    // Non-copyable
    PacketDispatcher(const PacketDispatcher&) = delete;
    PacketDispatcher& operator=(const PacketDispatcher&) = delete;

    // ========================================================================
    // Handler Registration
    // ========================================================================

    /**
     * @brief Set handler for Initialize packets
     */
    void set_initialize_handler(PacketHandler<protocol::InitializeMessage> handler);

    /**
     * @brief Set handler for Connected packets (join success)
     */
    void set_connected_handler(PacketHandler<protocol::NetworkInfo> handler);

    /**
     * @brief Set handler for SyncNetwork packets
     */
    void set_sync_network_handler(PacketHandler<protocol::NetworkInfo> handler);

    /**
     * @brief Set handler for ScanReply packets
     */
    void set_scan_reply_handler(PacketHandler<protocol::NetworkInfo> handler);

    /**
     * @brief Set handler for ScanReplyEnd packets
     */
    void set_scan_reply_end_handler(EmptyPacketHandler handler);

    /**
     * @brief Set handler for Disconnect packets
     */
    void set_disconnect_handler(PacketHandler<protocol::DisconnectMessage> handler);

    /**
     * @brief Set handler for Ping packets
     */
    void set_ping_handler(PacketHandler<protocol::PingMessage> handler);

    /**
     * @brief Set handler for NetworkError packets
     */
    void set_network_error_handler(PacketHandler<protocol::NetworkErrorMessage> handler);

    /**
     * @brief Set handler for ProxyConfig packets
     */
    void set_proxy_config_handler(PacketHandler<protocol::ProxyConfig> handler);

    /**
     * @brief Set handler for ProxyConnect packets
     */
    void set_proxy_connect_handler(PacketHandler<protocol::ProxyConnectRequest> handler);

    /**
     * @brief Set handler for ProxyConnectReply packets
     */
    void set_proxy_connect_reply_handler(PacketHandler<protocol::ProxyConnectResponse> handler);

    /**
     * @brief Set handler for ProxyData packets
     */
    void set_proxy_data_handler(ProxyDataHandler handler);

    /**
     * @brief Set handler for ProxyDisconnect packets
     */
    void set_proxy_disconnect_handler(PacketHandler<protocol::ProxyDisconnectMessage> handler);

    /**
     * @brief Set handler for Reject packets
     */
    void set_reject_handler(PacketHandler<protocol::RejectRequest> handler);

    /**
     * @brief Set handler for RejectReply packets
     */
    void set_reject_reply_handler(EmptyPacketHandler handler);

    /**
     * @brief Set handler for SetAcceptPolicy packets
     */
    void set_accept_policy_handler(PacketHandler<protocol::SetAcceptPolicyRequest> handler);

    // ========================================================================
    // Dispatch
    // ========================================================================

    /**
     * @brief Dispatch a received packet to the appropriate handler
     *
     * Parses the packet data and calls the registered handler for the
     * packet type. If no handler is registered, the packet is silently
     * ignored.
     *
     * @param header The packet header (already parsed)
     * @param data Pointer to payload data (after header)
     * @param data_size Size of payload data in bytes
     *
     * @note If data_size is smaller than required for the packet type,
     *       the packet is silently ignored (invalid packet).
     */
    void dispatch(const protocol::LdnHeader& header, const uint8_t* data, size_t data_size);

private:
    // Handler pointers (nullptr = not registered)
    PacketHandler<protocol::InitializeMessage>       m_initialize_handler;
    PacketHandler<protocol::NetworkInfo>             m_connected_handler;
    PacketHandler<protocol::NetworkInfo>             m_sync_network_handler;
    PacketHandler<protocol::NetworkInfo>             m_scan_reply_handler;
    EmptyPacketHandler                               m_scan_reply_end_handler;
    PacketHandler<protocol::DisconnectMessage>       m_disconnect_handler;
    PacketHandler<protocol::PingMessage>             m_ping_handler;
    PacketHandler<protocol::NetworkErrorMessage>     m_network_error_handler;
    PacketHandler<protocol::ProxyConfig>             m_proxy_config_handler;
    PacketHandler<protocol::ProxyConnectRequest>     m_proxy_connect_handler;
    PacketHandler<protocol::ProxyConnectResponse>    m_proxy_connect_reply_handler;
    ProxyDataHandler                                 m_proxy_data_handler;
    PacketHandler<protocol::ProxyDisconnectMessage>  m_proxy_disconnect_handler;
    PacketHandler<protocol::RejectRequest>           m_reject_handler;
    EmptyPacketHandler                               m_reject_reply_handler;
    PacketHandler<protocol::SetAcceptPolicyRequest>  m_accept_policy_handler;

    /**
     * @brief Helper to safely parse and dispatch typed packets
     */
    template<typename T>
    void dispatch_typed(const protocol::LdnHeader& header,
                        const uint8_t* data,
                        size_t data_size,
                        PacketHandler<T> handler);
};

} // namespace ryu_ldn::ldn