/**
 * @file p2p_proxy_client.hpp
 * @brief P2P Proxy Client - Direct TCP client for joining P2P sessions
 *
 * This file defines the P2pProxyClient class which allows a Nintendo Switch
 * to connect directly to another player hosting a P2P session, bypassing the
 * relay server for reduced latency.
 *
 * ## Architecture
 *
 * ```
 *                    ┌─────────────────────┐
 *                    │  RyuLDN Server      │
 *                    │  (Master Server)    │
 *                    └──────────┬──────────┘
 *                               │ ExternalProxyConfig
 *                               ▼
 *                    ┌─────────────────────┐
 *                    │  P2pProxyClient     │───────► P2pProxyServer (Host)
 *                    │  (Switch Joiner)    │         TCP:39990-39999
 *                    └─────────────────────┘
 * ```
 *
 * ## Flow
 *
 * 1. Master server sends ExternalProxyConfig to joiner
 * 2. Joiner creates P2pProxyClient and calls Connect()
 * 3. Client sends ExternalProxyConfig for authentication
 * 4. Host validates token and sends ProxyConfig
 * 5. Client is now ready for direct P2P communication
 *
 * ## Ryujinx Compatibility
 *
 * This implementation mirrors Ryujinx's P2pProxyClient:
 * - Auth timeout: 4 seconds (FAILURE_TIMEOUT)
 * - Same packet format for ExternalProxyConfig
 * - Same ProxyConfig response handling
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include "../protocol/types.hpp"
#include "../protocol/ryu_protocol.hpp"

namespace ams::mitm::p2p {

/**
 * @brief Callback type for forwarding received proxy packets
 *
 * When the P2P client receives ProxyData, ProxyConnect, etc. from the host,
 * it forwards them to the BSD MITM layer via this callback.
 */
using ProxyPacketCallback = void(*)(ryu_ldn::protocol::PacketId type,
                                     const void* data, size_t size);

/**
 * @brief P2P Proxy Client
 *
 * TCP client that connects to a P2P host for direct LDN multiplayer.
 * When a Switch joins a network with P2P enabled, this client establishes
 * a direct connection to the host instead of going through the relay server.
 *
 * ## Security / Encryption
 *
 * P2P traffic is carried **in cleartext** over the direct TCP connection.
 * There is no TLS, no DTLS, and no application-layer encryption of proxy
 * packets. This is a deliberate architectural choice:
 *
 *   - It is consistent with the rest of the sysmodule: the relay-server
 *     path (RyuLdnClient → Ryujinx LDN server) is plain raw TCP (the LDN
 *     protocol carries no confidentiality guarantees — it transports PIA
 *     mesh frames that the game layer is responsible for protecting if
 *     needed).
 *   - Adding TLS to the P2P leg would require a shared trust anchor
 *     between peers that the current server-mediated auth
 *     (ExternalProxyConfig token) does not provide; it would also push
 *     the sysmodule past its memory budget (mbedTLS context + buffers).
 *   - The threat model is "LDN multiplayer traffic between consenting
 *     peers on a trusted network", not "confidential payload over an
 *     untrusted link". Peers who need confidentiality should use a VPN
 *     at the OS level.
 *
 * Anyone reusing this code in a context where the P2P link crosses an
 * untrusted network MUST add transport encryption — do not assume the
 * cleartext default is safe for arbitrary deployments.
 *
 * ## Thread Safety
 *
 * All public methods are thread-safe. Internal state is protected by mutex.
 *
 * ## Lifecycle
 *
 * 1. Create client with packet callback
 * 2. Connect(address, port) - establish TCP connection
 * 3. PerformAuth(config) - send authentication
 * 4. EnsureProxyReady() - wait for ProxyConfig
 * 5. Send/receive proxy messages
 * 6. Disconnect() - cleanup
 */
class P2pProxyClient {
public:
    // =========================================================================
    // Constants (matching Ryujinx)
    // =========================================================================

    /** @brief Timeout for authentication and ready wait (milliseconds) */
    static constexpr int FAILURE_TIMEOUT_MS = 4000;

    /** @brief Connection timeout (milliseconds) */
    static constexpr int CONNECT_TIMEOUT_MS = 5000;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Constructor
     * @param packet_callback Callback for forwarding received packets
     */
    explicit P2pProxyClient(ProxyPacketCallback packet_callback = nullptr);

    /**
     * @brief Destructor - disconnects and cleans up
     */
    ~P2pProxyClient();

    // Non-copyable
    P2pProxyClient(const P2pProxyClient&) = delete;
    P2pProxyClient& operator=(const P2pProxyClient&) = delete;

    // =========================================================================
    // Connection
    // =========================================================================

    /**
     * @brief Connect to a P2P host
     * @param address Host IP address (dotted decimal string)
     * @param port Host port number
     * @return true if connection established
     *
     * Establishes a TCP connection to the P2P host server.
     * Does not perform authentication - call PerformAuth() after connecting.
     */
    /// @gdb{tag="P2P:CLIENT", msg="Client connecting"}
    bool Connect(const char* address, uint16_t port);

    /**
     * @brief Connect to a P2P host using IP bytes
     * @param ip_bytes IP address as byte array (network order)
     * @param ip_len Length of IP address (4 for IPv4)
     * @param port Host port number
     * @return true if connection established
     */
    /// @gdb{tag="P2P:CLIENT", msg="Client connecting (IP bytes)"}
    bool Connect(const uint8_t* ip_bytes, size_t ip_len, uint16_t port);

    /**
     * @brief Disconnect from the host
     */
    /// @gdb{tag="P2P:CLIENT", msg="Client disconnecting"}
    void Disconnect();

    /**
     * @brief Check if connected to host
     */
    /// @gdb{tag="P2P:CLIENT", msg="Connection state queried"}
    bool IsConnected() const;

    // =========================================================================
    // Authentication
    // =========================================================================

    /**
     * @brief Perform P2P authentication
     * @param config ExternalProxyConfig from master server
     * @return true if auth packet sent successfully
     *
     * Sends the ExternalProxyConfig to the host for token validation.
     * The host will respond with ProxyConfig if authentication succeeds.
     *
     * @note Call EnsureProxyReady() after this to wait for the response.
     */
    /// @gdb{tag="P2P:CLIENT", msg="Performing authentication"}
    bool PerformAuth(const ryu_ldn::protocol::ExternalProxyConfig& config);

    /**
     * @brief Wait for proxy to be ready
     * @param timeout_ms Timeout in milliseconds (default: FAILURE_TIMEOUT_MS)
     * @return true if ProxyConfig received within timeout
     *
     * Blocks until the host sends ProxyConfig (authentication success)
     * or timeout expires.
     */
    /// @gdb{tag="P2P:CLIENT", msg="Ensuring proxy ready"}
    bool EnsureProxyReady(int timeout_ms = FAILURE_TIMEOUT_MS);

    /**
     * @brief Check if proxy is ready (authenticated)
     */
    /// @gdb{tag="P2P:CLIENT", msg="Ready state queried"}
    bool IsReady() const;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get the received ProxyConfig
     * @return ProxyConfig from host (valid after EnsureProxyReady returns true)
     */
    const ryu_ldn::protocol::ProxyConfig& GetProxyConfig() const {
        return m_proxy_config;
    }

    /**
     * @brief Get our virtual IP address
     * @return Virtual IP assigned by host (valid after ready)
     */
    uint32_t GetVirtualIp() const { return m_proxy_config.proxy_ip; }

    // =========================================================================
    // Sending Proxy Messages
    // =========================================================================

    /**
     * @brief Send ProxyData to the host
     * @param header ProxyData header with routing info
     * @param data Payload data
     * @param data_len Payload length
     * @return true if sent successfully
     */
    /// @gdb{tag="P2P:MSG", msg="Client sending proxy data"}
    bool SendProxyData(const ryu_ldn::protocol::ProxyDataHeader& header,
                       const uint8_t* data, size_t data_len);

    /**
     * @brief Send ProxyConnect request to the host
     * @param request Connect request with destination info
     * @return true if sent successfully
     */
    /// @gdb{tag="P2P:MSG", msg="Client sending proxy connect"}
    bool SendProxyConnect(const ryu_ldn::protocol::ProxyConnectRequest& request);

    /**
     * @brief Send ProxyConnectReply to the host
     * @param response Connect response
     * @return true if sent successfully
     */
    /// @gdb{tag="P2P:MSG", msg="Client sending connect reply"}
    bool SendProxyConnectReply(const ryu_ldn::protocol::ProxyConnectResponse& response);

    /**
     * @brief Send ProxyDisconnect to the host
     * @param message Disconnect message
     * @return true if sent successfully
     */
    /// @gdb{tag="P2P:MSG", msg="Client sending disconnect"}
    bool SendProxyDisconnect(const ryu_ldn::protocol::ProxyDisconnectMessage& message);

    /**
     * @brief Send raw packet data
     * @param data Encoded packet
     * @param size Packet size
     * @return true if sent successfully
     */
    /// @gdb{tag="P2P:CLIENT", msg="Sending data"}
    bool Send(const void* data, size_t size);

private:
    // =========================================================================
    // Friend declarations for thread entry point
    // =========================================================================
    /// @gdb{tag="P2P:CLIENT", msg="Receive thread started"}
    friend void ClientRecvThreadEntry(void* arg);

    // =========================================================================
    // Internal Methods
    // =========================================================================

    /**
     * @brief Receive loop thread function
     */
    /// @gdb{tag="P2P:CLIENT", msg="Receive loop running"}
    void ReceiveLoop();

    /**
     * @brief Process received data
     * @param data Buffer containing packet(s)
     * @param size Size of data in buffer
     */
    /// @gdb{tag="P2P:CLIENT", msg="Processing received data"}
    void ProcessData(const uint8_t* data, size_t size);

    // Protocol handlers
    /// @gdb{tag="P2P:MSG", msg="Client handling proxy config"}
    void HandleProxyConfig(const ryu_ldn::protocol::ProxyConfig& config);
    /// @gdb{tag="P2P:MSG", msg="Client handling proxy data"}
    void HandleProxyData(const ryu_ldn::protocol::ProxyDataHeader& header,
                         const uint8_t* data, size_t data_len);
    /// @gdb{tag="P2P:MSG", msg="Client handling proxy connect"}
    void HandleProxyConnect(const ryu_ldn::protocol::ProxyConnectRequest& request);
    /// @gdb{tag="P2P:MSG", msg="Client handling connect reply"}
    void HandleProxyConnectReply(const ryu_ldn::protocol::ProxyConnectResponse& response);
    /// @gdb{tag="P2P:MSG", msg="Client handling disconnect"}
    void HandleProxyDisconnect(const ryu_ldn::protocol::ProxyDisconnectMessage& message);

    // =========================================================================
    // Member Variables
    // =========================================================================

    mutable os::Mutex m_mutex{false};

    // Socket
    int m_socket_fd;
    bool m_connected;
    bool m_disposed;

    // Authentication state
    bool m_ready;
    os::ConditionVariable m_ready_cv;

    // Proxy config from host
    ryu_ldn::protocol::ProxyConfig m_proxy_config;

    // Receive thread (stack lives in BSS — see g_p2p_client_recv_thread_stack
    // in p2p_proxy_client.cpp). Inlining `alignas(0x1000) uint8_t stack[N]`
    // here made the whole class over-aligned to 4 KB, so
    // `new P2pProxyClient(...)` in HandleExternalProxyConnect fell through
    // to the unimplemented aligned operator new and std::abort'd 0xFFE the
    // moment the server told us about the external proxy. Same root cause
    // as the P2pProxyServer/P2pProxySession fix.
    os::ThreadType m_recv_thread;
    bool m_recv_thread_running;

    // Receive buffer
    static constexpr size_t RECV_BUFFER_SIZE = 0x10000;
    uint8_t m_recv_buffer[RECV_BUFFER_SIZE];

    // Packet callback
    ProxyPacketCallback m_packet_callback;
};

} // namespace ams::mitm::p2p
