/**
 * @file p2p_proxy_server.hpp
 * @brief P2P Proxy Server - Direct TCP server for hosting P2P sessions
 *
 * This file defines the P2pProxyServer class which allows a Nintendo Switch
 * to host direct P2P connections from other players, bypassing the relay server
 * for reduced latency.
 *
 * ## Architecture
 *
 * ```
 *                    ┌─────────────────────┐
 *                    │  RyuLDN Server      │
 *                    │  (Master Server)    │
 *                    └──────────┬──────────┘
 *                               │ ExternalProxyToken
 *                    ┌──────────▼──────────┐
 *                    │  P2pProxyServer     │◄────── Joiner P2pProxyClient
 *                    │  (Switch Host)      │
 *                    │  TCP:39990-39999    │◄────── Joiner P2pProxyClient
 *                    └─────────────────────┘
 * ```
 *
 * ## Flow
 *
 * 1. Host calls Start() - begins listening on TCP port
 * 2. Host calls NatPunch() - UPnP opens public port
 * 3. Master server sends ExternalProxyToken for each joiner
 * 4. Joiner connects via TCP, sends ExternalProxyConfig
 * 5. TryRegisterUser() validates token, assigns virtual IP
 * 6. ProxyData/ProxyConnect/etc routed between sessions
 *
 * ## Ryujinx Compatibility
 *
 * This implementation mirrors Ryujinx's P2pProxyServer:
 * - Port range: 39990-39999
 * - Lease duration: 60 seconds
 * - Lease renewal: 50 seconds
 * - Auth timeout: 1 second
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include "../protocol/types.hpp"
#include "../protocol/ryu_protocol.hpp"
#include "upnp_port_mapper.hpp"

namespace ams::mitm::p2p {

// Forward declaration
class P2pProxySession;

/**
 * @brief Callback type for sending data to the master server
 *
 * Used to notify the master server of connection state changes.
 *
 * @param data Encoded packet data
 * @param size Size of data
 * @param user_data User-provided context pointer
 */
using MasterSendCallback = void(*)(const void* data, size_t size, void* user_data);

/**
 * @brief P2P Proxy Server
 *
 * TCP server that hosts direct P2P connections for LDN multiplayer.
 * When a Switch creates a network (hosts), this server accepts connections
 * from other players who join via P2P instead of through the relay server.
 *
 * ## Thread Safety
 *
 * All public methods are thread-safe. Internal state is protected by mutex.
 *
 * ## Lifecycle
 *
 * 1. Create server
 * 2. Start(port) - begin listening
 * 3. NatPunch() - open UPnP port (optional but recommended)
 * 4. Accept connections, validate tokens
 * 5. Route proxy messages between sessions
 * 6. Stop() - cleanup and close
 */
class P2pProxyServer {
public:
    // =========================================================================
    // Constants (matching Ryujinx)
    // =========================================================================

    static constexpr uint16_t PRIVATE_PORT_BASE = 39990;
    static constexpr int PRIVATE_PORT_RANGE = 10;
    static constexpr uint16_t PUBLIC_PORT_BASE = 39990;
    static constexpr int PUBLIC_PORT_RANGE = 10;
    static constexpr int PORT_LEASE_LENGTH = 60;   // seconds
    static constexpr int PORT_LEASE_RENEW = 50;    // seconds
    static constexpr int AUTH_WAIT_SECONDS = 1;
    static constexpr int MAX_PLAYERS = 8;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Constructor
     * @param master_callback Callback to send data to master server
     * @param user_data User data passed to callback
     */
    explicit P2pProxyServer(MasterSendCallback master_callback = nullptr,
                            void* user_data = nullptr);

    /**
     * @brief Destructor - stops server and cleans up
     */
    ~P2pProxyServer();

    // Non-copyable
    P2pProxyServer(const P2pProxyServer&) = delete;
    P2pProxyServer& operator=(const P2pProxyServer&) = delete;

    // =========================================================================
    // Server Control
    // =========================================================================

    /**
     * @brief Start the TCP server
     * @param port Port to listen on (default: auto-select from range)
     * @return true if server started successfully
     *
     * @note Tries ports 39990-39999 if port is 0
     */
    bool Start(uint16_t port = 0);

    /**
     * @brief Stop the server and disconnect all sessions
     */
    void Stop();

    /**
     * @brief Check if server is running
     */
    bool IsRunning() const;

    /**
     * @brief Get the private (local) port
     */
    uint16_t GetPrivatePort() const { return m_private_port; }

    /**
     * @brief Get the public (UPnP) port
     */
    uint16_t GetPublicPort() const { return m_public_port; }

    // =========================================================================
    // UPnP NAT Punch
    // =========================================================================

    /**
     * @brief Open a public port via UPnP
     * @return Public port number, or 0 if UPnP failed
     *
     * Attempts to open a port mapping on the router using UPnP.
     * Tries ports 39990-39999 until one succeeds.
     *
     * If successful, starts a lease renewal thread.
     */
    uint16_t NatPunch();

    /**
     * @brief Release UPnP port mapping
     */
    void ReleaseNatPunch();

    // =========================================================================
    // Token Management
    // =========================================================================

    /**
     * @brief Add a waiting token for an expected joiner
     * @param token Token from master server
     *
     * Called when master server notifies us someone is about to connect.
     */
    void AddWaitingToken(const ryu_ldn::protocol::ExternalProxyToken& token);

    /**
     * @brief Try to register a connecting user
     * @param session The session attempting to register
     * @param config The auth config sent by the client
     * @param remote_ip Remote IP address (for validation)
     * @return true if registration succeeded
     *
     * Called by P2pProxySession when a client sends ExternalProxyConfig.
     * Validates the token, assigns virtual IP, and adds to player list.
     */
    bool TryRegisterUser(P2pProxySession* session,
                         const ryu_ldn::protocol::ExternalProxyConfig& config,
                         uint32_t remote_ip);

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Configure broadcast address from ProxyConfig
     * @param config Configuration with subnet info
     */
    void Configure(const ryu_ldn::protocol::ProxyConfig& config);

    // =========================================================================
    // Proxy Message Routing
    // =========================================================================

    /**
     * @brief Handle ProxyData message from a session
     */
    void HandleProxyData(P2pProxySession* sender,
                         ryu_ldn::protocol::ProxyDataHeader& header,
                         const uint8_t* data, size_t data_len);

    /**
     * @brief Handle ProxyConnect message from a session
     */
    void HandleProxyConnect(P2pProxySession* sender,
                            ryu_ldn::protocol::ProxyConnectRequest& request);

    /**
     * @brief Handle ProxyConnectReply message from a session
     */
    void HandleProxyConnectReply(P2pProxySession* sender,
                                  ryu_ldn::protocol::ProxyConnectResponse& response);

    /**
     * @brief Handle ProxyDisconnect message from a session
     */
    void HandleProxyDisconnect(P2pProxySession* sender,
                               ryu_ldn::protocol::ProxyDisconnectMessage& message);

    /**
     * @brief Called when a session disconnects
     */
    void OnSessionDisconnected(P2pProxySession* session);

private:
    // =========================================================================
    // Friend declarations for thread entry points
    // =========================================================================
    friend void AcceptThreadEntry(void* arg);
    friend void LeaseThreadEntry(void* arg);

    // =========================================================================
    // Internal Methods
    // =========================================================================

    /**
     * @brief Accept loop thread function
     */
    void AcceptLoop();

    /**
     * @brief Route a message to appropriate session(s)
     * @param sender The session that sent the message
     * @param info Proxy info with source/dest IPs
     * @param send_func Function to call for each target session
     */
    template<typename SendFunc>
    void RouteMessage(P2pProxySession* sender,
                      ryu_ldn::protocol::ProxyInfo& info,
                      SendFunc send_func);

    /**
     * @brief Notify master server of connection state change
     */
    void NotifyMasterDisconnect(uint32_t virtual_ip);

    /**
     * @brief Lease renewal thread function
     */
    void LeaseRenewalLoop();

    /**
     * @brief Start lease renewal background thread
     */
    void StartLeaseRenewal();

    // =========================================================================
    // Member Variables
    // =========================================================================

    mutable os::Mutex m_mutex{false};

    // Server socket
    int m_listen_fd;
    uint16_t m_private_port;
    uint16_t m_public_port;
    bool m_running;
    bool m_disposed;

    // Accept thread
    os::ThreadType m_accept_thread;
    alignas(0x1000) uint8_t m_accept_thread_stack[0x4000];

    // Lease renewal thread
    os::ThreadType m_lease_thread;
    alignas(0x1000) uint8_t m_lease_thread_stack[0x2000];
    bool m_lease_thread_running;

    // Sessions
    P2pProxySession* m_sessions[MAX_PLAYERS];
    int m_session_count;

    // Waiting tokens for auth
    static constexpr int MAX_WAITING_TOKENS = 16;
    ryu_ldn::protocol::ExternalProxyToken m_waiting_tokens[MAX_WAITING_TOKENS];
    int m_waiting_token_count;
    os::ConditionVariable m_token_cv;

    // Network config
    uint32_t m_broadcast_address;

    // Master server callback
    MasterSendCallback m_master_callback;
    void* m_callback_user_data;
};

/**
 * @brief P2P Proxy Session
 *
 * Represents a single TCP connection from a P2P client.
 * Handles protocol parsing and delegates to parent server.
 */
class P2pProxySession {
public:
    /**
     * @brief Constructor
     * @param server Parent server
     * @param socket_fd Connected socket file descriptor
     * @param remote_ip Remote IP address
     */
    P2pProxySession(P2pProxyServer* server, int socket_fd, uint32_t remote_ip);

    /**
     * @brief Destructor
     */
    ~P2pProxySession();

    // Non-copyable
    P2pProxySession(const P2pProxySession&) = delete;
    P2pProxySession& operator=(const P2pProxySession&) = delete;

    // =========================================================================
    // Session Info
    // =========================================================================

    /**
     * @brief Get assigned virtual IP
     */
    uint32_t GetVirtualIpAddress() const { return m_virtual_ip; }

    /**
     * @brief Set virtual IP (called after auth)
     */
    void SetVirtualIp(uint32_t ip) { m_virtual_ip = ip; }

    /**
     * @brief Get remote (physical) IP
     */
    uint32_t GetRemoteIp() const { return m_remote_ip; }

    /**
     * @brief Check if session is authenticated
     */
    bool IsAuthenticated() const { return m_authenticated; }

    /**
     * @brief Mark as authenticated
     */
    void SetAuthenticated(bool auth) { m_authenticated = auth; }

    // =========================================================================
    // Network Operations
    // =========================================================================

    /**
     * @brief Start receive loop thread
     */
    void Start();

    /**
     * @brief Send data to the client
     * @return true if send succeeded
     */
    bool Send(const void* data, size_t size);

    /**
     * @brief Disconnect and stop
     * @param from_master true if disconnect was initiated by master server
     */
    void Disconnect(bool from_master = false);

    /**
     * @brief Check if session is connected
     */
    bool IsConnected() const { return m_connected; }

private:
    // Friend for thread entry point
    friend void SessionRecvThreadEntry(void* arg);

    /**
     * @brief Receive loop thread function
     */
    void ReceiveLoop();

    /**
     * @brief Process received data
     */
    void ProcessData(const uint8_t* data, size_t size);

    // Protocol handlers
    void HandleExternalProxy(const ryu_ldn::protocol::ExternalProxyConfig& config);
    void HandleProxyData(const ryu_ldn::protocol::ProxyDataHeader& header,
                         const uint8_t* data, size_t data_len);
    void HandleProxyConnect(const ryu_ldn::protocol::ProxyConnectRequest& request);
    void HandleProxyConnectReply(const ryu_ldn::protocol::ProxyConnectResponse& response);
    void HandleProxyDisconnect(const ryu_ldn::protocol::ProxyDisconnectMessage& message);

    // =========================================================================
    // Member Variables
    // =========================================================================

    P2pProxyServer* m_server;
    int m_socket_fd;
    uint32_t m_remote_ip;
    uint32_t m_virtual_ip;
    bool m_connected;
    bool m_authenticated;
    bool m_master_closed;

    // Receive thread
    os::ThreadType m_recv_thread;
    alignas(0x1000) uint8_t m_recv_thread_stack[0x4000];

    // Receive buffer
    static constexpr size_t RECV_BUFFER_SIZE = 0x10000;
    uint8_t m_recv_buffer[RECV_BUFFER_SIZE];
};

} // namespace ams::mitm::p2p
