/**
 * @file p2p_proxy_server.cpp
 * @brief P2P Proxy Server implementation for Nintendo Switch
 *
 * This file implements the P2pProxyServer class which allows a Nintendo Switch
 * to host direct P2P connections from other players (either Ryujinx or Switch).
 *
 * ## Architecture Overview
 *
 * When a Switch creates a network (becomes host), the P2pProxyServer:
 *
 * ```
 *     ┌─────────────────────────────────────────────────────────────────┐
 *     │                    P2pProxyServer                                │
 *     │                                                                  │
 *     │  ┌──────────────┐    ┌─────────────────────────────────────┐   │
 *     │  │ Accept Loop  │───►│ Creates P2pProxySession for each   │   │
 *     │  │ (Thread)     │    │ incoming TCP connection             │   │
 *     │  └──────────────┘    └─────────────────────────────────────┘   │
 *     │                                                                  │
 *     │  ┌──────────────┐    ┌─────────────────────────────────────┐   │
 *     │  │ Lease Renew  │───►│ Refreshes UPnP port mapping every  │   │
 *     │  │ (Thread)     │    │ 50 seconds to maintain 60s lease    │   │
 *     │  └──────────────┘    └─────────────────────────────────────┘   │
 *     │                                                                  │
 *     │  ┌──────────────────────────────────────────────────────────┐  │
 *     │  │                    Session Array                          │  │
 *     │  │  [0] P2pProxySession (Player 1) ─── Recv Thread          │  │
 *     │  │  [1] P2pProxySession (Player 2) ─── Recv Thread          │  │
 *     │  │  [2] nullptr                                              │  │
 *     │  │  ...                                                      │  │
 *     │  │  [7] nullptr                                              │  │
 *     │  └──────────────────────────────────────────────────────────┘  │
 *     │                                                                  │
 *     │  ┌──────────────────────────────────────────────────────────┐  │
 *     │  │                    Waiting Tokens                         │  │
 *     │  │  Tokens from master server waiting for client auth        │  │
 *     │  │  [ExternalProxyToken] [ExternalProxyToken] ...           │  │
 *     │  └──────────────────────────────────────────────────────────┘  │
 *     └─────────────────────────────────────────────────────────────────┘
 * ```
 *
 * ## Connection Flow
 *
 * ### 1. Server Startup
 *
 * ```
 * Host Switch                                      Router
 *     │                                               │
 *     │ Start(port)                                   │
 *     │   ├─ ::socket(AF_INET, SOCK_STREAM)            │
 *     │   ├─ setsockopt(SO_REUSEADDR)                │
 *     │   ├─ setsockopt(TCP_NODELAY)                 │
 *     │   ├─ bind(port 39990-39999)                  │
 *     │   ├─ listen(backlog=8)                       │
 *     │   └─ spawn accept thread                      │
 *     │                                               │
 *     │ NatPunch()                                    │
 *     │   ├─ UPnP Discovery (SSDP) ─────────────────►│
 *     │   │◄─────────────── IGD Response ────────────│
 *     │   │                                           │
 *     │   ├─ UPNP_AddPortMapping(39990) ────────────►│
 *     │   │◄─────────────── Success ─────────────────│
 *     │   │                                           │
 *     │   └─ spawn lease renewal thread               │
 *     ▼                                               ▼
 * ```
 *
 * ### 2. Client Authentication
 *
 * ```
 * Master Server              Host Switch           Joiner (Ryujinx/Switch)
 *     │                          │                          │
 *     │ ExternalProxyToken      │                          │
 *     │  (VirtualIP + Token)    │                          │
 *     │─────────────────────────►│                          │
 *     │                          │ AddWaitingToken()        │
 *     │                          │                          │
 *     │                          │◄─────── TCP Connect ─────│
 *     │                          │         (port 39990)     │
 *     │                          │                          │
 *     │                          │◄─── ExternalProxyConfig ─│
 *     │                          │      (Token for auth)    │
 *     │                          │                          │
 *     │                          │ TryRegisterUser()        │
 *     │                          │   ├─ Match token         │
 *     │                          │   ├─ Assign virtual IP   │
 *     │                          │   └─ Add to sessions     │
 *     │                          │                          │
 *     │                          │──── ProxyConfig ────────►│
 *     │                          │     (VirtualIP + Mask)   │
 *     ▼                          ▼                          ▼
 * ```
 *
 * ### 3. Message Routing
 *
 * ```
 * Player A (Session 0)       P2pProxyServer        Player B (Session 1)
 *     │                          │                          │
 *     │─── ProxyData ───────────►│                          │
 *     │    (Dest: Player B)      │                          │
 *     │                          │ RouteMessage()           │
 *     │                          │   ├─ Fix source IP       │
 *     │                          │   ├─ Find target         │
 *     │                          │   └─ Forward packet      │
 *     │                          │──── ProxyData ──────────►│
 *     │                          │                          │
 *     │◄── ProxyData ────────────│                          │
 *     │    (Dest: Broadcast)     │◄─── ProxyData ───────────│
 *     │                          │     (Dest: 0xFFFFFFFF)   │
 *     ▼                          ▼                          ▼
 * ```
 *
 * ## Thread Model
 *
 * The server uses the following threads:
 *
 * | Thread          | Priority | Stack  | Purpose                           |
 * |-----------------|----------|--------|-----------------------------------|
 * | p2p_accept      | High-1   | 16KB   | Accept incoming TCP connections   |
 * | p2p_lease       | Lowest   | 8KB    | UPnP lease renewal every 50s     |
 * | p2p_session[n]  | High-2   | 16KB   | Receive data from each client    |
 *
 * ## Error Handling
 *
 * | Error Scenario          | Action                                   |
 * |-------------------------|------------------------------------------|
 * | Bind fails              | Try next port in range (39990-39999)    |
 * | UPnP discovery fails    | Log warning, continue without NAT punch |
 * | Auth timeout            | Disconnect client                        |
 * | Invalid packet magic    | Disconnect client                        |
 * | Session limit reached   | Reject new connections                   |
 *
 * ## Ryujinx Compatibility
 *
 * This implementation mirrors Ryujinx's P2pProxyServer for full interoperability:
 *
 * | Parameter        | Value   | Notes                              |
 * |------------------|---------|-------------------------------------|
 * | Port range       | 39990-9 | Both private and public             |
 * | Lease duration   | 60s     | UPnP port mapping lifetime          |
 * | Lease renewal    | 50s     | Renew before expiry                 |
 * | Auth timeout     | 1s      | Wait for token match                |
 * | Max players      | 8       | Maximum concurrent sessions         |
 * | Broadcast IP     | 0xc0a800ff | 192.168.0.255 (translated)       |
 * | Subnet mask      | /16     | 0xFFFF0000                          |
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "p2p_proxy_server.hpp"
#include "../debug/log.hpp"

// =============================================================================
// BSD Socket Headers
// =============================================================================
// Standard POSIX socket API for TCP networking

#include <sys/socket.h>   // socket(), bind(), listen(), accept(), send(), recv()
#include <netinet/in.h>   // sockaddr_in, INADDR_ANY, htons(), ntohs()
#include <netinet/tcp.h>  // TCP_NODELAY
#include <arpa/inet.h>    // inet_ntop() for logging
#include <unistd.h>       // close()
#include <fcntl.h>        // fcntl() for non-blocking (if needed)
#include <cerrno>         // errno
#include <cstring>        // memcmp(), memset()

namespace ams::mitm::p2p {

// =============================================================================
// Thread Entry Points
// =============================================================================
//
// These static functions serve as entry points for OS threads.
// They simply cast the void* argument to the appropriate type and call
// the corresponding member function.
//
// Why static functions instead of lambdas?
// - Atmosphere's os::CreateThread requires a C-style function pointer
// - Static member functions have the right calling convention
// - The 'arg' parameter passes the 'this' pointer

/**
 * @brief Entry point for the accept loop thread
 *
 * This thread continuously calls accept() on the listen socket,
 * creating a new P2pProxySession for each incoming connection.
 *
 * @param arg Pointer to P2pProxyServer instance
 */
void AcceptThreadEntry(void* arg) {
    auto* server = static_cast<P2pProxyServer*>(arg);
    server->AcceptLoop();
}

/**
 * @brief Entry point for the UPnP lease renewal thread
 *
 * This thread wakes up every PORT_LEASE_RENEW seconds (50s) to
 * refresh the UPnP port mapping, preventing it from expiring
 * (which happens after PORT_LEASE_LENGTH seconds = 60s).
 *
 * @param arg Pointer to P2pProxyServer instance
 */
void LeaseThreadEntry(void* arg) {
    auto* server = static_cast<P2pProxyServer*>(arg);
    server->LeaseRenewalLoop();
}

/**
 * @brief Entry point for a session receive thread
 *
 * Each P2pProxySession has its own receive thread that blocks on
 * recv() and processes incoming packets.
 *
 * @param arg Pointer to P2pProxySession instance
 */
void SessionRecvThreadEntry(void* arg) {
    auto* session = static_cast<P2pProxySession*>(arg);
    session->ReceiveLoop();
}

// =============================================================================
// P2pProxyServer Constructor / Destructor
// =============================================================================

/**
 * @brief Constructor - initializes server state
 *
 * @param master_callback Callback function to send packets to the master server.
 *                        Used to notify about client disconnections.
 *                        Can be nullptr if notifications aren't needed.
 *
 * The constructor initializes all member variables to safe defaults:
 * - Socket file descriptor to -1 (invalid)
 * - Ports to 0 (not yet bound)
 * - Session array to all nullptr
 * - Token queue to empty
 */
P2pProxyServer::P2pProxyServer(MasterSendCallback master_callback, void* user_data)
    : m_listen_fd(-1)
    , m_private_port(0)
    , m_public_port(0)
    , m_running(false)
    , m_disposed(false)
    , m_lease_thread_running(false)
    , m_session_count(0)
    , m_waiting_token_count(0)
    , m_broadcast_address(0)
    , m_master_callback(master_callback)
    , m_callback_user_data(user_data)
{
    // Initialize session array to nullptr
    // This is important - we use nullptr to detect empty slots
    for (int i = 0; i < MAX_PLAYERS; i++) {
        m_sessions[i] = nullptr;
    }
}

/**
 * @brief Destructor - cleanup all resources
 *
 * Ensures clean shutdown:
 * 1. Stop() - Closes listen socket, disconnects sessions, stops accept thread
 * 2. ReleaseNatPunch() - Stops lease thread, deletes UPnP port mapping
 * 3. Sets m_disposed flag to prevent further operations
 */
P2pProxyServer::~P2pProxyServer() {
    Stop();
    ReleaseNatPunch();
    m_disposed = true;
}

// =============================================================================
// Server Control - Start
// =============================================================================

/**
 * @brief Start the TCP server and begin accepting connections
 *
 * @param port Specific port to bind to, or 0 for auto-select from range
 * @return true if server started successfully, false on error
 *
 * ## Socket Creation Flow
 *
 * ```
 * 1. ::socket(AF_INET, SOCK_STREAM, 0)
 *    │
 *    ├─ AF_INET = IPv4
 *    ├─ SOCK_STREAM = TCP (reliable, ordered)
 *    └─ Protocol 0 = default for stream (TCP)
 *
 * 2. setsockopt(SO_REUSEADDR)
 *    │
 *    └─ Allow bind to recently-closed port
 *       (Important for quick restart after crash)
 *
 * 3. setsockopt(TCP_NODELAY)
 *    │
 *    └─ Disable Nagle's algorithm
 *       (Critical for low-latency game traffic)
 *
 * 4. bind(port)
 *    │
 *    └─ Try ports 39990-39999 until one succeeds
 *
 * 5. listen(backlog=8)
 *    │
 *    └─ Allow up to 8 pending connections
 *
 * 6. Create accept thread
 *    │
 *    └─ Thread loops calling accept()
 * ```
 */
bool P2pProxyServer::Start(uint16_t port) {
    std::scoped_lock lock(m_mutex);

    // Already running check
    if (m_running) {
        LOG_WARN("P2pProxyServer already running");
        return true;
    }

    // =========================================================================
    // Step 1: Create TCP Socket
    // =========================================================================
    //
    // We create a standard TCP socket for IPv4 communication.
    // This is the same socket type used by Ryujinx's NetCoreServer TcpServer.

    m_listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_listen_fd < 0) {
        LOG_ERROR("Failed to create P2P server socket: errno=%d", errno);
        return false;
    }

    // =========================================================================
    // Step 2: Configure Socket Options
    // =========================================================================

    // SO_REUSEADDR: Allow binding to a port in TIME_WAIT state
    // This is crucial for quick server restarts without waiting ~2 minutes
    // for the kernel to release the port
    int reuse = 1;
    if (setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        LOG_WARN("setsockopt SO_REUSEADDR failed: errno=%d", errno);
        // Not fatal, continue
    }

    // TCP_NODELAY: Disable Nagle's algorithm
    // Nagle's algorithm batches small packets to reduce overhead, but adds latency.
    // For real-time gaming, we want packets sent immediately.
    // Ryujinx sets this via OptionNoDelay = true in the TcpServer constructor.
    int nodelay = 1;
    if (setsockopt(m_listen_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
        LOG_WARN("setsockopt TCP_NODELAY failed: errno=%d", errno);
        // Not fatal, continue
    }

    // =========================================================================
    // Step 3: Bind to Port
    // =========================================================================
    //
    // Like Ryujinx, we try ports 39990-39999 (PRIVATE_PORT_BASE + PRIVATE_PORT_RANGE)
    // This range is chosen to not conflict with common services while being
    // predictable for firewall rules.

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all interfaces

    // Determine port range to try
    uint16_t start_port = (port != 0) ? port : PRIVATE_PORT_BASE;
    uint16_t end_port = (port != 0) ? port : (PRIVATE_PORT_BASE + PRIVATE_PORT_RANGE - 1);

    bool bound = false;
    for (uint16_t try_port = start_port; try_port <= end_port; try_port++) {
        addr.sin_port = htons(try_port);  // Convert to network byte order (big-endian)

        int result = bind(m_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (result == 0) {
            m_private_port = try_port;
            bound = true;
            LOG_INFO("P2P server bound to port %u", try_port);
            break;
        }

        // Common errors:
        // - EADDRINUSE (98): Port already in use
        // - EACCES (13): Permission denied (port < 1024)
        LOG_VERBOSE("Port %u busy (errno=%d), trying next...", try_port, errno);
    }

    if (!bound) {
        LOG_ERROR("Failed to bind P2P server to any port in range %u-%u",
                  start_port, end_port);
        close(m_listen_fd);
        m_listen_fd = -1;
        return false;
    }

    // =========================================================================
    // Step 4: Start Listening
    // =========================================================================
    //
    // listen() marks the socket as a passive socket that will accept()
    // incoming connections. The backlog parameter (MAX_PLAYERS) specifies
    // the maximum number of pending connections in the queue.

    if (listen(m_listen_fd, MAX_PLAYERS) < 0) {
        LOG_ERROR("Failed to listen on P2P socket: errno=%d", errno);
        close(m_listen_fd);
        m_listen_fd = -1;
        return false;
    }

    // =========================================================================
    // Step 5: Start Accept Thread
    // =========================================================================
    //
    // We spawn a dedicated thread to handle incoming connections.
    // This thread loops calling accept() and creates P2pProxySession
    // objects for each new connection.
    //
    // Thread priority: HighestThreadPriority - 1
    // This is high priority because we don't want connection delays.

    m_running = true;

    R_ABORT_UNLESS(os::CreateThread(
        &m_accept_thread,
        AcceptThreadEntry,        // Entry function
        this,                      // Argument (this pointer)
        m_accept_thread_stack,     // Stack memory
        sizeof(m_accept_thread_stack),  // Stack size (16KB)
        os::HighestThreadPriority - 1   // High priority
    ));

    os::SetThreadNamePointer(&m_accept_thread, "p2p_accept");
    os::StartThread(&m_accept_thread);

    LOG_INFO("P2P server started on port %u", m_private_port);
    return true;
}

// =============================================================================
// Server Control - Stop
// =============================================================================

/**
 * @brief Stop the server and disconnect all clients
 *
 * Performs clean shutdown:
 * 1. Sets m_running = false to signal threads to exit
 * 2. Closes listen socket (wakes accept thread from blocking accept())
 * 3. Disconnects all sessions
 * 4. Clears waiting tokens
 * 5. Waits for accept thread to finish
 *
 * Thread-safe: Can be called from any thread.
 */
void P2pProxyServer::Stop() {
    {
        std::scoped_lock lock(m_mutex);

        if (!m_running) {
            return;  // Already stopped
        }

        m_running = false;

        // =====================================================================
        // Close Listen Socket
        // =====================================================================
        // This is important! The accept thread is blocked in accept().
        // Closing the socket will cause accept() to return with an error,
        // allowing the thread to check m_running and exit cleanly.

        if (m_listen_fd >= 0) {
            close(m_listen_fd);
            m_listen_fd = -1;
        }

        // =====================================================================
        // Disconnect All Sessions
        // =====================================================================
        // We disconnect with from_master=true to prevent sessions from
        // calling OnSessionDisconnected (since we're shutting down anyway).

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (m_sessions[i] != nullptr) {
                m_sessions[i]->Disconnect(true);  // from_master = true
                delete m_sessions[i];
                m_sessions[i] = nullptr;
            }
        }
        m_session_count = 0;

        // =====================================================================
        // Clear Waiting Tokens
        // =====================================================================
        // Any pending tokens are now invalid since the server is stopping.

        m_waiting_token_count = 0;
    }

    // =========================================================================
    // Wait for Accept Thread
    // =========================================================================
    // Must be done outside the lock to avoid deadlock if the thread
    // is waiting for the lock.

    os::WaitThread(&m_accept_thread);
    os::DestroyThread(&m_accept_thread);

    LOG_INFO("P2P server stopped");
}

/**
 * @brief Check if the server is currently running
 * @return true if running, false if stopped
 */
bool P2pProxyServer::IsRunning() const {
    std::scoped_lock lock(m_mutex);
    return m_running;
}

// =============================================================================
// UPnP NAT Punch
// =============================================================================

/**
 * @brief Open a public port via UPnP for NAT traversal
 *
 * @return The public port number if successful, 0 if UPnP failed
 *
 * ## UPnP Port Mapping Flow
 *
 * ```
 * Switch                     Router (IGD)              Internet
 *   │                           │                         │
 *   │ SSDP Discovery ──────────►│                         │
 *   │◄──────── IGD Response ────│                         │
 *   │                           │                         │
 *   │ AddPortMapping ──────────►│                         │
 *   │  (39990, TCP, 60s)        │                         │
 *   │◄──────── Success ─────────│                         │
 *   │                           │                         │
 *   │         [Now port 39990 on router forwards to us]   │
 *   │                           │                         │
 *   │                           │◄──── TCP Connect ───────│
 *   │◄──────────────────────────│   (to router:39990)     │
 *   │                           │                         │
 * ```
 *
 * ## Why Try Multiple Ports?
 *
 * A port might be:
 * 1. Already mapped by another device on the network
 * 2. Blocked by router firewall rules
 * 3. Reserved by the router itself
 *
 * By trying 39990-39999, we increase the chance of finding an available port.
 */
uint16_t P2pProxyServer::NatPunch() {
    // =========================================================================
    // Step 1: Discover UPnP Gateway
    // =========================================================================
    // UpnpPortMapper::Discover() sends an SSDP multicast query to find
    // UPnP-capable routers on the network. Timeout is 2500ms (like Ryujinx).

    auto& mapper = UpnpPortMapper::GetInstance();

    if (!mapper.Discover()) {
        // UPnP not available - this is common:
        // - Router doesn't support UPnP
        // - UPnP is disabled in router settings
        // - Network firewall blocks SSDP
        LOG_WARN("UPnP discovery failed - P2P may not work through NAT");
        return 0;
    }

    // =========================================================================
    // Step 2: Try Port Mappings
    // =========================================================================
    // Like Ryujinx, try ports 39990-39999 until one succeeds.
    // We use PORT_LEASE_LENGTH (60 seconds) as the lease duration.

    for (int i = 0; i < PUBLIC_PORT_RANGE; i++) {
        uint16_t try_port = PUBLIC_PORT_BASE + static_cast<uint16_t>(i);

        // Attempt to create port mapping:
        // - Internal (private) port: m_private_port (what we're listening on)
        // - External (public) port: try_port (what remote clients connect to)
        // - Description: "ryu_ldn_nx P2P" (visible in router admin page)
        // - Lease: 60 seconds (must be renewed)
        if (mapper.AddPortMapping(m_private_port, try_port,
                                  "ryu_ldn_nx P2P", PORT_LEASE_LENGTH)) {
            m_public_port = try_port;

            // Log the mapping for debugging
            // Getting external IP is optional but helpful for troubleshooting
            char external_ip[16] = {0};
            if (mapper.GetExternalIPAddress(external_ip, sizeof(external_ip))) {
                LOG_INFO("UPnP port mapping: %s:%u -> local:%u",
                         external_ip, m_public_port, m_private_port);
            } else {
                LOG_INFO("UPnP port mapping: public:%u -> local:%u",
                         m_public_port, m_private_port);
            }

            // ================================================================
            // Step 3: Start Lease Renewal Thread
            // ================================================================
            // The mapping expires after PORT_LEASE_LENGTH (60s).
            // We need to refresh it every PORT_LEASE_RENEW (50s) to
            // maintain the mapping.
            StartLeaseRenewal();

            return m_public_port;
        }

        // Common UPnP errors:
        // - 718: ConflictInMappingEntry (port already mapped to another host)
        // - 725: OnlyPermanentLeasesSupported (router doesn't support timed leases)
        LOG_VERBOSE("UPnP port %u failed, trying next...", try_port);
    }

    LOG_WARN("UPnP failed to map any port in range %u-%u",
             PUBLIC_PORT_BASE, PUBLIC_PORT_BASE + PUBLIC_PORT_RANGE - 1);
    return 0;
}

/**
 * @brief Release the UPnP port mapping and stop lease renewal
 *
 * Called during server shutdown to:
 * 1. Stop the lease renewal thread
 * 2. Delete the port mapping from the router
 *
 * Important: We clean up our mappings to be a good network citizen.
 * Abandoned mappings waste router resources and can cause issues
 * for other applications.
 */
void P2pProxyServer::ReleaseNatPunch() {
    // =========================================================================
    // Stop Lease Renewal Thread
    // =========================================================================
    // Must stop before deleting mapping, otherwise renewal might
    // try to refresh a deleted mapping.

    if (m_lease_thread_running) {
        m_lease_thread_running = false;
        // Thread will exit on next wake-up check
        os::WaitThread(&m_lease_thread);
        os::DestroyThread(&m_lease_thread);
    }

    // =========================================================================
    // Delete Port Mapping
    // =========================================================================

    if (m_public_port != 0) {
        auto& mapper = UpnpPortMapper::GetInstance();
        if (mapper.DeletePortMapping(m_public_port)) {
            LOG_INFO("UPnP port mapping released: %u", m_public_port);
        } else {
            LOG_WARN("Failed to release UPnP port mapping: %u", m_public_port);
        }
        m_public_port = 0;
    }
}

/**
 * @brief Start the UPnP lease renewal background thread
 *
 * Internal helper called after successful NatPunch().
 * Creates a low-priority thread that periodically refreshes
 * the UPnP port mapping.
 */
void P2pProxyServer::StartLeaseRenewal() {
    if (m_lease_thread_running) {
        return;  // Already running
    }

    m_lease_thread_running = true;

    R_ABORT_UNLESS(os::CreateThread(
        &m_lease_thread,
        LeaseThreadEntry,
        this,
        m_lease_thread_stack,
        sizeof(m_lease_thread_stack),
        os::LowestThreadPriority  // Low priority - not time-critical
    ));

    os::SetThreadNamePointer(&m_lease_thread, "p2p_lease");
    os::StartThread(&m_lease_thread);
}

/**
 * @brief Lease renewal thread main loop
 *
 * Runs continuously until m_lease_thread_running is set to false.
 * Wakes up every PORT_LEASE_RENEW seconds (50s) to refresh the
 * UPnP port mapping.
 *
 * ## Why 50 seconds?
 *
 * The lease duration is 60 seconds. By renewing at 50 seconds, we have
 * a 10-second safety margin. If renewal fails, we have time to retry
 * before the mapping expires.
 *
 * This matches Ryujinx's timing exactly for compatibility.
 */
void P2pProxyServer::LeaseRenewalLoop() {
    // Sleep duration: PORT_LEASE_RENEW seconds = 50 seconds
    const auto renew_ns = TimeSpan::FromSeconds(PORT_LEASE_RENEW).GetNanoSeconds();

    while (m_lease_thread_running && !m_disposed) {
        // Sleep for renewal interval
        svc::SleepThread(renew_ns);

        // Check if we should exit (server might have stopped during sleep)
        if (!m_lease_thread_running || m_disposed) {
            break;
        }

        // Attempt to renew the lease
        auto& mapper = UpnpPortMapper::GetInstance();
        if (mapper.RefreshPortMapping(m_private_port, m_public_port, "ryu_ldn_nx P2P")) {
            LOG_VERBOSE("UPnP lease renewed for port %u", m_public_port);
        } else {
            // Renewal failed - mapping might expire!
            // We log a warning but don't abort - the connection might
            // still work if we're on the same local network.
            LOG_WARN("UPnP lease renewal failed for port %u", m_public_port);
        }
    }
}

// =============================================================================
// Token Management
// =============================================================================

/**
 * @brief Add a waiting token for an expected joiner
 *
 * @param token Token received from master server
 *
 * ## Token Flow
 *
 * When a player wants to join via P2P, the master server:
 * 1. Assigns them a virtual IP (e.g., 10.114.0.2)
 * 2. Generates an auth token (16 bytes of random data)
 * 3. Sends ExternalProxyToken to the host with:
 *    - VirtualIP: The IP to assign to the joiner
 *    - Token: The auth token
 *    - PhysicalIP: The joiner's real IP (for validation)
 * 4. Sends ExternalProxyConfig to the joiner with connection info
 *
 * When the joiner connects, they send the same token in ExternalProxyConfig.
 * We match tokens to validate the connection and assign the virtual IP.
 *
 * ## Queue Full Handling
 *
 * If the token queue is full (MAX_WAITING_TOKENS = 16), we drop the oldest
 * token. This handles edge cases like:
 * - Master server sends tokens for disconnected clients
 * - Network issues cause token accumulation
 */
void P2pProxyServer::AddWaitingToken(const ryu_ldn::protocol::ExternalProxyToken& token) {
    std::scoped_lock lock(m_mutex);

    // Handle queue full
    if (m_waiting_token_count >= MAX_WAITING_TOKENS) {
        LOG_WARN("Waiting token queue full, dropping oldest");
        // Shift tokens left, discarding the oldest (index 0)
        for (int i = 0; i < MAX_WAITING_TOKENS - 1; i++) {
            m_waiting_tokens[i] = m_waiting_tokens[i + 1];
        }
        m_waiting_token_count--;
    }

    // Add new token to end of queue
    m_waiting_tokens[m_waiting_token_count++] = token;

    LOG_VERBOSE("Added waiting token for virtual IP 0x%08X", token.virtual_ip);

    // Signal any threads blocked in TryRegisterUser()
    // They're waiting for tokens to arrive
    m_token_cv.Broadcast();
}

/**
 * @brief Try to authenticate a connecting client
 *
 * @param session The P2pProxySession attempting to authenticate
 * @param config The ExternalProxyConfig sent by the client
 * @param remote_ip The client's physical IP address (network byte order)
 * @return true if authentication succeeded, false if no matching token found
 *
 * ## Authentication Process
 *
 * 1. Wait up to AUTH_WAIT_SECONDS (1 second) for a matching token
 * 2. For each waiting token, check:
 *    - Physical IP match (or all-zeros for private IPs)
 *    - Auth token match (16 bytes)
 * 3. If match found:
 *    - Remove token from queue
 *    - Assign virtual IP to session
 *    - Send ProxyConfig response
 *    - Add session to player list
 *
 * ## Private IP Handling
 *
 * The master server sends all-zeros for PhysicalIP if the client
 * has a private IP address (192.168.x.x, 10.x.x.x, etc.).
 * This allows clients behind NAT to connect without IP validation.
 *
 * ## Thread Safety
 *
 * Uses condition variable (m_token_cv) to efficiently wait for new tokens.
 * The thread sleeps until either:
 * - A new token arrives (signaled by AddWaitingToken)
 * - Timeout expires (AUTH_WAIT_SECONDS)
 */
bool P2pProxyServer::TryRegisterUser(P2pProxySession* session,
                                      const ryu_ldn::protocol::ExternalProxyConfig& config,
                                      uint32_t remote_ip) {
    std::scoped_lock lock(m_mutex);

    // =========================================================================
    // Retry Loop with Timeout
    // =========================================================================
    // We wait up to AUTH_WAIT_SECONDS (1 second) for a matching token.
    // The token might not have arrived yet if the network is slow.
    // We check multiple times with a short wait between each check.

    constexpr int MAX_RETRIES = 10;  // 10 * 100ms = 1 second total
    const auto wait_time = TimeSpan::FromMilliSeconds(100);

    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        // =====================================================================
        // Search Waiting Tokens
        // =====================================================================

        for (int i = 0; i < m_waiting_token_count; i++) {
            const auto& token = m_waiting_tokens[i];

            // =================================================================
            // Check Physical IP
            // =================================================================
            // Two cases:
            // 1. PhysicalIP is all zeros: Allow any client (private IP)
            // 2. PhysicalIP is set: Must match client's IP

            bool is_private = true;
            for (int j = 0; j < 16; j++) {
                if (token.physical_ip[j] != 0) {
                    is_private = false;
                    break;
                }
            }

            bool ip_match = is_private;
            if (!ip_match && token.address_family == 2) {  // AF_INET = 2 (IPv4)
                // Extract IPv4 from the 16-byte array
                // PhysicalIP is stored in network byte order (big-endian)
                uint32_t token_ip = (static_cast<uint32_t>(token.physical_ip[0]) << 24) |
                                    (static_cast<uint32_t>(token.physical_ip[1]) << 16) |
                                    (static_cast<uint32_t>(token.physical_ip[2]) << 8)  |
                                    static_cast<uint32_t>(token.physical_ip[3]);
                ip_match = (token_ip == remote_ip);
            }

            // =================================================================
            // Check Auth Token
            // =================================================================
            // 16-byte comparison - must be exact match

            bool token_match = (std::memcmp(token.token, config.token, 16) == 0);

            // =================================================================
            // Match Found!
            // =================================================================

            if (ip_match && token_match) {
                LOG_INFO("P2P auth success: virtual IP 0x%08X", token.virtual_ip);

                // Remove token from queue (shift remaining tokens left)
                for (int j = i; j < m_waiting_token_count - 1; j++) {
                    m_waiting_tokens[j] = m_waiting_tokens[j + 1];
                }
                m_waiting_token_count--;

                // Configure session with virtual IP
                session->SetVirtualIp(token.virtual_ip);
                session->SetAuthenticated(true);

                // Create ProxyConfig to send back to client
                ryu_ldn::protocol::ProxyConfig proxy_config{};
                proxy_config.proxy_ip = token.virtual_ip;
                proxy_config.proxy_subnet_mask = 0xFFFF0000;  // /16 subnet (like Ryujinx)

                // Configure broadcast address if this is the first player
                if (m_session_count == 0) {
                    Configure(proxy_config);
                }

                // Add session to player list
                for (int j = 0; j < MAX_PLAYERS; j++) {
                    if (m_sessions[j] == nullptr) {
                        m_sessions[j] = session;
                        m_session_count++;
                        break;
                    }
                }

                // Send ProxyConfig response to client
                // This tells the client their virtual IP and subnet mask
                uint8_t packet[256];
                size_t len = 0;
                ryu_ldn::protocol::encode(packet, sizeof(packet),
                                          ryu_ldn::protocol::PacketId::ProxyConfig,
                                          proxy_config, len);
                session->Send(packet, len);

                return true;
            }
        }

        // =====================================================================
        // Token Not Found - Wait for More
        // =====================================================================
        // The token might not have arrived yet. Wait on the condition variable
        // until either a new token arrives or the timeout expires.

        m_token_cv.TimedWait(m_mutex, wait_time);
    }

    LOG_WARN("P2P auth failed: no matching token found (waited %d sec)", AUTH_WAIT_SECONDS);
    return false;
}

// =============================================================================
// Configuration
// =============================================================================

/**
 * @brief Configure broadcast address from ProxyConfig
 *
 * @param config ProxyConfig containing IP and subnet mask
 *
 * Calculates the broadcast address for the virtual network.
 * Formula: broadcast = IP | ~mask
 *
 * Example for 10.114.0.1 with /16 mask:
 * - proxy_ip = 0x0A720001
 * - proxy_subnet_mask = 0xFFFF0000
 * - ~mask = 0x0000FFFF
 * - broadcast = 0x0A720001 | 0x0000FFFF = 0x0A72FFFF (10.114.255.255)
 */
void P2pProxyServer::Configure(const ryu_ldn::protocol::ProxyConfig& config) {
    m_broadcast_address = config.proxy_ip | (~config.proxy_subnet_mask);
    LOG_VERBOSE("P2P broadcast address: 0x%08X", m_broadcast_address);
}

// =============================================================================
// Accept Loop
// =============================================================================

/**
 * @brief Main loop for accepting incoming TCP connections
 *
 * This function runs in a dedicated thread and loops continuously:
 * 1. Block on accept() waiting for a connection
 * 2. Configure the new socket (TCP_NODELAY)
 * 3. Create a P2pProxySession for the connection
 * 4. Start the session's receive thread
 *
 * The loop exits when:
 * - m_running is set to false
 * - The listen socket is closed (causes accept to fail)
 *
 * ## Accept Behavior
 *
 * accept() is a blocking call. When Stop() closes the listen socket,
 * accept() returns with an error (EBADF or similar), which we detect
 * and break out of the loop.
 */
void P2pProxyServer::AcceptLoop() {
    while (m_running) {
        // Accept incoming connection
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(m_listen_fd,
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &client_len);

        if (client_fd < 0) {
            // Check if we're shutting down
            if (m_running) {
                // Unexpected error
                LOG_ERROR("P2P accept failed: errno=%d", errno);
            }
            // Either shutting down or error - continue to check m_running
            continue;
        }

        // =====================================================================
        // Extract Client Information
        // =====================================================================

        // Convert IP to host byte order for internal use
        uint32_t remote_ip = ntohl(client_addr.sin_addr.s_addr);

        // Format IP for logging
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        LOG_INFO("P2P connection from %s:%u", ip_str, ntohs(client_addr.sin_port));

        // =====================================================================
        // Configure Client Socket
        // =====================================================================

        // TCP_NODELAY on client socket too (for low latency responses)
        int nodelay = 1;
        if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
            LOG_WARN("Failed to set TCP_NODELAY on client socket");
        }

        // =====================================================================
        // Check Session Limit
        // =====================================================================

        {
            std::scoped_lock lock(m_mutex);

            if (m_session_count >= MAX_PLAYERS) {
                LOG_WARN("P2P session limit reached (%d), rejecting connection from %s",
                         MAX_PLAYERS, ip_str);
                close(client_fd);
                continue;
            }
        }

        // =====================================================================
        // Create Session
        // =====================================================================
        // The session will be added to m_sessions[] after successful auth
        // in TryRegisterUser()

        auto* session = new P2pProxySession(this, client_fd, remote_ip);
        session->Start();  // Start receive thread
    }
}

// =============================================================================
// Message Routing
// =============================================================================

/**
 * @brief Route a proxy message to appropriate destination(s)
 *
 * @tparam SendFunc Callable type for sending to a session
 * @param sender The session that sent the message
 * @param info ProxyInfo containing source and destination IPs
 * @param send_func Function to call for each target session
 *
 * ## Routing Logic
 *
 * 1. Fix source IP if zero (unbound socket sends as 0.0.0.0)
 * 2. Reject spoofing (source IP must match sender's virtual IP)
 * 3. Translate legacy broadcast (0xc0a800ff -> actual broadcast)
 * 4. Route to destination:
 *    - Broadcast: Send to all authenticated sessions
 *    - Unicast: Send to specific session by virtual IP
 *
 * ## Security Note
 *
 * We validate that the source IP matches the sender's virtual IP.
 * This prevents a malicious client from impersonating another player.
 */
template<typename SendFunc>
void P2pProxyServer::RouteMessage(P2pProxySession* sender,
                                   ryu_ldn::protocol::ProxyInfo& info,
                                   SendFunc send_func) {
    std::scoped_lock lock(m_mutex);

    // =========================================================================
    // Fix Source IP if Zero
    // =========================================================================
    // If the sender bound their socket to 0.0.0.0, the source IP will be 0.
    // We fix this to their virtual IP so receivers know who sent it.

    if (info.source_ipv4 == 0) {
        info.source_ipv4 = sender->GetVirtualIpAddress();
    } else if (info.source_ipv4 != sender->GetVirtualIpAddress()) {
        // Security: Reject spoofing attempts
        LOG_WARN("P2P spoofing attempt: session 0x%08X tried to send as 0x%08X",
                 sender->GetVirtualIpAddress(), info.source_ipv4);
        return;
    }

    // =========================================================================
    // Translate Legacy Broadcast Address
    // =========================================================================
    // Some games use 192.168.0.255 (0xc0a800ff) as broadcast.
    // We translate this to our actual broadcast address.

    uint32_t dest_ip = info.dest_ipv4;
    if (dest_ip == 0xc0a800ff) {
        dest_ip = m_broadcast_address;
    }

    bool is_broadcast = (dest_ip == m_broadcast_address);

    // =========================================================================
    // Route to Destination(s)
    // =========================================================================

    if (is_broadcast) {
        // Send to all authenticated players
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (m_sessions[i] != nullptr && m_sessions[i]->IsAuthenticated()) {
                send_func(m_sessions[i]);
            }
        }
    } else {
        // Send to specific player by virtual IP
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (m_sessions[i] != nullptr &&
                m_sessions[i]->IsAuthenticated() &&
                m_sessions[i]->GetVirtualIpAddress() == dest_ip) {
                send_func(m_sessions[i]);
                break;  // Found the target
            }
        }
    }
}

/**
 * @brief Handle ProxyData message from a session
 *
 * ProxyData is the main data transfer message used for UDP game traffic.
 * Routes the data to the destination session(s) based on ProxyInfo.
 */
void P2pProxyServer::HandleProxyData(P2pProxySession* sender,
                                      ryu_ldn::protocol::ProxyDataHeader& header,
                                      const uint8_t* data, size_t data_len) {
    RouteMessage(sender, header.info, [&](P2pProxySession* target) {
        // Encode and send to target
        uint8_t packet[0x10000];  // 64KB max packet
        size_t len = 0;
        ryu_ldn::protocol::encode_with_data(packet, sizeof(packet),
                                            ryu_ldn::protocol::PacketId::ProxyData,
                                            header, data, data_len, len);
        target->Send(packet, len);
    });
}

/**
 * @brief Handle ProxyConnect message from a session
 *
 * ProxyConnect initiates a virtual TCP connection between two players.
 * The target responds with ProxyConnectReply (accept/reject).
 */
void P2pProxyServer::HandleProxyConnect(P2pProxySession* sender,
                                         ryu_ldn::protocol::ProxyConnectRequest& request) {
    RouteMessage(sender, request.info, [&](P2pProxySession* target) {
        uint8_t packet[256];
        size_t len = 0;
        ryu_ldn::protocol::encode(packet, sizeof(packet),
                                  ryu_ldn::protocol::PacketId::ProxyConnect,
                                  request, len);
        target->Send(packet, len);
    });
}

/**
 * @brief Handle ProxyConnectReply message from a session
 *
 * Response to ProxyConnect - indicates whether the connection was accepted.
 */
void P2pProxyServer::HandleProxyConnectReply(P2pProxySession* sender,
                                              ryu_ldn::protocol::ProxyConnectResponse& response) {
    RouteMessage(sender, response.info, [&](P2pProxySession* target) {
        uint8_t packet[256];
        size_t len = 0;
        ryu_ldn::protocol::encode(packet, sizeof(packet),
                                  ryu_ldn::protocol::PacketId::ProxyConnectReply,
                                  response, len);
        target->Send(packet, len);
    });
}

/**
 * @brief Handle ProxyDisconnect message from a session
 *
 * Notifies the target that a virtual connection has been closed.
 */
void P2pProxyServer::HandleProxyDisconnect(P2pProxySession* sender,
                                            ryu_ldn::protocol::ProxyDisconnectMessage& message) {
    RouteMessage(sender, message.info, [&](P2pProxySession* target) {
        uint8_t packet[256];
        size_t len = 0;
        ryu_ldn::protocol::encode(packet, sizeof(packet),
                                  ryu_ldn::protocol::PacketId::ProxyDisconnect,
                                  message, len);
        target->Send(packet, len);
    });
}

// =============================================================================
// Session Disconnect Handler
// =============================================================================

/**
 * @brief Called when a session disconnects
 *
 * @param session The disconnected session
 *
 * Handles cleanup when a client disconnects:
 * 1. Remove from session array
 * 2. Notify master server (if was authenticated)
 *
 * The master server notification allows it to:
 * - Clean up its player list
 * - Notify other players of the disconnection
 */
void P2pProxyServer::OnSessionDisconnected(P2pProxySession* session) {
    std::scoped_lock lock(m_mutex);

    // Find and remove from session array
    bool found = false;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (m_sessions[i] == session) {
            m_sessions[i] = nullptr;
            m_session_count--;
            found = true;
            LOG_INFO("P2P session disconnected: virtual IP 0x%08X",
                     session->GetVirtualIpAddress());
            break;
        }
    }

    // Notify master server if session was authenticated
    if (found && session->IsAuthenticated()) {
        NotifyMasterDisconnect(session->GetVirtualIpAddress());
    }
}

/**
 * @brief Notify master server of a client disconnection
 *
 * @param virtual_ip The virtual IP of the disconnected client
 *
 * Sends ExternalProxyConnectionState with Connected=false to the
 * master server. This allows the server to update its records and
 * notify other players.
 */
void P2pProxyServer::NotifyMasterDisconnect(uint32_t virtual_ip) {
    if (m_master_callback == nullptr) {
        return;  // No callback registered
    }

    // Create ExternalProxyConnectionState packet
    ryu_ldn::protocol::ExternalProxyConnectionState state{};
    state.ip_address = virtual_ip;
    state.connected = 0;  // false

    // Encode and send via callback
    uint8_t packet[256];
    size_t len = 0;
    ryu_ldn::protocol::encode(packet, sizeof(packet),
                              ryu_ldn::protocol::PacketId::ExternalProxyState,
                              state, len);

    m_master_callback(packet, len, m_callback_user_data);
}

// =============================================================================
// P2pProxySession Implementation
// =============================================================================
//
// P2pProxySession represents a single TCP connection from a P2P client.
// Each session runs its own receive thread to handle incoming data.

/**
 * @brief Constructor - initialize session state
 *
 * @param server Parent P2pProxyServer
 * @param socket_fd Connected TCP socket file descriptor
 * @param remote_ip Client's IP address (host byte order)
 */
P2pProxySession::P2pProxySession(P2pProxyServer* server, int socket_fd, uint32_t remote_ip)
    : m_server(server)
    , m_socket_fd(socket_fd)
    , m_remote_ip(remote_ip)
    , m_virtual_ip(0)
    , m_connected(true)
    , m_authenticated(false)
    , m_master_closed(false)
{
}

/**
 * @brief Destructor - ensure disconnection
 */
P2pProxySession::~P2pProxySession() {
    Disconnect(true);
}

/**
 * @brief Start the receive thread
 *
 * Creates and starts a thread that loops calling recv() on the socket.
 * The thread processes received data and dispatches to appropriate handlers.
 */
void P2pProxySession::Start() {
    R_ABORT_UNLESS(os::CreateThread(
        &m_recv_thread,
        SessionRecvThreadEntry,
        this,
        m_recv_thread_stack,
        sizeof(m_recv_thread_stack),
        os::HighestThreadPriority - 2  // High priority but below accept thread
    ));

    os::SetThreadNamePointer(&m_recv_thread, "p2p_session");
    os::StartThread(&m_recv_thread);
}

/**
 * @brief Send data to the client
 *
 * @param data Pointer to data buffer
 * @param size Size of data in bytes
 * @return true if send succeeded, false on error
 *
 * Uses blocking send(). TCP guarantees delivery order and reliability.
 */
bool P2pProxySession::Send(const void* data, size_t size) {
    if (!m_connected || m_socket_fd < 0) {
        return false;
    }

    ssize_t sent = send(m_socket_fd, data, size, 0);
    return sent == static_cast<ssize_t>(size);
}

/**
 * @brief Disconnect the session
 *
 * @param from_master true if disconnect was initiated by master server/cleanup
 *
 * Closes the socket and optionally notifies the parent server.
 * If from_master is true, we skip the notification (already handled).
 */
void P2pProxySession::Disconnect(bool from_master) {
    if (!m_connected) {
        return;  // Already disconnected
    }

    m_connected = false;
    m_master_closed = from_master;

    // Close socket
    if (m_socket_fd >= 0) {
        shutdown(m_socket_fd, SHUT_RDWR);  // Stop reads and writes
        close(m_socket_fd);
        m_socket_fd = -1;
    }

    // Notify server (unless this disconnect came from the server itself)
    if (!from_master) {
        m_server->OnSessionDisconnected(this);
    }
}

/**
 * @brief Receive thread main loop
 *
 * Continuously receives data from the socket and processes it.
 * Exits when:
 * - Connection is closed (recv returns 0)
 * - Error occurs (recv returns -1)
 * - m_connected is set to false
 */
void P2pProxySession::ReceiveLoop() {
    while (m_connected) {
        ssize_t received = recv(m_socket_fd, m_recv_buffer, RECV_BUFFER_SIZE, 0);

        if (received <= 0) {
            if (received < 0 && errno == EINTR) {
                continue;  // Interrupted by signal, retry
            }
            // Connection closed or error
            break;
        }

        // Process received data
        ProcessData(m_recv_buffer, static_cast<size_t>(received));
    }

    // Connection ended - cleanup
    Disconnect(false);
}

/**
 * @brief Process received data as LDN packets
 *
 * @param data Pointer to received data
 * @param size Size of received data
 *
 * Parses the data as LDN protocol packets and dispatches to handlers.
 * Each packet has:
 * - LdnHeader (magic, type, data_size)
 * - Payload (data_size bytes)
 *
 * Multiple packets may be received in a single recv() call due to TCP streaming.
 */
void P2pProxySession::ProcessData(const uint8_t* data, size_t size) {
    size_t offset = 0;

    while (offset < size) {
        // =====================================================================
        // Parse Header
        // =====================================================================

        // Need at least header size
        if (size - offset < sizeof(ryu_ldn::protocol::LdnHeader)) {
            LOG_WARN("P2P session: incomplete header");
            break;
        }

        const auto* header = reinterpret_cast<const ryu_ldn::protocol::LdnHeader*>(data + offset);

        // Validate magic number
        if (header->magic != ryu_ldn::protocol::PROTOCOL_MAGIC) {
            LOG_WARN("P2P session: invalid packet magic 0x%08X", header->magic);
            Disconnect(false);
            return;
        }

        // Calculate total packet size
        size_t packet_size = sizeof(ryu_ldn::protocol::LdnHeader) + static_cast<size_t>(header->data_size);

        // Ensure we have the complete packet
        if (offset + packet_size > size) {
            // Incomplete packet in buffer
            // This shouldn't happen with TCP unless buffer is too small
            LOG_WARN("P2P session: incomplete packet (need %zu, have %zu)",
                     packet_size, size - offset);
            break;
        }

        // Get pointer to packet payload
        const uint8_t* packet_data = data + offset + sizeof(ryu_ldn::protocol::LdnHeader);

        // =====================================================================
        // Dispatch by Packet Type
        // =====================================================================

        switch (static_cast<ryu_ldn::protocol::PacketId>(header->type)) {
            case ryu_ldn::protocol::PacketId::ExternalProxy: {
                // Client authentication packet
                if (static_cast<size_t>(header->data_size) >= sizeof(ryu_ldn::protocol::ExternalProxyConfig)) {
                    const auto* config = reinterpret_cast<const ryu_ldn::protocol::ExternalProxyConfig*>(packet_data);
                    HandleExternalProxy(*config);
                }
                break;
            }

            case ryu_ldn::protocol::PacketId::ProxyData: {
                // Data transfer packet
                if (static_cast<size_t>(header->data_size) >= sizeof(ryu_ldn::protocol::ProxyDataHeader)) {
                    auto* pheader = const_cast<ryu_ldn::protocol::ProxyDataHeader*>(
                        reinterpret_cast<const ryu_ldn::protocol::ProxyDataHeader*>(packet_data));
                    const uint8_t* payload = packet_data + sizeof(ryu_ldn::protocol::ProxyDataHeader);
                    size_t payload_len = static_cast<size_t>(header->data_size) - sizeof(ryu_ldn::protocol::ProxyDataHeader);
                    HandleProxyData(*pheader, payload, payload_len);
                }
                break;
            }

            case ryu_ldn::protocol::PacketId::ProxyConnect: {
                // Virtual TCP connect request
                if (static_cast<size_t>(header->data_size) >= sizeof(ryu_ldn::protocol::ProxyConnectRequest)) {
                    auto* request = const_cast<ryu_ldn::protocol::ProxyConnectRequest*>(
                        reinterpret_cast<const ryu_ldn::protocol::ProxyConnectRequest*>(packet_data));
                    HandleProxyConnect(*request);
                }
                break;
            }

            case ryu_ldn::protocol::PacketId::ProxyConnectReply: {
                // Virtual TCP connect response
                if (static_cast<size_t>(header->data_size) >= sizeof(ryu_ldn::protocol::ProxyConnectResponse)) {
                    auto* response = const_cast<ryu_ldn::protocol::ProxyConnectResponse*>(
                        reinterpret_cast<const ryu_ldn::protocol::ProxyConnectResponse*>(packet_data));
                    HandleProxyConnectReply(*response);
                }
                break;
            }

            case ryu_ldn::protocol::PacketId::ProxyDisconnect: {
                // Virtual TCP disconnect
                if (static_cast<size_t>(header->data_size) >= sizeof(ryu_ldn::protocol::ProxyDisconnectMessage)) {
                    auto* message = const_cast<ryu_ldn::protocol::ProxyDisconnectMessage*>(
                        reinterpret_cast<const ryu_ldn::protocol::ProxyDisconnectMessage*>(packet_data));
                    HandleProxyDisconnect(*message);
                }
                break;
            }

            default:
                LOG_WARN("P2P session: unhandled packet type %u", header->type);
                break;
        }

        // Move to next packet
        offset += packet_size;
    }
}

/**
 * @brief Handle ExternalProxy packet (client authentication)
 *
 * Called when a client sends their auth token. Delegates to server's
 * TryRegisterUser() for validation.
 */
void P2pProxySession::HandleExternalProxy(const ryu_ldn::protocol::ExternalProxyConfig& config) {
    if (!m_server->TryRegisterUser(this, config, m_remote_ip)) {
        LOG_WARN("P2P auth failed, disconnecting client");
        Disconnect(false);
    }
}

/**
 * @brief Handle ProxyData packet
 *
 * Only processed if session is authenticated. Delegates routing to server.
 */
void P2pProxySession::HandleProxyData(const ryu_ldn::protocol::ProxyDataHeader& header,
                                       const uint8_t* data, size_t data_len) {
    if (!m_authenticated) {
        LOG_WARN("ProxyData from unauthenticated session");
        return;
    }

    auto mutable_header = header;
    m_server->HandleProxyData(this, mutable_header, data, data_len);
}

/**
 * @brief Handle ProxyConnect packet
 */
void P2pProxySession::HandleProxyConnect(const ryu_ldn::protocol::ProxyConnectRequest& request) {
    if (!m_authenticated) {
        LOG_WARN("ProxyConnect from unauthenticated session");
        return;
    }

    auto mutable_request = request;
    m_server->HandleProxyConnect(this, mutable_request);
}

/**
 * @brief Handle ProxyConnectReply packet
 */
void P2pProxySession::HandleProxyConnectReply(const ryu_ldn::protocol::ProxyConnectResponse& response) {
    if (!m_authenticated) {
        LOG_WARN("ProxyConnectReply from unauthenticated session");
        return;
    }

    auto mutable_response = response;
    m_server->HandleProxyConnectReply(this, mutable_response);
}

/**
 * @brief Handle ProxyDisconnect packet
 */
void P2pProxySession::HandleProxyDisconnect(const ryu_ldn::protocol::ProxyDisconnectMessage& message) {
    if (!m_authenticated) {
        LOG_WARN("ProxyDisconnect from unauthenticated session");
        return;
    }

    auto mutable_message = message;
    m_server->HandleProxyDisconnect(this, mutable_message);
}

} // namespace ams::mitm::p2p
