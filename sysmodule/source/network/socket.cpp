/**
 * @file socket.cpp
 * @brief TCP Socket Implementation for ryu_ldn_nx
 *
 * This file implements the Socket class for TCP network communication.
 * It provides a platform-agnostic interface that works on:
 *   - Nintendo Switch (using libnx BSD socket implementation)
 *   - Host systems (Linux/macOS for testing)
 *
 * ## Architecture
 *
 * The socket implementation uses standard BSD socket APIs which are available
 * on both Switch (via libnx) and POSIX systems. Key features:
 *
 * - Non-blocking I/O with poll() for timeout support
 * - Automatic hostname resolution via getaddrinfo()
 * - MSG_NOSIGNAL to prevent SIGPIPE on broken connections
 * - Proper cleanup on move/destruction
 *
 * ## Switch-Specific Notes
 *
 * On Switch, socket_init() is called during sysmodule startup (main.cpp) and only
 * sets a global flag — socketInitializeDefault() is called separately by the host.
 * On POSIX (test builds), socket_init() is a no-op.
 *
 * - DNS resolution via getaddrinfo() requires an active network connection
 * - Transfer memory is allocated automatically by libnx
 * - Maximum concurrent sockets is limited by system resources
 *
 * ## Error Handling
 *
 * All operations return SocketResult enum values. The errno_to_result()
 * helper maps POSIX errno codes to our result enum for consistent error
 * handling across platforms.
 *
 * ## Thread Safety
 *
 * Individual Socket instances are NOT thread-safe. Each thread should
 * have its own Socket instance, or external synchronization must be used.
 * The socket_init()/socket_exit() functions are safe to call from any thread
 * but should typically be called once at startup/shutdown.
 *
 * @see socket.hpp for the public interface
 * @see Epic 2, Story 2.2 for requirements
 */

#include "socket.hpp"
#include "../debug/log.hpp"
#include <cstring>
#include <cerrno>

// =============================================================================
// Platform-Specific Includes
// =============================================================================

#ifdef __SWITCH__
// Nintendo Switch - libnx provides BSD-compatible socket APIs
#include <switch.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#else
// Host system (Linux/macOS) - Standard POSIX sockets
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#endif

namespace ryu_ldn::network {

// =============================================================================
// Socket timeout constants
// =============================================================================
// Named constants for the magic-number timeouts previously inlined in send_all
// and connect. Preserves the existing 5-second per-chunk write timeout.

/// @brief Per-chunk write timeout in send_all() (ms)
constexpr uint32_t SOCKET_WRITE_TIMEOUT_MS = 5000;

// =============================================================================
// Static State
// =============================================================================

/**
 * @brief Global initialization flag for socket subsystem
 *
 * Tracks whether socket_init() has been called. On Switch, this ensures
 * we don't call socketInitializeDefault() multiple times. On host systems,
 * this is mainly for consistency and debugging.
 */
static bool s_initialized = false;

// =============================================================================
// Socket Subsystem Functions
// =============================================================================

/**
 * @brief Initialize the socket subsystem
 *
 * Sets a global flag to indicate that sockets are available.
 * On Switch, the actual socketInitializeDefault() call happens in main.cpp
 * before any MITM services are created — this function only sets the flag.
 *
 * On host systems (test builds):
 * - No-op, sockets are always available
 *
 * @return SocketResult::Success always (on Switch, actual init is in main.cpp)
 *
 * @note Safe to call multiple times (subsequent calls are no-ops)
 * @note Must call socket_exit() when done to clear the flag on Switch
 */
SocketResult socket_init() {
    // Idempotent - safe to call multiple times
    if (s_initialized) {
        return SocketResult::Success;
    }

    // Note: On Switch, sockets are initialized in main.cpp via socketInitialize()
    // before any MITM services are created. We don't call socketInitializeDefault()
    // here because it would fail (sockets already initialized) or conflict with
    // the existing initialization.

    s_initialized = true;
    return SocketResult::Success;
}

/**
 * @brief Shutdown the socket subsystem
 *
 * Marks the socket subsystem as uninitialized.
 *
 * @note On Switch, socket cleanup is handled by main.cpp via socketExit().
 *       This function only resets the internal state flag.
 *
 * @note Safe to call even if not initialized (no-op)
 */
void socket_exit() {
    // Note: On Switch, socketExit() is called in main.cpp's FinalizeSystemModule()
    // We only reset our internal state flag here.
    s_initialized = false;
}

/**
 * @brief Check if socket subsystem is initialized
 *
 * @return true if socket_init() was called successfully
 * @return false otherwise
 */
bool socket_is_initialized() {
    return s_initialized;
}

// =============================================================================
// Helper Functions (Internal)
// =============================================================================

namespace {

/**
 * @brief Map POSIX errno to SocketResult
 *
 * This function translates standard errno values to our SocketResult enum,
 * providing consistent error handling across platforms.
 *
 * @param err The errno value to translate
 * @return Corresponding SocketResult enum value
 *
 * Common mappings:
 * - EAGAIN/EWOULDBLOCK -> WouldBlock (non-blocking operation)
 * - ECONNREFUSED -> ConnectionRefused (server not listening)
 * - ECONNRESET -> ConnectionReset (connection dropped by peer)
 * - EHOSTUNREACH -> HostUnreachable (routing failure)
 * - ETIMEDOUT -> Timeout (connection or operation timeout)
 */
#ifndef TEST_BUILD
SocketResult errno_to_result(int err) {
    // Non-blocking operation would block
    if (err == EAGAIN) {
        return SocketResult::WouldBlock;
    }
#if EAGAIN != EWOULDBLOCK
    if (err == EWOULDBLOCK) {
        return SocketResult::WouldBlock;
    }
#endif
    // Connection errors
    if (err == ECONNREFUSED) {
        return SocketResult::ConnectionRefused;
    }
    if (err == ECONNRESET) {
        return SocketResult::ConnectionReset;
    }
    // Network reachability errors
    if (err == EHOSTUNREACH || err == ENETUNREACH) {
        return SocketResult::HostUnreachable;
    }
    if (err == ENETDOWN) {
        return SocketResult::NetworkDown;
    }
    // Socket state errors
    if (err == ENOTCONN) {
        return SocketResult::NotConnected;
    }
    if (err == EISCONN) {
        return SocketResult::AlreadyConnected;
    }
    // Timeout
    if (err == ETIMEDOUT) {
        return SocketResult::Timeout;
    }
    // Everything else is a generic socket error
    return SocketResult::SocketError;
}
#endif

/**
 * @brief Resolve hostname to IPv4 address
 *
 * Attempts to resolve the given hostname to an IPv4 address. First tries
 * to parse as a numeric IP (e.g., "192.168.1.1"), then falls back to
 * DNS resolution.
 *
 * @param host Hostname or IP address string (null-terminated)
 * @param[out] addr Sockaddr structure to fill with resolved address
 * @return true if resolution succeeded
 * @return false if hostname could not be resolved
 *
 * @note On Switch, DNS resolution requires an active network connection.
 *       If offline, only numeric IPs will work.
 *
 * @warning This function is blocking. DNS queries may take several seconds
 *          if the network is slow or DNS server is unresponsive.
 */
bool resolve_host(const char* host, struct sockaddr_in& addr) {
    // Zero-initialize the address structure
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;

    // First, try to parse as numeric IPv4 address (e.g., "192.168.1.100")
    // This is fast and doesn't require network access
    if (inet_pton(AF_INET, host, &addr.sin_addr) == 1) {
        return true;
    }

    // Not a numeric IP, try DNS resolution
    // Set up hints for IPv4 TCP socket
    struct addrinfo hints{};
    hints.ai_family = AF_INET;       // IPv4 only
    hints.ai_socktype = SOCK_STREAM; // TCP

    struct addrinfo* result = nullptr;
    int ret = getaddrinfo(host, nullptr, &hints, &result);

    if (ret != 0 || !result) {
        // DNS resolution failed
        // Common causes: network offline, DNS server unreachable, invalid hostname
        return false;
    }

    // Copy the first result (usually the only one for simple queries)
    std::memcpy(&addr, result->ai_addr, sizeof(addr));

    // Free the linked list allocated by getaddrinfo
    freeaddrinfo(result);

    return true;
}

} // anonymous namespace

// =============================================================================
// Socket Class Implementation
// =============================================================================

/**
 * @brief Default constructor - creates an uninitialized socket
 *
 * After construction, the socket is in an invalid state (m_fd == -1).
 * Call connect() to establish a connection, which will create the
 * underlying socket automatically.
 */
Socket::Socket()
    : m_fd(-1)
    , m_connected(false)
{
}

/**
 * @brief Destructor - ensures socket is properly closed
 *
 * Automatically closes the socket if still open. This ensures no
 * resource leaks even if close() wasn't explicitly called.
 */
Socket::~Socket() {
    close();
}

/**
 * @brief Move constructor
 *
 * Transfers ownership of the socket from another instance.
 * The source socket becomes invalid after the move.
 *
 * @param other Socket to move from
 */
Socket::Socket(Socket&& other) noexcept
    : m_fd(other.m_fd)
    , m_connected(other.m_connected)
{
    // Invalidate the source socket
    other.m_fd = -1;
    other.m_connected = false;
}

/**
 * @brief Move assignment operator
 *
 * Closes any existing socket, then transfers ownership from the source.
 * The source socket becomes invalid after the move.
 *
 * @param other Socket to move from
 * @return Reference to this socket
 */
Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        // Close our existing socket first
        close();

        // Transfer ownership
        m_fd = other.m_fd;
        m_connected = other.m_connected;

        // Invalidate source
        other.m_fd = -1;
        other.m_connected = false;
    }
    return *this;
}

/**
 * @brief Create the underlying TCP socket
 *
 * Internal function to create the BSD socket. Called automatically
 * by connect() if the socket hasn't been created yet.
 *
 * @return SocketResult::Success if socket created or already exists
 * @return SocketResult::SocketError on failure
 */
SocketResult Socket::create() {
    // Idempotent - if already created, just return success
    if (m_fd >= 0) {
        return SocketResult::Success;
    }

    // Create TCP socket (IPv4, stream, default protocol)
    m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd < 0) {
        return errno_to_result(errno);
    }

    return SocketResult::Success;
}

/**
 * @brief Connect to a remote host
 *
 * Establishes a TCP connection to the specified host and port.
 * Creates the underlying socket if not already created.
 *
 * ## Connection Process
 * 1. Resolve hostname to IP address
 * 2. Create socket if needed
 * 3. Set non-blocking mode (if timeout specified)
 * 4. Initiate connection
 * 5. Wait for connection with poll() (if timeout specified)
 * 6. Verify connection succeeded
 * 7. Restore blocking mode
 *
 * @param host IP address (e.g., "ryuldnnx.ddns.net")
 * @param port TCP port number (e.g., 30456)
 * @param timeout_ms Connection timeout in milliseconds (0 = blocking/no timeout)
 *
 * @return SocketResult::Success on successful connection
 * @return SocketResult::NotInitialized if socket_init() wasn't called
 * @return SocketResult::AlreadyConnected if already connected
 * @return SocketResult::InvalidAddress if hostname resolution fails
 * @return SocketResult::Timeout if connection times out
 * @return SocketResult::ConnectionRefused if server not listening
 * @return SocketResult::HostUnreachable if host can't be reached
 *
 * @note If connection fails, the socket is automatically closed
 * @note Recommended timeout: 5000ms (5 seconds) for typical use
 */
SocketResult Socket::connect(const char* host, uint16_t port, uint32_t timeout_ms) {
    // Ensure socket subsystem is initialized
    if (!s_initialized) {
        return SocketResult::NotInitialized;
    }

    // Validate host parameter - null or empty hostname is invalid
    if (host == nullptr || host[0] == '\0') {
        return SocketResult::InvalidAddress;
    }

    // Don't allow connecting if already connected
    if (m_connected) {
        return SocketResult::AlreadyConnected;
    }

    // Create the underlying socket
    SocketResult result = create();
    if (result != SocketResult::Success) {
        return result;
    }

    // Resolve hostname to IPv4 address
    struct sockaddr_in addr;
    if (!resolve_host(host, addr)) {
        LOG_ERROR("Socket::connect: resolve_host failed for '%s'", host);
        close();  // Clean up the created socket on resolution failure
        return SocketResult::InvalidAddress;
    }
    LOG_INFO("Socket::connect: resolved '%s' -> %s:%u", host,
             inet_ntoa(addr.sin_addr), port);
    addr.sin_port = htons(port);  // Convert port to network byte order

    // Determine if we should use timeout
    bool use_timeout = (timeout_ms > 0);

    if (use_timeout) {
        // Set non-blocking mode for connect with timeout
        result = set_non_blocking(true);
        if (result != SocketResult::Success) {
            close();
            return result;
        }
    }

    // Initiate TCP connection
    int ret = ::connect(m_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

    if (ret < 0) {
        if (use_timeout && (errno == EINPROGRESS || errno == EWOULDBLOCK)) {
            // Connection in progress - wait for it to complete
            LOG_VERBOSE("Socket::connect: EINPROGRESS, waiting up to %u ms", timeout_ms);
            result = wait_ready(timeout_ms, true);  // Wait for socket to be writable
            if (result != SocketResult::Success) {
                LOG_WARN("Socket::connect: wait_ready failed: %s", socket_result_to_string(result));
                close();
                return result;
            }

            // Connection attempt finished - check if it succeeded
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
                LOG_ERROR("Socket::connect: getsockopt failed: %d", errno);
                close();
                return errno_to_result(errno);
            }

            if (error != 0) {
                // Connection failed
                LOG_WARN("Socket::connect: SO_ERROR=%d (%s) after poll", error, strerror(error));
                close();
                return errno_to_result(error);
            }
        } else {
            // Immediate failure (or blocking mode error)
            LOG_WARN("Socket::connect: immediate failure: %d (%s)", errno, strerror(errno));
            close();
            return errno_to_result(errno);
        }
    }

    // Connection succeeded - restore blocking mode if we changed it
    if (use_timeout) {
        set_non_blocking(false);
    }

    // Enable TCP keepalive to detect dead connections on unstable networks.
    // This is purely a transport-level mechanism — no RyuLDN protocol changes.
    // After 30s idle, send a probe every 10s; after 5 failed probes, close.
    int keepalive = 1;
    if (setsockopt(m_fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0) {
        LOG_WARN("Socket::connect: SO_KEEPALIVE failed: %d (%s)", errno, strerror(errno));
        // Non-fatal — continue without keepalive
    } else {
#ifdef __SWITCH__
        // Switch/libnx uses TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT
        // (same constants as Linux, available via <netinet/tcp.h>)
        int idle = 30;   // seconds before first probe
        int intvl = 10;  // seconds between probes
        int cnt = 5;     // number of failed probes before close
        setsockopt(m_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
        setsockopt(m_fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
        setsockopt(m_fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif
    }

    m_connected = true;
    LOG_INFO("Socket::connect: connected to %s:%u successfully", host, port);
    return SocketResult::Success;
}

/**
 * @brief Send data over the socket
 *
 * Attempts to send data. May not send all data in one call - check
 * the 'sent' parameter to see how much was actually sent.
 *
 * @param data Pointer to data buffer to send
 * @param size Number of bytes to send
 * @param[out] sent Number of bytes actually sent (may be less than size)
 *
 * @return SocketResult::Success if some data was sent
 * @return SocketResult::WouldBlock if socket is non-blocking and would block
 * @return SocketResult::NotConnected if not connected
 * @return SocketResult::Closed if connection was closed by peer
 * @return SocketResult::ConnectionReset if connection was reset
 *
 * @note Use send_all() if you need to send all data reliably
 * @note Uses MSG_NOSIGNAL to prevent SIGPIPE on broken connections
 */
SocketResult Socket::send(const uint8_t* data, size_t size, size_t& sent) {
    sent = 0;

    if (!m_connected) {
        return SocketResult::NotConnected;
    }

    // MSG_NOSIGNAL prevents SIGPIPE if the connection is broken
    // Without this, writing to a closed socket would kill the process
    ssize_t ret = ::send(m_fd, data, size, MSG_NOSIGNAL);

    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return SocketResult::WouldBlock;
        }
        // Connection error - mark as disconnected
        m_connected = false;
        return errno_to_result(errno);
    }

    if (ret == 0) {
        // Zero bytes sent usually means connection closed
        m_connected = false;
        return SocketResult::Closed;
    }

    sent = static_cast<size_t>(ret);
    return SocketResult::Success;
}

/**
 * @brief Send all data reliably
 *
 * Loops until all data is sent or an error occurs. Handles partial
 * sends and WouldBlock conditions automatically.
 *
 * @param data Pointer to data buffer to send
 * @param size Number of bytes to send
 *
 * @return SocketResult::Success if all data was sent
 * @return SocketResult::Timeout if waiting too long for socket to be writable
 * @return Other error codes on failure
 *
 * @note This function may block for extended periods
 * @note Uses 5 second timeout per send chunk
 */
SocketResult Socket::send_all(const uint8_t* data, size_t size) {
    size_t total_sent = 0;

    while (total_sent < size) {
        size_t sent = 0;
        SocketResult result = send(data + total_sent, size - total_sent, sent);

        if (result == SocketResult::WouldBlock) {
            // Wait for socket to become writable
            result = wait_ready(SOCKET_WRITE_TIMEOUT_MS, true);  // 5 second timeout per chunk
            if (result != SocketResult::Success) {
                return result;
            }
            continue;  // Retry send
        }

        if (result != SocketResult::Success) {
            return result;
        }

        total_sent += sent;
    }

    return SocketResult::Success;
}

/**
 * @brief Receive data from the socket
 *
 * Attempts to receive data from the socket. Behavior depends on timeout:
 * - timeout_ms > 0: Wait up to timeout_ms for data
 * - timeout_ms == 0: Non-blocking, return immediately
 * - timeout_ms < 0: Blocking, wait indefinitely
 *
 * @param buffer Buffer to receive data into
 * @param buffer_size Maximum bytes to receive (size of buffer)
 * @param[out] received Number of bytes actually received
 * @param timeout_ms Receive timeout in milliseconds
 *
 * @return SocketResult::Success if data was received
 * @return SocketResult::WouldBlock if non-blocking and no data available
 * @return SocketResult::Timeout if timed out waiting for data
 * @return SocketResult::Closed if connection was closed by peer
 * @return SocketResult::NotConnected if not connected
 *
 * @note received may be less than buffer_size (TCP is a stream, not messages)
 */
SocketResult Socket::recv(uint8_t* buffer, size_t buffer_size, size_t& received, int32_t timeout_ms) {
    received = 0;

    if (!m_connected) {
        return SocketResult::NotConnected;
    }

    // Handle positive timeout: wait for data with poll()
    if (timeout_ms > 0) {
        SocketResult result = wait_ready(static_cast<uint32_t>(timeout_ms), false);
        if (result != SocketResult::Success) {
            return result;
        }
    }
    // Handle non-blocking (timeout_ms == 0)
    else if (timeout_ms == 0) {
        // Temporarily set non-blocking mode if needed
        int flags = fcntl(m_fd, F_GETFL, 0);
        bool was_blocking = !(flags & O_NONBLOCK);

        if (was_blocking) {
            fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
        }

        ssize_t ret = ::recv(m_fd, buffer, buffer_size, 0);

        // Restore original mode
        if (was_blocking) {
            fcntl(m_fd, F_SETFL, flags);
        }

        if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return SocketResult::WouldBlock;
            }
            m_connected = false;
            return errno_to_result(errno);
        }

        if (ret == 0) {
            // Zero bytes = connection closed gracefully
            m_connected = false;
            return SocketResult::Closed;
        }

        received = static_cast<size_t>(ret);
        return SocketResult::Success;
    }

    // Blocking receive (timeout_ms < 0)
    ssize_t ret = ::recv(m_fd, buffer, buffer_size, 0);

    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return SocketResult::WouldBlock;
        }
        m_connected = false;
        return errno_to_result(errno);
    }

    if (ret == 0) {
        // Zero bytes = connection closed gracefully
        m_connected = false;
        return SocketResult::Closed;
    }

    received = static_cast<size_t>(ret);
    return SocketResult::Success;
}

/**
 * @brief Close the socket
 *
 * Closes the underlying socket file descriptor and resets state.
 * Safe to call multiple times (subsequent calls are no-ops).
 * Also called automatically by the destructor.
 */
void Socket::close() {
    if (m_fd >= 0) {
        // Graceful shutdown: send FIN before close so the remote end
        // sees a clean disconnect rather than a RST. On unstable WiFi
        // this prevents the server from interpreting the close as an
        // abnormal connection loss.
        if (m_connected) {
            ::shutdown(m_fd, SHUT_WR);
        }
        ::close(m_fd);
        m_fd = -1;
    }
    m_connected = false;
}

/**
 * @brief Check if socket is connected
 *
 * @return true if a connection has been established and not yet closed
 * @return false otherwise
 *
 * @note This only tracks local state. The remote end may have closed
 *       the connection; this will be detected on the next send/recv.
 */
bool Socket::is_connected() const {
    return m_connected;
}

/**
 * @brief Check if socket is valid
 *
 * @return true if the socket has a valid file descriptor
 * @return false if socket was never created or has been closed
 */
bool Socket::is_valid() const {
    return m_fd >= 0;
}

/**
 * @brief Set socket blocking mode
 *
 * @param non_blocking true to set non-blocking, false for blocking
 * @return SocketResult::Success on success
 * @return SocketResult::SocketError if socket is invalid or fcntl fails
 *
 * @note Generally you don't need to call this directly - the timeout
 *       parameters on connect/recv handle non-blocking behavior.
 */
SocketResult Socket::set_non_blocking(bool non_blocking) {
    if (m_fd < 0) {
        return SocketResult::SocketError;
    }

    int flags = fcntl(m_fd, F_GETFL, 0);
    if (flags < 0) {
        return errno_to_result(errno);
    }

    if (non_blocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(m_fd, F_SETFL, flags) < 0) {
        return errno_to_result(errno);
    }

    return SocketResult::Success;
}

/**
 * @brief Set TCP_NODELAY option
 *
 * Disables Nagle's algorithm, which buffers small packets to reduce
 * overhead. For latency-sensitive protocols like gaming, disabling
 * this can reduce latency at the cost of more network packets.
 *
 * @param nodelay true to disable Nagle (lower latency), false to enable
 * @return SocketResult::Success on success
 * @return SocketResult::SocketError on failure
 *
 * @note Recommended: true for gaming/realtime applications
 */
SocketResult Socket::set_nodelay(bool nodelay) {
    if (m_fd < 0) {
        return SocketResult::SocketError;
    }

    int flag = nodelay ? 1 : 0;
    if (setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        return errno_to_result(errno);
    }

    return SocketResult::Success;
}

/**
 * @brief Set socket receive buffer size
 *
 * Adjusts the kernel receive buffer size. Larger buffers can help
 * with high-bandwidth transfers but consume more memory.
 *
 * @param size Buffer size in bytes
 * @return SocketResult::Success on success
 * @return SocketResult::SocketError on failure
 *
 * @note The kernel may not honor the exact size requested
 */
SocketResult Socket::set_recv_buffer_size(int size) {
    if (m_fd < 0) {
        return SocketResult::SocketError;
    }

    if (setsockopt(m_fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) < 0) {
        return errno_to_result(errno);
    }

    return SocketResult::Success;
}

/**
 * @brief Set socket send buffer size
 *
 * Adjusts the kernel send buffer size. Larger buffers can help
 * with high-bandwidth transfers but consume more memory.
 *
 * @param size Buffer size in bytes
 * @return SocketResult::Success on success
 * @return SocketResult::SocketError on failure
 *
 * @note The kernel may not honor the exact size requested
 */
SocketResult Socket::set_send_buffer_size(int size) {
    if (m_fd < 0) {
        return SocketResult::SocketError;
    }

    if (setsockopt(m_fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) < 0) {
        return errno_to_result(errno);
    }

    return SocketResult::Success;
}

/**
 * @brief Wait for socket to be ready for I/O
 *
 * Uses poll() to wait for the socket to become readable or writable.
 * This is used internally for timeout support on connect and recv.
 *
 * @param timeout_ms Maximum time to wait in milliseconds
 * @param for_write true to wait for write-ready, false for read-ready
 *
 * @return SocketResult::Success if socket is ready
 * @return SocketResult::Timeout if timeout expired
 * @return SocketResult::SocketError if poll indicates an error
 *
 * @note poll() is preferred over select() for simplicity and efficiency
 */
SocketResult Socket::wait_ready(uint32_t timeout_ms, bool for_write) {
    struct pollfd pfd;
    pfd.fd = m_fd;
    pfd.events = for_write ? POLLOUT : POLLIN;
    pfd.revents = 0;

    int ret = poll(&pfd, 1, static_cast<int>(timeout_ms));

    if (ret < 0) {
        // poll() error (e.g., interrupted by signal)
        return errno_to_result(errno);
    }

    if (ret == 0) {
        // Timeout - no events within the time limit
        return SocketResult::Timeout;
    }

    // Check for error conditions in revents.
    //
    // Without this treatment, returning SocketError here (and keeping
    // m_connected=true) caused process_packets to spin — receive_packet
    // returned ClientResult::InternalError (generic), process_packets
    // logged + broke without touching the state machine, and the background
    // thread re-looped forever on a socket that would never see Success.
    // When POLLHUP/POLLERR/POLLNVAL is signaled, the peer side is dead
    // (or the fd is invalid): mark the socket as disconnected — same as
    // we do for recv() == 0 or ECONNRESET — and map to ConnectionReset so
    // the TcpClient stack propagates ConnectionLost and triggers
    // auto-reconnect.
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        // Flush — this log is the key diagnostic event for "master TCP dies
        // while game IPC is still running". Without flushing we lose it if
        // a KP follows (the logger's 2s idle-flush hasn't triggered yet).
        LOG_INFO("Socket::wait_ready: peer-side close on fd=%d (revents=0x%X: %s%s%s) — marking disconnected",
                 m_fd, static_cast<unsigned>(pfd.revents),
                 (pfd.revents & POLLERR)  ? "POLLERR " : "",
                 (pfd.revents & POLLHUP)  ? "POLLHUP " : "",
                 (pfd.revents & POLLNVAL) ? "POLLNVAL" : "");
        ryu_ldn::debug::g_logger.flush();
        m_connected = false;
        return SocketResult::ConnectionReset;
    }

    // Socket is ready for the requested operation
    return SocketResult::Success;
}

} // namespace ryu_ldn::network

#ifdef TEST_BUILD
// Expose errno_to_result for unit testing
namespace ryu_ldn::network {

SocketResult errno_to_result(int err) {
    // Delegate to the anonymous namespace version by reimplementing
    // (the anonymous version is not accessible here)
    if (err == EAGAIN) {
        return SocketResult::WouldBlock;
    }
#if EAGAIN != EWOULDBLOCK
    if (err == EWOULDBLOCK) {
        return SocketResult::WouldBlock;
    }
#endif
    if (err == ECONNREFUSED) {
        return SocketResult::ConnectionRefused;
    }
    if (err == ECONNRESET) {
        return SocketResult::ConnectionReset;
    }
    if (err == EHOSTUNREACH || err == ENETUNREACH) {
        return SocketResult::HostUnreachable;
    }
    if (err == ENETDOWN) {
        return SocketResult::NetworkDown;
    }
    if (err == ENOTCONN) {
        return SocketResult::NotConnected;
    }
    if (err == EISCONN) {
        return SocketResult::AlreadyConnected;
    }
    if (err == ETIMEDOUT) {
        return SocketResult::Timeout;
    }
    return SocketResult::SocketError;
}

} // namespace ryu_ldn::network
#endif
