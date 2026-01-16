/**
 * @file p2p_proxy_client.cpp
 * @brief P2P Proxy Client Implementation
 *
 * This file implements the P2pProxyClient class for direct P2P connections
 * to a host player, bypassing the relay server.
 *
 * ## Architecture Overview
 *
 * ```
 *                           Master Server
 *                                │
 *                                │ ExternalProxyConfig
 *                                │ (contains host IP, port, auth token)
 *                                ▼
 *     ┌─────────────────────────────────────────────────────────┐
 *     │                    P2pProxyClient                       │
 *     │                                                         │
 *     │  ┌─────────────┐     ┌──────────────┐                   │
 *     │  │ Main Thread │     │ Recv Thread  │                   │
 *     │  │             │     │              │                   │
 *     │  │ Connect()   │     │ ReceiveLoop()│                   │
 *     │  │ PerformAuth │     │ ProcessData()│                   │
 *     │  │ Send*()     │     │ Handle*()    │                   │
 *     │  └──────┬──────┘     └──────┬───────┘                   │
 *     │         │                   │                           │
 *     │         │   m_socket_fd     │                           │
 *     │         └────────┬──────────┘                           │
 *     │                  │                                      │
 *     └──────────────────┼──────────────────────────────────────┘
 *                        │
 *                        │ TCP Connection
 *                        ▼
 *                 P2pProxyServer (Host)
 *                 Port 39990-39999
 * ```
 *
 * ## Connection Flow
 *
 * ```
 * Client (Joiner)                              Server (Host)
 *     │                                             │
 *     │ Connect(ip, port)                           │
 *     │─────────────────────────────────────────────►│
 *     │            TCP SYN/SYN-ACK/ACK              │
 *     │◄────────────────────────────────────────────│
 *     │                                             │
 *     │ PerformAuth(ExternalProxyConfig)            │
 *     │─────────────────────────────────────────────►│
 *     │     PacketId::ExternalProxy + config        │ TryRegisterUser()
 *     │                                             │
 *     │ EnsureProxyReady() [blocking]               │
 *     │◄────────────────────────────────────────────│
 *     │     PacketId::ProxyConfig + config          │
 *     │                                             │
 *     │ [Ready for proxy communication]             │
 *     │                                             │
 *     │ SendProxyData() / HandleProxyData()         │
 *     │◄───────────────────────────────────────────►│
 *     │                                             │
 * ```
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "p2p_proxy_client.hpp"
#include "../debug/log.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

namespace ams::mitm::p2p {

// =============================================================================
// Thread Entry Point
// =============================================================================

/**
 * @brief Entry point for the receive thread
 *
 * This thread continuously receives data from the P2P host and processes
 * incoming packets.
 *
 * @param arg Pointer to P2pProxyClient instance
 */
void ClientRecvThreadEntry(void* arg) {
    auto* client = static_cast<P2pProxyClient*>(arg);
    client->ReceiveLoop();
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

/**
 * @brief Constructor
 *
 * Initializes client state but does not establish a connection.
 * Call Connect() to connect to a host.
 *
 * @param packet_callback Callback for forwarding received proxy packets
 */
P2pProxyClient::P2pProxyClient(ProxyPacketCallback packet_callback)
    : m_socket_fd(-1)
    , m_connected(false)
    , m_disposed(false)
    , m_ready(false)
    , m_proxy_config{}
    , m_recv_thread_running(false)
    , m_packet_callback(packet_callback) {

    LOG_VERBOSE("P2pProxyClient created");
}

/**
 * @brief Destructor
 *
 * Ensures clean shutdown of socket and receive thread.
 */
P2pProxyClient::~P2pProxyClient() {
    m_disposed = true;
    Disconnect();
    LOG_VERBOSE("P2pProxyClient destroyed");
}

// =============================================================================
// Connection
// =============================================================================

/**
 * @brief Connect to a P2P host using string address
 *
 * @param address Host IP address as dotted decimal string (e.g., "192.168.1.100")
 * @param port Host port number
 * @return true if connection established successfully
 *
 * ## Implementation Notes
 *
 * 1. Parse IP address using inet_pton()
 * 2. Create TCP socket
 * 3. Set socket options (TCP_NODELAY for low latency)
 * 4. Connect with timeout
 * 5. Start receive thread
 */
bool P2pProxyClient::Connect(const char* address, uint16_t port) {
    if (address == nullptr) {
        LOG_ERROR("P2P client: null address");
        return false;
    }

    // Parse IP address
    struct in_addr addr;
    if (inet_pton(AF_INET, address, &addr) != 1) {
        LOG_ERROR("P2P client: invalid address '%s'", address);
        return false;
    }

    // Convert to byte array and call the other overload
    return Connect(reinterpret_cast<const uint8_t*>(&addr.s_addr), 4, port);
}

/**
 * @brief Connect to a P2P host using IP bytes
 *
 * @param ip_bytes IP address as byte array (network order)
 * @param ip_len Length of IP address (4 for IPv4)
 * @param port Host port number
 * @return true if connection established successfully
 *
 * This overload is used when we have the IP address from ExternalProxyConfig.
 */
bool P2pProxyClient::Connect(const uint8_t* ip_bytes, size_t ip_len, uint16_t port) {
    std::scoped_lock lock(m_mutex);

    if (m_connected) {
        LOG_WARN("P2P client: already connected");
        return true;
    }

    if (ip_bytes == nullptr || ip_len < 4) {
        LOG_ERROR("P2P client: invalid IP bytes");
        return false;
    }

    // =========================================================================
    // Step 1: Create TCP Socket
    // =========================================================================

    m_socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket_fd < 0) {
        LOG_ERROR("P2P client: failed to create socket (errno=%d)", errno);
        return false;
    }

    // =========================================================================
    // Step 2: Configure Socket Options
    // =========================================================================

    // TCP_NODELAY - disable Nagle's algorithm for low latency
    int nodelay = 1;
    if (setsockopt(m_socket_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
        LOG_WARN("P2P client: failed to set TCP_NODELAY (errno=%d)", errno);
    }

    // Set non-blocking for connect with timeout
    int flags = fcntl(m_socket_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(m_socket_fd, F_SETFL, flags | O_NONBLOCK);
    }

    // =========================================================================
    // Step 3: Build Server Address
    // =========================================================================

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    std::memcpy(&server_addr.sin_addr.s_addr, ip_bytes, 4);

    // Log connection attempt
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &server_addr.sin_addr, ip_str, sizeof(ip_str));
    LOG_INFO("P2P client: connecting to %s:%u", ip_str, port);

    // =========================================================================
    // Step 4: Connect with Timeout
    // =========================================================================

    int result = ::connect(m_socket_fd, reinterpret_cast<struct sockaddr*>(&server_addr),
                           sizeof(server_addr));

    if (result < 0 && errno != EINPROGRESS) {
        LOG_ERROR("P2P client: connect failed (errno=%d)", errno);
        close(m_socket_fd);
        m_socket_fd = -1;
        return false;
    }

    // Wait for connection with timeout using select()
    if (result < 0) {
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(m_socket_fd, &write_fds);

        struct timeval timeout;
        timeout.tv_sec = CONNECT_TIMEOUT_MS / 1000;
        timeout.tv_usec = (CONNECT_TIMEOUT_MS % 1000) * 1000;

        result = select(m_socket_fd + 1, nullptr, &write_fds, nullptr, &timeout);

        if (result <= 0) {
            LOG_ERROR("P2P client: connect timeout or error (result=%d, errno=%d)",
                      result, errno);
            close(m_socket_fd);
            m_socket_fd = -1;
            return false;
        }

        // Check if connection succeeded
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (getsockopt(m_socket_fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
            LOG_ERROR("P2P client: connect failed (so_error=%d)", so_error);
            close(m_socket_fd);
            m_socket_fd = -1;
            return false;
        }
    }

    // =========================================================================
    // Step 5: Set Back to Blocking Mode
    // =========================================================================

    if (flags >= 0) {
        fcntl(m_socket_fd, F_SETFL, flags);  // Remove O_NONBLOCK
    }

    // =========================================================================
    // Step 6: Start Receive Thread
    // =========================================================================

    m_connected = true;
    m_recv_thread_running = true;

    Result rc = os::CreateThread(&m_recv_thread, ClientRecvThreadEntry, this,
                                  m_recv_thread_stack, sizeof(m_recv_thread_stack),
                                  /* priority */ 0x2C);

    if (R_FAILED(rc)) {
        LOG_ERROR("P2P client: failed to create recv thread (rc=0x%X)", rc.GetValue());
        close(m_socket_fd);
        m_socket_fd = -1;
        m_connected = false;
        m_recv_thread_running = false;
        return false;
    }

    os::StartThread(&m_recv_thread);

    LOG_INFO("P2P client: connected to %s:%u", ip_str, port);
    return true;
}

/**
 * @brief Disconnect from the host
 *
 * Stops the receive thread and closes the socket.
 * Safe to call multiple times.
 */
void P2pProxyClient::Disconnect() {
    std::scoped_lock lock(m_mutex);

    if (!m_connected && m_socket_fd < 0) {
        return;
    }

    LOG_INFO("P2P client: disconnecting");

    // Stop receive thread
    m_recv_thread_running = false;

    // Close socket to unblock recv()
    if (m_socket_fd >= 0) {
        shutdown(m_socket_fd, SHUT_RDWR);
        close(m_socket_fd);
        m_socket_fd = -1;
    }

    // Wait for receive thread to finish
    if (m_connected) {
        // We need to release the lock temporarily to avoid deadlock
        // since the recv thread might try to acquire it
        m_mutex.Unlock();
        os::WaitThread(&m_recv_thread);
        os::DestroyThread(&m_recv_thread);
        m_mutex.Lock();
    }

    m_connected = false;
    m_ready = false;

    // Signal any waiting threads
    m_ready_cv.Broadcast();
}

/**
 * @brief Check if connected to host
 */
bool P2pProxyClient::IsConnected() const {
    std::scoped_lock lock(m_mutex);
    return m_connected;
}

// =============================================================================
// Authentication
// =============================================================================

/**
 * @brief Perform P2P authentication
 *
 * Sends the ExternalProxyConfig packet to the host. The host validates the
 * authentication token and responds with ProxyConfig on success.
 *
 * @param config ExternalProxyConfig received from master server
 * @return true if authentication packet was sent successfully
 *
 * ## Ryujinx Reference
 *
 * ```csharp
 * public bool PerformAuth(ExternalProxyConfig config) {
 *     _connected.WaitOne(FailureTimeout);
 *     SendAsync(_protocol.Encode(PacketId.ExternalProxy, config));
 *     return true;
 * }
 * ```
 */
bool P2pProxyClient::PerformAuth(const ryu_ldn::protocol::ExternalProxyConfig& config) {
    std::scoped_lock lock(m_mutex);

    if (!m_connected) {
        LOG_ERROR("P2P client: cannot auth - not connected");
        return false;
    }

    LOG_INFO("P2P client: performing authentication");

    // Encode ExternalProxyConfig packet
    uint8_t packet[256];
    size_t len = 0;
    ryu_ldn::protocol::encode(packet, sizeof(packet),
                              ryu_ldn::protocol::PacketId::ExternalProxy,
                              config, len);

    // Send to host
    ssize_t sent = send(m_socket_fd, packet, len, 0);
    if (sent < 0 || static_cast<size_t>(sent) != len) {
        LOG_ERROR("P2P client: failed to send auth packet (sent=%zd, errno=%d)",
                  sent, errno);
        return false;
    }

    LOG_VERBOSE("P2P client: auth packet sent (%zu bytes)", len);
    return true;
}

/**
 * @brief Wait for proxy to be ready
 *
 * Blocks until the host sends ProxyConfig (indicating successful authentication)
 * or timeout expires.
 *
 * @param timeout_ms Timeout in milliseconds
 * @return true if ProxyConfig received within timeout
 *
 * ## Ryujinx Reference
 *
 * ```csharp
 * public bool EnsureProxyReady() {
 *     return _ready.WaitOne(FailureTimeout);
 * }
 * ```
 */
bool P2pProxyClient::EnsureProxyReady(int timeout_ms) {
    std::scoped_lock lock(m_mutex);

    if (m_ready) {
        return true;
    }

    if (!m_connected) {
        LOG_ERROR("P2P client: cannot wait for ready - not connected");
        return false;
    }

    LOG_INFO("P2P client: waiting for proxy ready (timeout=%dms)", timeout_ms);

    // Wait on condition variable with timeout
    const auto wait_time = TimeSpan::FromMilliSeconds(timeout_ms);
    m_ready_cv.TimedWait(m_mutex, wait_time);

    if (m_ready) {
        LOG_INFO("P2P client: proxy is ready (virtual IP: 0x%08X)",
                 m_proxy_config.proxy_ip);
        return true;
    }

    LOG_WARN("P2P client: proxy ready timeout");
    return false;
}

/**
 * @brief Check if proxy is ready
 */
bool P2pProxyClient::IsReady() const {
    std::scoped_lock lock(m_mutex);
    return m_ready;
}

// =============================================================================
// Sending Proxy Messages
// =============================================================================

/**
 * @brief Send raw packet data to host
 */
bool P2pProxyClient::Send(const void* data, size_t size) {
    std::scoped_lock lock(m_mutex);

    if (!m_connected || m_socket_fd < 0) {
        return false;
    }

    ssize_t sent = send(m_socket_fd, data, size, 0);
    return sent >= 0 && static_cast<size_t>(sent) == size;
}

/**
 * @brief Send ProxyData to host
 */
bool P2pProxyClient::SendProxyData(const ryu_ldn::protocol::ProxyDataHeader& header,
                                    const uint8_t* data, size_t data_len) {
    uint8_t packet[0x10000];  // 64KB max
    size_t len = 0;
    ryu_ldn::protocol::encode_with_data(packet, sizeof(packet),
                                        ryu_ldn::protocol::PacketId::ProxyData,
                                        header, data, data_len, len);
    return Send(packet, len);
}

/**
 * @brief Send ProxyConnect request to host
 */
bool P2pProxyClient::SendProxyConnect(const ryu_ldn::protocol::ProxyConnectRequest& request) {
    uint8_t packet[256];
    size_t len = 0;
    ryu_ldn::protocol::encode(packet, sizeof(packet),
                              ryu_ldn::protocol::PacketId::ProxyConnect,
                              request, len);
    return Send(packet, len);
}

/**
 * @brief Send ProxyConnectReply to host
 */
bool P2pProxyClient::SendProxyConnectReply(const ryu_ldn::protocol::ProxyConnectResponse& response) {
    uint8_t packet[256];
    size_t len = 0;
    ryu_ldn::protocol::encode(packet, sizeof(packet),
                              ryu_ldn::protocol::PacketId::ProxyConnectReply,
                              response, len);
    return Send(packet, len);
}

/**
 * @brief Send ProxyDisconnect to host
 */
bool P2pProxyClient::SendProxyDisconnect(const ryu_ldn::protocol::ProxyDisconnectMessage& message) {
    uint8_t packet[256];
    size_t len = 0;
    ryu_ldn::protocol::encode(packet, sizeof(packet),
                              ryu_ldn::protocol::PacketId::ProxyDisconnect,
                              message, len);
    return Send(packet, len);
}

// =============================================================================
// Receive Thread
// =============================================================================

/**
 * @brief Receive loop thread function
 *
 * Continuously receives data from the socket and processes packets.
 * Runs until Disconnect() is called or connection is lost.
 */
void P2pProxyClient::ReceiveLoop() {
    LOG_VERBOSE("P2P client: recv thread started");

    while (m_recv_thread_running && !m_disposed) {
        // Receive data (blocking)
        ssize_t received = recv(m_socket_fd, m_recv_buffer, RECV_BUFFER_SIZE, 0);

        if (received <= 0) {
            if (received == 0) {
                LOG_INFO("P2P client: connection closed by host");
            } else if (errno != EINTR && m_recv_thread_running) {
                LOG_ERROR("P2P client: recv error (errno=%d)", errno);
            }
            break;
        }

        // Process received data
        ProcessData(m_recv_buffer, static_cast<size_t>(received));
    }

    LOG_VERBOSE("P2P client: recv thread exiting");

    // Mark as disconnected if we exited due to error
    {
        std::scoped_lock lock(m_mutex);
        if (m_connected && !m_disposed) {
            m_connected = false;
            m_ready = false;
            m_ready_cv.Broadcast();
        }
    }
}

/**
 * @brief Process received data
 *
 * Parses the received buffer and dispatches each complete packet to
 * the appropriate handler.
 *
 * @param data Buffer containing received data
 * @param size Number of bytes in buffer
 */
void P2pProxyClient::ProcessData(const uint8_t* data, size_t size) {
    size_t offset = 0;

    while (offset < size) {
        // Need at least header
        if (size - offset < sizeof(ryu_ldn::protocol::LdnHeader)) {
            LOG_WARN("P2P client: incomplete header");
            break;
        }

        const auto* header = reinterpret_cast<const ryu_ldn::protocol::LdnHeader*>(data + offset);

        // Validate magic
        if (header->magic != ryu_ldn::protocol::PROTOCOL_MAGIC) {
            LOG_WARN("P2P client: invalid magic 0x%08X", header->magic);
            break;
        }

        // Calculate total packet size
        size_t packet_size = sizeof(ryu_ldn::protocol::LdnHeader) +
                             static_cast<size_t>(header->data_size);

        // Need complete packet
        if (offset + packet_size > size) {
            LOG_WARN("P2P client: incomplete packet (need %zu, have %zu)",
                     packet_size, size - offset);
            break;
        }

        // Get packet payload
        const uint8_t* packet_data = data + offset + sizeof(ryu_ldn::protocol::LdnHeader);

        // Dispatch by packet type
        switch (static_cast<ryu_ldn::protocol::PacketId>(header->type)) {
            case ryu_ldn::protocol::PacketId::ProxyConfig: {
                if (static_cast<size_t>(header->data_size) >= sizeof(ryu_ldn::protocol::ProxyConfig)) {
                    const auto* config = reinterpret_cast<const ryu_ldn::protocol::ProxyConfig*>(packet_data);
                    HandleProxyConfig(*config);
                }
                break;
            }

            case ryu_ldn::protocol::PacketId::ProxyData: {
                if (static_cast<size_t>(header->data_size) >= sizeof(ryu_ldn::protocol::ProxyDataHeader)) {
                    const auto* pheader = reinterpret_cast<const ryu_ldn::protocol::ProxyDataHeader*>(packet_data);
                    const uint8_t* payload = packet_data + sizeof(ryu_ldn::protocol::ProxyDataHeader);
                    size_t payload_len = static_cast<size_t>(header->data_size) -
                                         sizeof(ryu_ldn::protocol::ProxyDataHeader);
                    HandleProxyData(*pheader, payload, payload_len);
                }
                break;
            }

            case ryu_ldn::protocol::PacketId::ProxyConnect: {
                if (static_cast<size_t>(header->data_size) >= sizeof(ryu_ldn::protocol::ProxyConnectRequest)) {
                    const auto* request = reinterpret_cast<const ryu_ldn::protocol::ProxyConnectRequest*>(packet_data);
                    HandleProxyConnect(*request);
                }
                break;
            }

            case ryu_ldn::protocol::PacketId::ProxyConnectReply: {
                if (static_cast<size_t>(header->data_size) >= sizeof(ryu_ldn::protocol::ProxyConnectResponse)) {
                    const auto* response = reinterpret_cast<const ryu_ldn::protocol::ProxyConnectResponse*>(packet_data);
                    HandleProxyConnectReply(*response);
                }
                break;
            }

            case ryu_ldn::protocol::PacketId::ProxyDisconnect: {
                if (static_cast<size_t>(header->data_size) >= sizeof(ryu_ldn::protocol::ProxyDisconnectMessage)) {
                    const auto* message = reinterpret_cast<const ryu_ldn::protocol::ProxyDisconnectMessage*>(packet_data);
                    HandleProxyDisconnect(*message);
                }
                break;
            }

            default:
                LOG_VERBOSE("P2P client: unknown packet type %u", header->type);
                break;
        }

        offset += packet_size;
    }
}

// =============================================================================
// Packet Handlers
// =============================================================================

/**
 * @brief Handle ProxyConfig from host
 *
 * This packet indicates successful authentication. The ProxyConfig contains
 * our assigned virtual IP and subnet mask.
 *
 * ## Ryujinx Reference
 *
 * ```csharp
 * private void HandleProxyConfig(LdnHeader header, ProxyConfig config) {
 *     ProxyConfig = config;
 *     SocketHelpers.RegisterProxy(new LdnProxy(config, this, _protocol));
 *     _ready.Set();
 * }
 * ```
 */
void P2pProxyClient::HandleProxyConfig(const ryu_ldn::protocol::ProxyConfig& config) {
    std::scoped_lock lock(m_mutex);

    LOG_INFO("P2P client: received ProxyConfig (IP: 0x%08X, mask: 0x%08X)",
             config.proxy_ip, config.proxy_subnet_mask);

    // Store config
    m_proxy_config = config;

    // Mark as ready
    m_ready = true;
    m_ready_cv.Broadcast();
}

/**
 * @brief Handle ProxyData from host
 *
 * Forward to the BSD MITM layer via callback.
 */
void P2pProxyClient::HandleProxyData(const ryu_ldn::protocol::ProxyDataHeader& header,
                                      const uint8_t* data, size_t data_len) {
    LOG_VERBOSE("P2P client: received ProxyData (%zu bytes)", data_len);

    if (m_packet_callback) {
        // Create combined buffer for callback
        size_t total_size = sizeof(header) + data_len;
        uint8_t buffer[0x10000];
        if (total_size <= sizeof(buffer)) {
            std::memcpy(buffer, &header, sizeof(header));
            std::memcpy(buffer + sizeof(header), data, data_len);
            m_packet_callback(ryu_ldn::protocol::PacketId::ProxyData, buffer, total_size);
        }
    }
}

/**
 * @brief Handle ProxyConnect from host
 *
 * Forward to the BSD MITM layer via callback.
 */
void P2pProxyClient::HandleProxyConnect(const ryu_ldn::protocol::ProxyConnectRequest& request) {
    LOG_VERBOSE("P2P client: received ProxyConnect");

    if (m_packet_callback) {
        m_packet_callback(ryu_ldn::protocol::PacketId::ProxyConnect,
                          &request, sizeof(request));
    }
}

/**
 * @brief Handle ProxyConnectReply from host
 *
 * Forward to the BSD MITM layer via callback.
 */
void P2pProxyClient::HandleProxyConnectReply(const ryu_ldn::protocol::ProxyConnectResponse& response) {
    LOG_VERBOSE("P2P client: received ProxyConnectReply");

    if (m_packet_callback) {
        m_packet_callback(ryu_ldn::protocol::PacketId::ProxyConnectReply,
                          &response, sizeof(response));
    }
}

/**
 * @brief Handle ProxyDisconnect from host
 *
 * Forward to the BSD MITM layer via callback.
 */
void P2pProxyClient::HandleProxyDisconnect(const ryu_ldn::protocol::ProxyDisconnectMessage& message) {
    LOG_VERBOSE("P2P client: received ProxyDisconnect");

    if (m_packet_callback) {
        m_packet_callback(ryu_ldn::protocol::PacketId::ProxyDisconnect,
                          &message, sizeof(message));
    }
}

} // namespace ams::mitm::p2p
