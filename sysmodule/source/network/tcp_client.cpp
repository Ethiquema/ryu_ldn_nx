/**
 * @file tcp_client.cpp
 * @brief TCP Client Implementation for RyuLdn Protocol
 *
 * Implements the TcpClient class which provides high-level protocol
 * communication over TCP sockets.
 *
 * ## Implementation Notes
 *
 * ### Packet Buffering
 * TCP is a stream protocol - data may arrive in fragments or multiple
 * packets may arrive together. The PacketBuffer handles reassembly:
 * 1. Raw TCP data is appended to the buffer
 * 2. Buffer is checked for complete packets (header + payload)
 * 3. Complete packets are extracted and returned to caller
 *
 * ### Send Buffer
 * A fixed 2KB send buffer is used for encoding outgoing packets.
 * This is sufficient for all protocol messages (largest is ~1.3KB).
 * The buffer avoids dynamic allocation on the critical send path.
 *
 * ### Error Handling
 * - Socket errors are mapped to ClientResult for consistency
 * - Protocol errors (invalid packets) are detected during receive
 * - Connection loss is detected and reported appropriately
 *
 * @see tcp_client.hpp for the public interface
 */

#include "tcp_client.hpp"
#include "../debug/log.hpp"
#include <cstring>

namespace ryu_ldn::network {

// =============================================================================
// Constructor / Destructor
// =============================================================================

/**
 * @brief Default constructor - creates disconnected client
 *
 * Initializes internal buffers. No connection is established.
 */
TcpClient::TcpClient()
    : m_socket()
    , m_recv_buffer()
{
    // Send buffer is uninitialized - will be filled during send operations
}

/**
 * @brief Destructor - ensures clean disconnection
 */
TcpClient::~TcpClient() {
    disconnect();
}

/**
 * @brief Move constructor
 *
 * Transfers ownership of socket and buffers from other client.
 *
 * @param other Client to move from
 */
TcpClient::TcpClient(TcpClient&& other) noexcept
    : m_socket(std::move(other.m_socket))
    , m_recv_buffer()  // PacketBuffer doesn't have move semantics, just reset
{
    // Copy buffer state manually if needed (for simplicity, just reset)
    other.m_recv_buffer.reset();
}

/**
 * @brief Move assignment operator
 *
 * Disconnects current connection (if any) and takes ownership from other.
 *
 * @param other Client to move from
 * @return Reference to this client
 */
TcpClient& TcpClient::operator=(TcpClient&& other) noexcept {
    if (this != &other) {
        disconnect();
        m_socket = std::move(other.m_socket);
        m_recv_buffer.reset();
        other.m_recv_buffer.reset();
    }
    return *this;
}

// =============================================================================
// Connection Management
// =============================================================================

/**
 * @brief Connect to RyuLdn server
 *
 * Establishes TCP connection and prepares for protocol communication.
 * The receive buffer is reset to ensure clean state.
 */
ClientResult TcpClient::connect(const char* host, uint16_t port, uint32_t timeout_ms) {
    LOG_VERBOSE("TcpClient::connect(%s, %u, %u)", host, port, timeout_ms);

    // Check if already connected
    if (m_socket.is_connected()) {
        LOG_WARN("TcpClient already connected");
        return ClientResult::AlreadyConnected;
    }

    // Attempt connection
    SocketResult result = m_socket.connect(host, port, timeout_ms);

    if (result != SocketResult::Success) {
        LOG_VERBOSE("Socket connect failed: %s", socket_result_to_string(result));
        return socket_to_client_result(result);
    }

    // Reset receive buffer for new connection
    m_recv_buffer.reset();

    // Enable TCP_NODELAY by default for lower latency
    m_socket.set_nodelay(true);

    LOG_VERBOSE("TcpClient connected successfully");
    return ClientResult::Success;
}

/**
 * @brief Disconnect from server
 *
 * Closes socket and resets internal state.
 */
void TcpClient::disconnect() {
    LOG_VERBOSE("TcpClient::disconnect()");
    m_socket.close();
    m_recv_buffer.reset();
}

/**
 * @brief Check connection status
 */
bool TcpClient::is_connected() const {
    return m_socket.is_connected();
}

// =============================================================================
// Send Operations
// =============================================================================

/**
 * @brief Send a raw protocol packet
 *
 * Encodes the packet with header and sends over the socket.
 * Uses internal send buffer for encoding.
 */
ClientResult TcpClient::send_packet(protocol::PacketId type, const void* payload, size_t payload_size) {
    if (!m_socket.is_connected()) {
        return ClientResult::NotConnected;
    }

    // Encode packet into send buffer using encode_raw for arbitrary data
    size_t encoded_size = 0;
    protocol::EncodeResult encode_result = protocol::encode_raw(
        m_send_buffer,
        sizeof(m_send_buffer),
        type,
        static_cast<const uint8_t*>(payload),
        payload_size,
        encoded_size
    );

    if (encode_result != protocol::EncodeResult::Success) {
        return ClientResult::EncodingError;
    }

    // Send the encoded packet
    SocketResult send_result = m_socket.send_all(m_send_buffer, encoded_size);

    if (send_result != SocketResult::Success) {
        return socket_to_client_result(send_result);
    }

    return ClientResult::Success;
}

/**
 * @brief Send Initialize message
 */
ClientResult TcpClient::send_initialize(const protocol::InitializeMessage& msg) {
    if (!m_socket.is_connected()) {
        return ClientResult::NotConnected;
    }

    size_t encoded_size = 0;
    protocol::EncodeResult encode_result = protocol::encode(
        m_send_buffer, sizeof(m_send_buffer),
        protocol::PacketId::Initialize, msg, encoded_size);

    if (encode_result != protocol::EncodeResult::Success) {
        return ClientResult::EncodingError;
    }

    SocketResult send_result = m_socket.send_all(m_send_buffer, encoded_size);
    return send_result == SocketResult::Success ? ClientResult::Success : socket_to_client_result(send_result);
}

/**
 * @brief Send Passphrase message
 */
ClientResult TcpClient::send_passphrase(const protocol::PassphraseMessage& msg) {
    if (!m_socket.is_connected()) {
        return ClientResult::NotConnected;
    }

    size_t encoded_size = 0;
    protocol::EncodeResult encode_result = protocol::encode(
        m_send_buffer, sizeof(m_send_buffer),
        protocol::PacketId::Passphrase, msg, encoded_size);

    if (encode_result != protocol::EncodeResult::Success) {
        return ClientResult::EncodingError;
    }

    SocketResult send_result = m_socket.send_all(m_send_buffer, encoded_size);
    return send_result == SocketResult::Success ? ClientResult::Success : socket_to_client_result(send_result);
}

/**
 * @brief Send Passphrase message (string convenience overload)
 */
ClientResult TcpClient::send_passphrase(const char* passphrase) {
    protocol::PassphraseMessage msg{};
    std::memset(msg.passphrase, 0, sizeof(msg.passphrase));
    if (passphrase != nullptr) {
        size_t len = std::strlen(passphrase);
        if (len > 127) len = 127;
        std::memcpy(msg.passphrase, passphrase, len);
    }
    return send_passphrase(msg);
}

/**
 * @brief Send Ping message
 */
ClientResult TcpClient::send_ping(const protocol::PingMessage& msg) {
    if (!m_socket.is_connected()) {
        return ClientResult::NotConnected;
    }

    size_t encoded_size = 0;
    protocol::EncodeResult encode_result = protocol::encode(
        m_send_buffer, sizeof(m_send_buffer),
        protocol::PacketId::Ping, msg, encoded_size);

    if (encode_result != protocol::EncodeResult::Success) {
        return ClientResult::EncodingError;
    }

    SocketResult send_result = m_socket.send_all(m_send_buffer, encoded_size);
    return send_result == SocketResult::Success ? ClientResult::Success : socket_to_client_result(send_result);
}

/**
 * @brief Send Disconnect message
 */
ClientResult TcpClient::send_disconnect(const protocol::DisconnectMessage& msg) {
    if (!m_socket.is_connected()) {
        return ClientResult::NotConnected;
    }

    size_t encoded_size = 0;
    protocol::EncodeResult encode_result = protocol::encode(
        m_send_buffer, sizeof(m_send_buffer),
        protocol::PacketId::Disconnect, msg, encoded_size);

    if (encode_result != protocol::EncodeResult::Success) {
        return ClientResult::EncodingError;
    }

    SocketResult send_result = m_socket.send_all(m_send_buffer, encoded_size);
    return send_result == SocketResult::Success ? ClientResult::Success : socket_to_client_result(send_result);
}

/**
 * @brief Send CreateAccessPoint request
 */
ClientResult TcpClient::send_create_access_point(const protocol::CreateAccessPointRequest& request) {
    if (!m_socket.is_connected()) {
        return ClientResult::NotConnected;
    }

    size_t encoded_size = 0;
    protocol::EncodeResult encode_result = protocol::encode(
        m_send_buffer, sizeof(m_send_buffer),
        protocol::PacketId::CreateAccessPoint, request, encoded_size);

    if (encode_result != protocol::EncodeResult::Success) {
        return ClientResult::EncodingError;
    }

    SocketResult send_result = m_socket.send_all(m_send_buffer, encoded_size);
    return send_result == SocketResult::Success ? ClientResult::Success : socket_to_client_result(send_result);
}

/**
 * @brief Send Connect request
 */
ClientResult TcpClient::send_connect(const protocol::ConnectRequest& request) {
    if (!m_socket.is_connected()) {
        return ClientResult::NotConnected;
    }

    size_t encoded_size = 0;
    protocol::EncodeResult encode_result = protocol::encode(
        m_send_buffer, sizeof(m_send_buffer),
        protocol::PacketId::Connect, request, encoded_size);

    if (encode_result != protocol::EncodeResult::Success) {
        return ClientResult::EncodingError;
    }

    SocketResult send_result = m_socket.send_all(m_send_buffer, encoded_size);
    return send_result == SocketResult::Success ? ClientResult::Success : socket_to_client_result(send_result);
}

/**
 * @brief Send Scan request
 */
ClientResult TcpClient::send_scan(const protocol::ScanFilterFull& filter) {
    if (!m_socket.is_connected()) {
        return ClientResult::NotConnected;
    }

    size_t encoded_size = 0;
    protocol::EncodeResult encode_result = protocol::encode(
        m_send_buffer, sizeof(m_send_buffer),
        protocol::PacketId::Scan, filter, encoded_size);

    if (encode_result != protocol::EncodeResult::Success) {
        return ClientResult::EncodingError;
    }

    SocketResult send_result = m_socket.send_all(m_send_buffer, encoded_size);
    return send_result == SocketResult::Success ? ClientResult::Success : socket_to_client_result(send_result);
}

/**
 * @brief Send proxy data
 *
 * Proxy data packets are special - they combine header + variable data.
 * We need to encode them together in the send buffer.
 */
ClientResult TcpClient::send_proxy_data(const protocol::ProxyDataHeader& header,
                                         const uint8_t* data, size_t data_size) {
    if (!m_socket.is_connected()) {
        return ClientResult::NotConnected;
    }

    size_t encoded_size = 0;
    protocol::EncodeResult encode_result = protocol::encode_proxy_data(
        m_send_buffer, sizeof(m_send_buffer),
        header.info, data, data_size, encoded_size);

    if (encode_result != protocol::EncodeResult::Success) {
        return ClientResult::EncodingError;
    }

    SocketResult send_result = m_socket.send_all(m_send_buffer, encoded_size);
    return send_result == SocketResult::Success ? ClientResult::Success : socket_to_client_result(send_result);
}

// =============================================================================
// Receive Operations
// =============================================================================

/**
 * @brief Receive next protocol packet
 *
 * This function handles the complexity of TCP stream reassembly:
 * 1. Check if a complete packet is already buffered
 * 2. If not, receive more data from socket
 * 3. Repeat until complete packet or timeout
 * 4. Extract and return the packet
 */
ClientResult TcpClient::receive_packet(protocol::PacketId& type,
                                        void* payload, size_t payload_buffer_size,
                                        size_t& payload_size,
                                        int32_t timeout_ms) {
    if (!m_socket.is_connected()) {
        return ClientResult::NotConnected;
    }

    payload_size = 0;

    // Loop until we have a complete packet or error/timeout
    while (!m_recv_buffer.has_complete_packet()) {
        ClientResult recv_result = receive_into_buffer(timeout_ms);

        if (recv_result != ClientResult::Success) {
            return recv_result;
        }
    }

    // We have a complete packet - get packet info
    size_t packet_size = 0;
    protocol::BufferResult peek_result = m_recv_buffer.peek_packet_info(packet_size);

    if (peek_result != protocol::BufferResult::Success) {
        return ClientResult::InvalidPacket;
    }

    // Decode header to get type and payload size
    protocol::LdnHeader header;
    protocol::DecodeResult decode_result = protocol::decode_header(
        m_recv_buffer.data(), m_recv_buffer.size(), header);

    if (decode_result != protocol::DecodeResult::Success) {
        return ClientResult::InvalidPacket;
    }

    // Check if payload fits in user buffer
    size_t packet_payload_size = static_cast<size_t>(header.data_size);
    payload_size = packet_payload_size;

    if (packet_payload_size > payload_buffer_size) {
        // Don't consume the packet - let caller provide larger buffer
        return ClientResult::BufferTooSmall;
    }

    // Extract the payload (skip header)
    if (packet_payload_size > 0) {
        std::memcpy(payload, m_recv_buffer.data() + sizeof(protocol::LdnHeader), packet_payload_size);
    }

    // Consume the packet from buffer
    m_recv_buffer.consume(packet_size);

    type = static_cast<protocol::PacketId>(header.type);
    return ClientResult::Success;
}

/**
 * @brief Check if a complete packet is available
 */
bool TcpClient::has_packet_available() const {
    return m_recv_buffer.has_complete_packet();
}

// =============================================================================
// Configuration
// =============================================================================

/**
 * @brief Enable/disable TCP_NODELAY
 */
ClientResult TcpClient::set_nodelay(bool enable) {
    if (!m_socket.is_connected()) {
        return ClientResult::NotConnected;
    }

    SocketResult result = m_socket.set_nodelay(enable);
    return socket_to_client_result(result);
}

// =============================================================================
// Private Helper Functions
// =============================================================================

/**
 * @brief Convert SocketResult to ClientResult
 *
 * Maps low-level socket errors to appropriate client-level results.
 */
ClientResult TcpClient::socket_to_client_result(SocketResult socket_result) {
    switch (socket_result) {
        case SocketResult::Success:
            return ClientResult::Success;

        case SocketResult::WouldBlock:
            return ClientResult::Success;  // Not an error in async context

        case SocketResult::Timeout:
            return ClientResult::Timeout;

        case SocketResult::NotConnected:
            return ClientResult::NotConnected;

        case SocketResult::AlreadyConnected:
            return ClientResult::AlreadyConnected;

        case SocketResult::NotInitialized:
            return ClientResult::NotInitialized;

        case SocketResult::Closed:
        case SocketResult::ConnectionReset:
            return ClientResult::ConnectionLost;

        case SocketResult::ConnectionRefused:
        case SocketResult::HostUnreachable:
        case SocketResult::NetworkDown:
        case SocketResult::InvalidAddress:
            return ClientResult::ConnectionFailed;

        default:
            return ClientResult::InternalError;
    }
}

/**
 * @brief Receive data into packet buffer
 *
 * Reads available data from socket and appends to the receive buffer.
 * Handles WouldBlock gracefully for non-blocking operation.
 */
ClientResult TcpClient::receive_into_buffer(int32_t timeout_ms) {
    // Temporary buffer for receiving
    uint8_t temp_buffer[4096];
    size_t received = 0;

    SocketResult recv_result = m_socket.recv(temp_buffer, sizeof(temp_buffer), received, timeout_ms);

    if (recv_result == SocketResult::WouldBlock) {
        // No data available right now - not an error
        return ClientResult::Timeout;
    }

    if (recv_result == SocketResult::Timeout) {
        return ClientResult::Timeout;
    }

    if (recv_result == SocketResult::Closed) {
        return ClientResult::ConnectionLost;
    }

    if (recv_result != SocketResult::Success) {
        return socket_to_client_result(recv_result);
    }

    if (received == 0) {
        // Zero bytes with Success means connection closed gracefully
        return ClientResult::ConnectionLost;
    }

    // Append received data to packet buffer
    protocol::BufferResult append_result = m_recv_buffer.append(temp_buffer, received);
    if (append_result != protocol::BufferResult::Success) {
        // Buffer overflow - shouldn't happen with normal protocol usage
        return ClientResult::InvalidPacket;
    }

    return ClientResult::Success;
}

} // namespace ryu_ldn::network
