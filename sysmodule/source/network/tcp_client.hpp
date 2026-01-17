/**
 * @file tcp_client.hpp
 * @brief TCP Client for RyuLdn Protocol Communication
 *
 * This module provides a high-level TCP client that combines the Socket
 * wrapper with the RyuLdn protocol encoder/decoder. It handles:
 *
 * - Connection management (connect, reconnect, disconnect)
 * - Protocol message encoding and sending
 * - Protocol message receiving and decoding
 * - Packet buffering for TCP stream reassembly
 * - Ping/keepalive handling
 *
 * ## Architecture
 *
 * ```
 * +----------------+     +---------------+     +----------------+
 * |   TcpClient    | --> |    Socket     | --> |   TCP/IP       |
 * | (protocol)     |     | (transport)   |     |   Network      |
 * +----------------+     +---------------+     +----------------+
 *        |
 *        v
 * +----------------+
 * | PacketBuffer   |
 * | (reassembly)   |
 * +----------------+
 * ```
 *
 * ## Thread Safety
 *
 * TcpClient is NOT thread-safe. If multiple threads need to use the
 * client, external synchronization is required.
 *
 * ## Usage Example
 *
 * @code
 * #include "network/tcp_client.hpp"
 *
 * using namespace ryu_ldn::network;
 *
 * TcpClient client;
 *
 * // Connect to server
 * if (client.connect("ldn.ryujinx.app", 30456, 5000) == ClientResult::Success) {
 *     // Send initialize message
 *     InitializeMessage init{};
 *     client.send_initialize(init);
 *
 *     // Receive response
 *     PacketId type;
 *     uint8_t payload[1024];
 *     size_t payload_size;
 *     if (client.receive_packet(type, payload, sizeof(payload), payload_size, 5000)
 *         == ClientResult::Success) {
 *         // Handle response...
 *     }
 *
 *     client.disconnect();
 * }
 * @endcode
 *
 * @see socket.hpp for low-level socket operations
 * @see protocol/ryu_protocol.hpp for message encoding/decoding
 * @see protocol/packet_buffer.hpp for TCP stream reassembly
 * @see Epic 2, Story 2.3 for requirements
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <utility>

#include "socket.hpp"
#include "protocol/ryu_protocol.hpp"
#include "protocol/packet_buffer.hpp"

namespace ryu_ldn::network {

// =============================================================================
// Result Codes
// =============================================================================

/**
 * @brief Result codes for TcpClient operations
 *
 * These codes provide more context than raw socket errors by combining
 * transport-level and protocol-level error conditions.
 */
enum class ClientResult {
    Success = 0,           ///< Operation completed successfully

    // Connection errors
    NotConnected,          ///< Client is not connected to server
    AlreadyConnected,      ///< Client is already connected
    ConnectionFailed,      ///< Failed to establish connection
    ConnectionLost,        ///< Connection was lost during operation
    Timeout,               ///< Operation timed out

    // Protocol errors
    InvalidPacket,         ///< Received packet failed validation
    ProtocolError,         ///< Protocol-level error (version mismatch, etc.)
    BufferTooSmall,        ///< Provided buffer too small for data
    EncodingError,         ///< Failed to encode outgoing packet

    // Resource errors
    NotInitialized,        ///< Socket subsystem not initialized
    InternalError          ///< Unexpected internal error
};

/**
 * @brief Convert ClientResult to human-readable string
 *
 * @param result ClientResult enum value
 * @return Static string describing the result
 */
inline const char* client_result_to_string(ClientResult result) {
    switch (result) {
        case ClientResult::Success:          return "Success";
        case ClientResult::NotConnected:     return "NotConnected";
        case ClientResult::AlreadyConnected: return "AlreadyConnected";
        case ClientResult::ConnectionFailed: return "ConnectionFailed";
        case ClientResult::ConnectionLost:   return "ConnectionLost";
        case ClientResult::Timeout:          return "Timeout";
        case ClientResult::InvalidPacket:    return "InvalidPacket";
        case ClientResult::ProtocolError:    return "ProtocolError";
        case ClientResult::BufferTooSmall:   return "BufferTooSmall";
        case ClientResult::EncodingError:    return "EncodingError";
        case ClientResult::NotInitialized:   return "NotInitialized";
        case ClientResult::InternalError:    return "InternalError";
        default:                             return "Unknown";
    }
}

// =============================================================================
// TcpClient Class
// =============================================================================

/**
 * @brief High-level TCP client for RyuLdn protocol
 *
 * Combines Socket transport with protocol encoding/decoding and
 * packet buffering for stream reassembly.
 *
 * ## Lifecycle
 * 1. Create TcpClient instance
 * 2. Call connect() to establish connection
 * 3. Send/receive messages using send_* and receive_packet methods
 * 4. Call disconnect() when done
 *
 * ## Reconnection
 * After disconnection (intentional or due to error), call connect()
 * again to re-establish connection. Previous session state is NOT
 * preserved - send Initialize again after reconnecting.
 */
class TcpClient {
public:
    /**
     * @brief Default constructor
     *
     * Creates a disconnected client. Call connect() to establish connection.
     */
    TcpClient();

    /**
     * @brief Destructor
     *
     * Automatically disconnects if connected.
     */
    ~TcpClient();

    // Non-copyable
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    // Moveable
    TcpClient(TcpClient&& other) noexcept;
    TcpClient& operator=(TcpClient&& other) noexcept;

    // =========================================================================
    // Connection Management
    // =========================================================================

    /**
     * @brief Connect to RyuLdn server
     *
     * Establishes TCP connection to the specified server. After successful
     * connection, the client is ready to send/receive protocol messages.
     *
     * @param host Server hostname or IP address
     * @param port Server port number (default: 30456)
     * @param timeout_ms Connection timeout in milliseconds (0 = blocking)
     *
     * @return ClientResult::Success on successful connection
     * @return ClientResult::AlreadyConnected if already connected
     * @return ClientResult::NotInitialized if socket subsystem not ready
     * @return ClientResult::ConnectionFailed if connection could not be established
     * @return ClientResult::Timeout if connection timed out
     *
     * @note After connection, you should send InitializeMessage
     * @note Recommended timeout: 5000ms (5 seconds)
     */
    ClientResult connect(const char* host, uint16_t port, uint32_t timeout_ms = 5000);

    /**
     * @brief Disconnect from server
     *
     * Closes the connection and resets internal state. Safe to call
     * even if not connected (no-op in that case).
     *
     * @note Does NOT send Disconnect message to server - call send_disconnect()
     *       first if you want to notify the server gracefully.
     */
    void disconnect();

    /**
     * @brief Check if client is connected
     *
     * @return true if TCP connection is established
     * @return false if disconnected or connection was lost
     *
     * @note This only checks local state. The server may have closed the
     *       connection; this will be detected on next send/receive.
     */
    bool is_connected() const;

    // =========================================================================
    // Send Operations
    // =========================================================================

    /**
     * @brief Send a raw protocol packet
     *
     * Low-level send function for any packet type. The packet is encoded
     * with the protocol header and sent over the socket.
     *
     * @param type Packet type (PacketId enum)
     * @param payload Packet payload data (can be nullptr if payload_size is 0)
     * @param payload_size Size of payload in bytes
     *
     * @return ClientResult::Success if packet was sent
     * @return ClientResult::NotConnected if not connected
     * @return ClientResult::EncodingError if packet encoding failed
     * @return ClientResult::ConnectionLost if send failed
     */
    ClientResult send_packet(protocol::PacketId type, const void* payload, size_t payload_size);

    /**
     * @brief Send raw pre-encoded data
     *
     * Sends data that has already been encoded with protocol header.
     * Used by P2P components to forward notifications.
     *
     * @param data Pre-encoded packet data
     * @param size Size of data in bytes
     *
     * @return ClientResult::Success if sent
     * @return ClientResult::NotConnected if not connected
     * @return ClientResult::ConnectionLost if send failed
     */
    ClientResult send_raw(const void* data, size_t size);

    /**
     * @brief Send Initialize message
     *
     * First message to send after connecting. Identifies the client to
     * the server with session ID and MAC address.
     *
     * @param msg Initialize message with client ID and MAC
     * @return ClientResult indicating success or error
     *
     * @note Send with zeros for id/mac to request new session from server
     */
    ClientResult send_initialize(const protocol::InitializeMessage& msg);

    /**
     * @brief Send Passphrase message
     *
     * Sent when joining a private (password-protected) room.
     *
     * @param msg Passphrase message with room password
     * @return ClientResult indicating success or error
     */
    ClientResult send_passphrase(const protocol::PassphraseMessage& msg);

    /**
     * @brief Send Passphrase message
     *
     * Sends passphrase for room filtering. Must be sent after TCP
     * connection and before Initialize packet.
     *
     * @param passphrase Passphrase string (null-terminated)
     * @return ClientResult indicating success or error
     */
    ClientResult send_passphrase(const char* passphrase);

    /**
     * @brief Send Ping message
     *
     * Keepalive message for connection health check.
     *
     * @param msg Ping message with requester and id
     * @return ClientResult indicating success or error
     *
     * @note Server will echo back the ping when requester=0
     */
    ClientResult send_ping(const protocol::PingMessage& msg);

    /**
     * @brief Send Disconnect message
     *
     * Notifies server that client is leaving the session gracefully.
     *
     * @param msg Disconnect message with reason code
     * @return ClientResult indicating success or error
     *
     * @note Call this before disconnect() for graceful shutdown
     */
    ClientResult send_disconnect(const protocol::DisconnectMessage& msg);

    /**
     * @brief Send CreateAccessPoint request
     *
     * Request to create a new network session (host mode).
     *
     * @param request Access point configuration
     * @return ClientResult indicating success or error
     */
    ClientResult send_create_access_point(const protocol::CreateAccessPointRequest& request);

    /**
     * @brief Send CreateAccessPointPrivate request
     *
     * Request to create a new private (password-protected) network session.
     *
     * @param request Private access point configuration
     * @param advertise_data Optional advertise data
     * @param advertise_size Size of advertise data
     * @return ClientResult indicating success or error
     */
    ClientResult send_create_access_point_private(const protocol::CreateAccessPointPrivateRequest& request,
                                                   const uint8_t* advertise_data = nullptr,
                                                   size_t advertise_size = 0);

    /**
     * @brief Send Connect request
     *
     * Request to join an existing network session.
     *
     * @param request Connection request with target network info
     * @return ClientResult indicating success or error
     */
    ClientResult send_connect(const protocol::ConnectRequest& request);

    /**
     * @brief Send ConnectPrivate request
     *
     * Request to join a private (password-protected) network session.
     *
     * @param request Private connection request
     * @return ClientResult indicating success or error
     */
    ClientResult send_connect_private(const protocol::ConnectPrivateRequest& request);

    /**
     * @brief Send Scan request
     *
     * Request to scan for available network sessions.
     *
     * @param filter Scan filter criteria
     * @return ClientResult indicating success or error
     */
    ClientResult send_scan(const protocol::ScanFilterFull& filter);

    /**
     * @brief Send proxy data
     *
     * Send game data through the proxy to another player.
     *
     * @param header Proxy header with source/destination nodes
     * @param data Game data to send
     * @param data_size Size of game data
     * @return ClientResult indicating success or error
     */
    ClientResult send_proxy_data(const protocol::ProxyDataHeader& header,
                                  const uint8_t* data, size_t data_size);

    /**
     * @brief Send SetAcceptPolicy request
     *
     * Host-only command to change the accept policy for new connections.
     *
     * @param request Accept policy request
     * @return ClientResult indicating success or error
     */
    ClientResult send_set_accept_policy(const protocol::SetAcceptPolicyRequest& request);

    /**
     * @brief Send SetAdvertiseData request
     *
     * Host-only command to update the advertise data for the network.
     *
     * @param data Advertise data buffer
     * @param size Size of advertise data (max 384 bytes)
     * @return ClientResult indicating success or error
     */
    ClientResult send_set_advertise_data(const uint8_t* data, size_t size);

    /**
     * @brief Send Reject request
     *
     * Host-only command to reject/kick a player from the network.
     *
     * @param request Reject request with node ID and reason
     * @return ClientResult indicating success or error
     */
    ClientResult send_reject(const protocol::RejectRequest& request);

    // =========================================================================
    // Receive Operations
    // =========================================================================

    /**
     * @brief Receive next protocol packet
     *
     * Waits for and receives the next complete protocol packet from the server.
     * Handles TCP stream reassembly internally using PacketBuffer.
     *
     * @param[out] type Packet type of received packet
     * @param[out] payload Buffer to receive payload data
     * @param payload_buffer_size Size of payload buffer
     * @param[out] payload_size Actual size of received payload
     * @param timeout_ms Receive timeout in milliseconds (0 = non-blocking, -1 = blocking)
     *
     * @return ClientResult::Success if packet was received
     * @return ClientResult::NotConnected if not connected
     * @return ClientResult::Timeout if no packet within timeout
     * @return ClientResult::BufferTooSmall if payload doesn't fit in buffer
     * @return ClientResult::InvalidPacket if received invalid packet
     * @return ClientResult::ConnectionLost if connection was lost
     *
     * @note payload_size is set even on BufferTooSmall (indicates required size)
     */
    ClientResult receive_packet(protocol::PacketId& type,
                                 void* payload, size_t payload_buffer_size,
                                 size_t& payload_size,
                                 int32_t timeout_ms = -1);

    /**
     * @brief Check if a complete packet is available
     *
     * Non-blocking check for packet availability. Use this to poll for
     * packets without blocking.
     *
     * @return true if at least one complete packet is buffered
     * @return false if no complete packet available
     *
     * @note Call receive_packet() after this returns true to get the packet
     */
    bool has_packet_available() const;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Enable TCP_NODELAY (disable Nagle's algorithm)
     *
     * Reduces latency for small packets at the cost of slightly higher
     * network overhead. Recommended for gaming applications.
     *
     * @param enable true to disable Nagle, false to enable
     * @return ClientResult::Success on success
     * @return ClientResult::NotConnected if not connected
     */
    ClientResult set_nodelay(bool enable);

private:
    Socket m_socket;                                 ///< Underlying TCP socket
    protocol::PacketBuffer<0x2000> m_recv_buffer;    ///< Buffer for TCP stream reassembly (8KB - saves 56KB!)
    uint8_t m_send_buffer[2048];                     ///< Buffer for encoding outgoing packets

    /**
     * @brief Convert SocketResult to ClientResult
     *
     * Maps low-level socket errors to higher-level client results.
     *
     * @param socket_result Socket operation result
     * @return Corresponding ClientResult
     */
    static ClientResult socket_to_client_result(SocketResult socket_result);

    /**
     * @brief Try to receive more data into packet buffer
     *
     * Reads available data from socket into the packet buffer.
     *
     * @param timeout_ms Receive timeout
     * @return ClientResult::Success if data received or would block
     * @return Error result on connection failure
     */
    ClientResult receive_into_buffer(int32_t timeout_ms);
};

} // namespace ryu_ldn::network
