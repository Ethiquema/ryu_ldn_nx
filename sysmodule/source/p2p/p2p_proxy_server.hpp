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
#include <atomic>
#include <memory>
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
    /// @gdb{tag="P2P:SERVER", msg="Server starting"}
    bool Start(uint16_t port = 0);

    /**
     * @brief Stop the server and disconnect all sessions
     */
    /// @gdb{tag="P2P:SERVER", msg="Server stopping"}
    void Stop();

    /**
     * @brief Check if server is running
     */
    /// @gdb{tag="P2P:SERVER", msg="Server state queried"}
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
    /// @gdb{tag="P2P:NAT", msg="Releasing NAT punch"}
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
    /// @gdb{tag="P2P:SESSION", msg="Token added to waiting list"}
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
    /// @gdb{tag="P2P:SESSION", msg="User registration attempt"}
    bool TryRegisterUser(P2pProxySession* session,
                         const ryu_ldn::protocol::ExternalProxyConfig& config,
                         uint32_t remote_ip);

    /**
     * @brief Apply a master-originated ExternalProxyState change.
     *
     * Mirrors Ryujinx P2pProxyServer.HandleStateChange. When the master
     * tells us a player is no longer connected (because their join
     * didn't complete or they dropped out without notifying us), we have
     * to remove the matching waiting token AND disconnect the matching
     * session — otherwise the zombie session keeps the recv thread
     * alive and the token sits forever blocking the slot for retries.
     *
     * @param virtual_ip ExternalProxyConnectionState.IpAddress
     * @param connected  ExternalProxyConnectionState.Connected
     */
    void HandleExternalProxyStateChange(uint32_t virtual_ip, bool connected);

    /**
     * @brief Free disconnected sessions queued by OnSessionDisconnected
     *
     * Walks m_zombie_sessions, joins+destroys the recv thread of any
     * session whose m_thread_done atomic has flipped to true, then
     * `delete`s the session and shrinks the array. Called from
     * AcceptLoop on every iteration and from Stop() so disconnected
     * peers don't leak their 64 KB recv buffer (RECV_BUFFER_SIZE in
     * the header).
     */
    void ReapZombieSessions();

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Configure broadcast address from ProxyConfig
     * @param config Configuration with subnet info
     */
    /// @gdb{tag="P2P:SERVER", msg="Server configuration updated"}
    void Configure(const ryu_ldn::protocol::ProxyConfig& config);

    // =========================================================================
    // Host self-routing — in-process shortcut
    // =========================================================================
    //
    // Ryujinx upstream has the host dial back to its own P2pProxyServer over
    // TCP loopback so the host appears as a regular session in `_players`
    // and shares the same data-plane code path as joiners. On Switch that
    // costs us +1 TCP connection, +2 worker threads, +128 KB recv buffers
    // and +2 bsd:s IPC sessions for traffic that never leaves the console.
    // We skip the loopback (see is_self_proxy in ldn_icommunication.cpp's
    // ExternalProxy handler) and route the host's data plane in-process:
    //
    //   - inbound  (joiner -> host game)
    //       a session receives a ProxyData whose dest is the host's vIP or
    //       the broadcast address. The server invokes m_host_data_callback
    //       so ICommunicationService can forward the payload to
    //       ProxySocketManager::RouteIncomingData — the same sink relay-
    //       mode ProxyData feeds.
    //
    //   - outbound (host game -> joiners)
    //       BSD MITM calls into ICommunicationService::SendProxyDataToServer.
    //       In P2P-host mode that path calls BroadcastFromHost() below
    //       instead of m_server_client.send_proxy_data, sending the payload
    //       directly to every authenticated joiner session over their P2P
    //       TCP — which is the only sink the joiner game listens to,
    //       because the joiner's LdnProxy is registered against the P2P
    //       protocol instance, not the master one.

    /// Callback invoked when an inbound proxy packet is destined to the
    /// host (its virtual IP or broadcast). Same shape as the relay-mode
    /// ProxyData handler in ldn_icommunication.cpp.
    using HostProxyDataCallback = void (*)(uint32_t source_ip, uint16_t source_port,
                                           uint32_t dest_ip,   uint16_t dest_port,
                                           ryu_ldn::protocol::ProtocolType protocol,
                                           const uint8_t* data, size_t data_len,
                                           void* user_data);

    /// The host's own virtual IP — packets whose dest matches this value or
    /// the configured broadcast address are dispatched to m_host_data_callback.
    void SetHostVirtualIp(uint32_t virtual_ip);

    /// Register the in-process inbound sink. Pass nullptr to detach.
    void SetHostDataCallback(HostProxyDataCallback callback, void* user_data);

    /// Broadcast a ProxyData packet from the host's local game out to every
    /// authenticated P2P session. The header's source_ipv4 is filled in with
    /// the host's virtual IP if the caller left it at 0 (mirrors the
    /// "bound on 0.0.0.0" handling in RouteMessage).
    bool BroadcastFromHost(ryu_ldn::protocol::ProxyDataHeader& header,
                           const uint8_t* data, size_t data_len);


    // =========================================================================
    // Proxy Message Routing
    // =========================================================================

    /**
     * @brief Handle ProxyData message from a session
     */
    /// @gdb{tag="P2P:ROUTE", msg="Proxy data handled (server)"}
    void HandleProxyData(P2pProxySession* sender,
                         ryu_ldn::protocol::ProxyDataHeader& header,
                         const uint8_t* data, size_t data_len);

    /**
     * @brief Handle ProxyConnect message from a session
     */
    /// @gdb{tag="P2P:ROUTE", msg="Proxy connect handled (server)"}
    void HandleProxyConnect(P2pProxySession* sender,
                            ryu_ldn::protocol::ProxyConnectRequest& request);

    /**
     * @brief Handle ProxyConnectReply message from a session
     */
    /// @gdb{tag="P2P:ROUTE", msg="Proxy connect reply handled (server)"}
    void HandleProxyConnectReply(P2pProxySession* sender,
                                  ryu_ldn::protocol::ProxyConnectResponse& response);

    /**
     * @brief Handle ProxyDisconnect message from a session
     */
    /// @gdb{tag="P2P:ROUTE", msg="Proxy disconnect handled (server)"}
    void HandleProxyDisconnect(P2pProxySession* sender,
                               ryu_ldn::protocol::ProxyDisconnectMessage& message);

    /**
     * @brief Called when a session disconnects
     */
    /// @gdb{tag="P2P:SESSION", msg="Session disconnected event"}
    void OnSessionDisconnected(P2pProxySession* session);

private:
    // =========================================================================
    // Friend declarations for thread entry points
    // =========================================================================
    /// @gdb{tag="P2P:SERVER", msg="Accept thread started"}
    friend void AcceptThreadEntry(void* arg);
    /// @gdb{tag="P2P:NAT", msg="Lease thread started"}
    friend void LeaseThreadEntry(void* arg);

    // =========================================================================
    // Internal Methods
    // =========================================================================

    /**
     * @brief Accept loop thread function
     */
    /// @gdb{tag="P2P:SERVER", msg="Accept loop running"}
    void AcceptLoop();

    /**
     * @brief Route a message to appropriate session(s)
     * @param sender The session that sent the message
     * @param info Proxy info with source/dest IPs
     * @param send_func Function to call for each target session
     */
    template<typename SendFunc>
    /// @gdb{tag="P2P:ROUTE", msg="Message routed"}
    void RouteMessage(P2pProxySession* sender,
                      ryu_ldn::protocol::ProxyInfo& info,
                      SendFunc send_func);

    /**
     * @brief Notify master server of connection state change
     */
    /// @gdb{tag="P2P:SESSION", msg="Master disconnect notification"}
    void NotifyMasterDisconnect(uint32_t virtual_ip);

    /**
     * @brief Lease renewal thread function
     */
    /// @gdb{tag="P2P:NAT", msg="Lease renewal loop"}
    void LeaseRenewalLoop() const;

    /**
     * @brief Start lease renewal background thread
     */
    /// @gdb{tag="P2P:NAT", msg="Starting lease renewal"}
    void StartLeaseRenewal();

    // =========================================================================
    // Member Variables
    // =========================================================================

    mutable os::Mutex m_mutex{false};

    // Server socket
    int m_listen_fd;
    uint16_t m_private_port;
    uint16_t m_public_port;
    // Atomic so the accept thread sees the Stop()-side flip without going
    // through m_mutex (we don't want to take m_mutex on every poll cycle).
    std::atomic<bool> m_running;
    bool m_disposed;

    // Accept thread (stack lives in BSS — see g_p2p_accept_thread_stack in
    // p2p_proxy_server.cpp). Inlining the stack inside the class with
    // alignas(0x1000) made the whole class over-aligned to 4 KB, which
    // forced `new P2pProxyServer(...)` to call the unimplemented aligned
    // operator new and crashed the sysmodule (DABRT 0x101) the moment a
    // host pressed "create network".
    os::ThreadType m_accept_thread;

    // Lease renewal thread (stack: g_p2p_lease_thread_stack in BSS)
    os::ThreadType m_lease_thread;
    bool m_lease_thread_running;

    // Sessions
    std::unique_ptr<P2pProxySession> m_sessions[MAX_PLAYERS];
    int m_session_count;

    // Disconnected sessions waiting to be deleted by ReapZombieSessions().
    // Sized at 2 * MAX_PLAYERS so a burst of joiners cycling in/out can't
    // overflow before the next reap. We can't `delete this` inside the
    // session's own recv thread (would free its own stack while it's still
    // unwinding), so OnSessionDisconnected just queues the pointer here and
    // AcceptLoop / Stop() free it once m_thread_done is true and the
    // thread has joined.
    static constexpr int MAX_ZOMBIE_SESSIONS = MAX_PLAYERS * 2;
    std::unique_ptr<P2pProxySession> m_zombie_sessions[MAX_ZOMBIE_SESSIONS];
    int m_zombie_session_count;

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

    // In-process host self-routing — see SetHostDataCallback() above.
    uint32_t m_host_virtual_ip;
    HostProxyDataCallback m_host_data_callback;
    void* m_host_data_user_data;
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
    /// @gdb{tag="P2P:SESSION", msg="Session started"}
    void Start();

    /**
     * @brief Send data to the client
     * @return true if send succeeded
     */
    /// @gdb{tag="P2P:ROUTE", msg="Session send"}
    bool Send(const void* data, size_t size) const;

    /**
     * @brief Disconnect and stop
     * @param from_master true if disconnect was initiated by master server
     */
    /// @gdb{tag="P2P:SESSION", msg="Session disconnecting"}
    void Disconnect(bool from_master = false);

    /**
     * @brief Check if session is connected
     */
    bool IsConnected() const { return m_connected; }

private:
    // Friend for thread entry point
    /// @gdb{tag="P2P:SESSION", msg="Session thread started"}
    friend void SessionRecvThreadEntry(void* arg);

    /**
     * @brief Receive loop thread function
     */
    /// @gdb{tag="P2P:SESSION", msg="Session receive loop"}
    void ReceiveLoop();

    /**
     * @brief Process received data
     */
    /// @gdb{tag="P2P:ROUTE", msg="Session data processing"}
    void ProcessData(const uint8_t* data, size_t size);

    // Protocol handlers
    /// @gdb{tag="P2P:ROUTE", msg="External proxy configured"}
    void HandleExternalProxy(const ryu_ldn::protocol::ExternalProxyConfig& config);
    /// @gdb{tag="P2P:ROUTE", msg="Proxy data handled (session)"}
    void HandleProxyData(const ryu_ldn::protocol::ProxyDataHeader& header,
                         const uint8_t* data, size_t data_len);
    /// @gdb{tag="P2P:ROUTE", msg="Proxy connect handled (session)"}
    void HandleProxyConnect(const ryu_ldn::protocol::ProxyConnectRequest& request);
    /// @gdb{tag="P2P:ROUTE", msg="Proxy connect reply handled (session)"}
    void HandleProxyConnectReply(const ryu_ldn::protocol::ProxyConnectResponse& response);
    /// @gdb{tag="P2P:ROUTE", msg="Proxy disconnect handled (session)"}
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

    // Receive thread (stack lives in a BSS pool — see g_p2p_session_stacks
    // in p2p_proxy_server.cpp). Inlining `alignas(0x1000) uint8_t stack[N]`
    // here made the whole class over-aligned to 4 KB and crashed
    // `new P2pProxySession(...)` with DABRT 0x101 — same root cause we
    // fixed in P2pProxyServer earlier. Keep it as a slot index so that
    // ~P2pProxySession can return the slot to the pool.
    os::ThreadType m_recv_thread;
    int m_stack_slot;            ///< Index in the global session-stack pool, -1 if unallocated

    // Set by SessionRecvThreadEntry just before the thread function returns,
    // so the server's reaper can spot a zombie session and free it without
    // doing `delete this` from the dying thread (which would free its own
    // stack mid-return and is UB).
    std::atomic<bool> m_thread_done{false};

    // Receive buffer
    static constexpr size_t RECV_BUFFER_SIZE = 0x10000;
    uint8_t m_recv_buffer[RECV_BUFFER_SIZE];

    friend class P2pProxyServer;  // for the reaper path
};

} // namespace ams::mitm::p2p
