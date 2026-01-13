/**
 * @file socket.hpp
 * @brief TCP Socket Wrapper for ryu_ldn_nx
 *
 * Provides a platform-agnostic TCP socket interface.
 * - On Switch: Uses libnx BSD sockets
 * - On host: Uses POSIX sockets (for testing)
 *
 * Reference: Epic 2, Story 2.2
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace ryu_ldn::network {

// ============================================================================
// Result Codes
// ============================================================================

enum class SocketResult {
    Success = 0,
    WouldBlock,        // Non-blocking operation would block
    Timeout,           // Operation timed out
    ConnectionRefused, // Server refused connection
    ConnectionReset,   // Connection reset by peer
    HostUnreachable,   // Cannot reach host
    NetworkDown,       // Network is down
    NotConnected,      // Socket not connected
    AlreadyConnected,  // Socket already connected
    InvalidAddress,    // Invalid address format
    SocketError,       // Generic socket error
    NotInitialized,    // Socket subsystem not initialized
    Closed             // Socket was closed
};

// ============================================================================
// Socket Class
// ============================================================================

/**
 * @brief TCP Socket wrapper
 *
 * Provides connect/send/recv operations with timeout support.
 * Non-copyable, move-only.
 *
 * Usage:
 * @code
 * Socket sock;
 * if (sock.connect("192.168.1.1", 30456, 5000) == SocketResult::Success) {
 *     uint8_t data[] = {0x01, 0x02, 0x03};
 *     size_t sent;
 *     sock.send(data, sizeof(data), sent);
 *
 *     uint8_t buf[256];
 *     size_t received;
 *     sock.recv(buf, sizeof(buf), received, 1000);
 *
 *     sock.close();
 * }
 * @endcode
 */
class Socket {
public:
    /**
     * @brief Default constructor - creates invalid socket
     */
    Socket();

    /**
     * @brief Destructor - closes socket if open
     */
    ~Socket();

    // Non-copyable
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    // Moveable
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    /**
     * @brief Connect to a remote host
     * @param host Hostname or IP address
     * @param port Port number
     * @param timeout_ms Connection timeout in milliseconds (0 = blocking)
     * @return SocketResult::Success or error
     */
    SocketResult connect(const char* host, uint16_t port, uint32_t timeout_ms = 0);

    /**
     * @brief Send data (blocking)
     * @param data Data to send
     * @param size Size of data
     * @param[out] sent Number of bytes actually sent
     * @return SocketResult::Success or error
     */
    SocketResult send(const uint8_t* data, size_t size, size_t& sent);

    /**
     * @brief Send all data (loops until complete or error)
     * @param data Data to send
     * @param size Size of data
     * @return SocketResult::Success or error
     */
    SocketResult send_all(const uint8_t* data, size_t size);

    /**
     * @brief Receive data (non-blocking or with timeout)
     * @param buffer Buffer to receive into
     * @param buffer_size Size of buffer
     * @param[out] received Number of bytes received
     * @param timeout_ms Receive timeout (0 = non-blocking, -1 = blocking)
     * @return SocketResult::Success, WouldBlock, or error
     */
    SocketResult recv(uint8_t* buffer, size_t buffer_size, size_t& received, int32_t timeout_ms = 0);

    /**
     * @brief Close the socket
     */
    void close();

    /**
     * @brief Check if socket is connected
     * @return true if connected
     */
    bool is_connected() const;

    /**
     * @brief Check if socket is valid (has file descriptor)
     * @return true if valid
     */
    bool is_valid() const;

    /**
     * @brief Get the native socket file descriptor
     * @return File descriptor or -1 if invalid
     */
    int get_fd() const { return m_fd; }

    /**
     * @brief Set socket to non-blocking mode
     * @param non_blocking true for non-blocking, false for blocking
     * @return SocketResult::Success or error
     */
    SocketResult set_non_blocking(bool non_blocking);

    /**
     * @brief Set TCP_NODELAY option (disable Nagle's algorithm)
     * @param nodelay true to disable Nagle
     * @return SocketResult::Success or error
     */
    SocketResult set_nodelay(bool nodelay);

    /**
     * @brief Set socket receive buffer size
     * @param size Buffer size in bytes
     * @return SocketResult::Success or error
     */
    SocketResult set_recv_buffer_size(int size);

    /**
     * @brief Set socket send buffer size
     * @param size Buffer size in bytes
     * @return SocketResult::Success or error
     */
    SocketResult set_send_buffer_size(int size);

private:
    int m_fd;
    bool m_connected;

    /**
     * @brief Create TCP socket
     * @return SocketResult::Success or error
     */
    SocketResult create();

    /**
     * @brief Wait for socket to be ready (using poll)
     * @param timeout_ms Timeout in milliseconds
     * @param for_write true to wait for write, false for read
     * @return SocketResult::Success, Timeout, or error
     */
    SocketResult wait_ready(uint32_t timeout_ms, bool for_write);
};

// ============================================================================
// Socket Subsystem
// ============================================================================

/**
 * @brief Initialize socket subsystem
 * @return SocketResult::Success or error
 *
 * Must be called before using any socket operations.
 * On Switch, this calls socketInitializeDefault().
 * On host, this is a no-op.
 */
SocketResult socket_init();

/**
 * @brief Shutdown socket subsystem
 *
 * Should be called when done with sockets.
 * On Switch, this calls socketExit().
 */
void socket_exit();

/**
 * @brief Check if socket subsystem is initialized
 * @return true if initialized
 */
bool socket_is_initialized();

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Convert SocketResult to string for debugging
 */
inline const char* socket_result_to_string(SocketResult result) {
    switch (result) {
        case SocketResult::Success:           return "Success";
        case SocketResult::WouldBlock:        return "WouldBlock";
        case SocketResult::Timeout:           return "Timeout";
        case SocketResult::ConnectionRefused: return "ConnectionRefused";
        case SocketResult::ConnectionReset:   return "ConnectionReset";
        case SocketResult::HostUnreachable:   return "HostUnreachable";
        case SocketResult::NetworkDown:       return "NetworkDown";
        case SocketResult::NotConnected:      return "NotConnected";
        case SocketResult::AlreadyConnected:  return "AlreadyConnected";
        case SocketResult::InvalidAddress:    return "InvalidAddress";
        case SocketResult::SocketError:       return "SocketError";
        case SocketResult::NotInitialized:    return "NotInitialized";
        case SocketResult::Closed:            return "Closed";
        default:                              return "Unknown";
    }
}

} // namespace ryu_ldn::network
