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
#include <poll.h>         // poll(), pollfd, POLLIN — see AcceptLoop
#include <cerrno>         // errno
#include <cstring>        // memcmp(), memset()

namespace ams::mitm::p2p {

// =============================================================================
// Statically-allocated thread stacks
// =============================================================================
//
// os::CreateThread requires the stack pointer to be aligned to 4 KB
// (os::ThreadStackAlignment). When P2pProxyServer is constructed via
// `new` through the sysmodule's expanded heap, the heap returns 8-byte
// aligned allocations — NOT 4 KB aligned — even though the inline stack
// arrays declared `alignas(0x1000)` inside the class. The compiler can
// only honor the alignas relative to the object's base address, which
// is itself not 4 KB aligned. CreateThread then refuses the misaligned
// stack and R_ABORT_UNLESS fires DABRT 0x101.
//
// Move the stacks out of the class into BSS, where the linker honors
// the alignment, and have the (singleton) server reference them.
// Single-host model means we only ever need one set of stacks.
alignas(os::ThreadStackAlignment) constinit u8 g_p2p_accept_thread_stack[0x4000];
alignas(os::ThreadStackAlignment) constinit u8 g_p2p_lease_thread_stack[0x2000];

// =============================================================================
// Session Recv Thread Stack Pool
// =============================================================================
//
// One stack per concurrent peer (capped at MAX_PLAYERS = 8). Stacks live in
// BSS for the same reason as the accept/lease ones above — `alignas(0x1000)`
// inside the P2pProxySession class made the class itself over-aligned, so
// `new P2pProxySession(...)` in AcceptLoop fell through to the unimplemented
// aligned operator new and DABRT'd 0x101 the moment a peer hit our port.
//
// Slots are claimed/released under g_p2p_session_stack_mutex so concurrent
// accept() callers (the accept thread is single, but Disconnect() can race
// with the next accept) don't double-allocate or leak slots.
constexpr int P2P_SESSION_STACK_COUNT = P2pProxyServer::MAX_PLAYERS;
constexpr size_t P2P_SESSION_STACK_SIZE = 0x4000;
alignas(os::ThreadStackAlignment) constinit u8
    g_p2p_session_stacks[P2P_SESSION_STACK_COUNT][P2P_SESSION_STACK_SIZE];
constinit bool g_p2p_session_stack_used[P2P_SESSION_STACK_COUNT] = {};
constinit os::SdkMutex g_p2p_session_stack_mutex;

// Returns slot index in [0, P2P_SESSION_STACK_COUNT), or -1 if the pool is full.
int AllocateSessionStackSlot() {
    std::scoped_lock lock(g_p2p_session_stack_mutex);
    for (int i = 0; i < P2P_SESSION_STACK_COUNT; ++i) {
        if (!g_p2p_session_stack_used[i]) {
            g_p2p_session_stack_used[i] = true;
            return i;
        }
    }
    return -1;
}

void ReleaseSessionStackSlot(int slot) {
    if (slot < 0 || slot >= P2P_SESSION_STACK_COUNT) return;
    std::scoped_lock lock(g_p2p_session_stack_mutex);
    g_p2p_session_stack_used[slot] = false;
}

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
    // Mark the session as fully drained so the server's reaper can join the
    // thread and `delete` the object. We can't `delete this` from inside the
    // recv thread itself — that would free the thread's own stack mid-return.
    session->m_thread_done.store(true, std::memory_order_release);
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
    , m_zombie_session_count(0)
    , m_waiting_token_count(0)
    , m_broadcast_address(0)
    , m_master_callback(master_callback)
    , m_callback_user_data(user_data)
    , m_host_virtual_ip(0)
    , m_host_data_callback(nullptr)
    , m_host_data_user_data(nullptr)
{
    // Initialize session array to nullptr
    // This is important - we use nullptr to detect empty slots
    for (int i = 0; i < MAX_PLAYERS; i++) {
        m_sessions[i].reset();
    }
    for (int i = 0; i < MAX_ZOMBIE_SESSIONS; i++) {
        m_zombie_sessions[i].reset();
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
    //
    // No setsockopt on the listen socket. Aligning strictly with
    // NetCoreServer.TcpServer.Start (TcpServer.cs:200-230) — there, only
    // OptionReuseAddress / OptionExclusiveAddressUse / OptionDualMode are
    // applied to the acceptor socket, *all default false*. OptionNoDelay
    // is applied to ACCEPTED sockets (TcpSession.Connect), never to the
    // listen socket. The accepted-socket TCP_NODELAY is still set in
    // AcceptLoop below to match.
    //
    // Why the change: previously we set SO_REUSEADDR=1 and TCP_NODELAY=1
    // on the listen fd. On a POSIX kernel both are no-ops on a passive
    // socket, but on the Switch bsd:s service we observed inbound SYN to
    // the listen socket never landing on accept(); removing these
    // unexpected options on the listen fd brings the bsd:s state into
    // exact parity with what NetCoreServer asks for and unblocks
    // server-side IsProxyReachable from the master.

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
    // Thread priority 5 — slightly above the MITM service thread (priority 6
    // in main.cpp) so connection accepts aren't starved. We previously used
    // `HighestThreadPriority - 1` (= -1) which falls in the system-priority
    // range (HighestSystemThreadPriority = -12) and a regular sysmodule has
    // no kernel capability to request it; svcCreateThread refused and
    // R_ABORT_UNLESS panicked the process with DABRT 0x101 the first time
    // a host called CreateNetwork.

    m_running = true;

    R_ABORT_UNLESS(os::CreateThread(
        &m_accept_thread,
        AcceptThreadEntry,                // Entry function
        this,                              // Argument (this pointer)
        g_p2p_accept_thread_stack,         // Static, page-aligned stack (BSS)
        sizeof(g_p2p_accept_thread_stack), // Stack size (16KB)
        5                                  // High user-mode priority
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
                m_sessions[i].reset();
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

    // The Stop() loop above already Disconnect+delete'd the *active*
    // sessions in m_sessions[]. There may still be entries sitting in
    // m_zombie_sessions that disconnected earlier (their recv threads
    // returned but AcceptLoop didn't get another iteration). Reap them
    // now so the recv buffers (64 KB each) and stack slots come back
    // before the server object goes away.
    ReapZombieSessions();

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
    LOG_INFO("NatPunch: entering, getting UpnpPortMapper instance");
    auto& mapper = UpnpPortMapper::GetInstance();
    LOG_INFO("NatPunch: calling mapper.Discover() (SSDP multicast, 2500ms timeout)");

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
    LOG_INFO("ReleaseNatPunch: ENTRY (lease_running=%d, public_port=%u)",
             static_cast<int>(m_lease_thread_running), m_public_port);

    // =========================================================================
    // Stop Lease Renewal Thread
    // =========================================================================
    // Must stop before deleting mapping, otherwise renewal might
    // try to refresh a deleted mapping.

    if (m_lease_thread_running) {
        LOG_INFO("ReleaseNatPunch: signalling lease thread, waiting for join (~250 ms max)");
        m_lease_thread_running = false;
        // Thread exits within ~ChunkMs (=250 ms) of seeing the flag flip.
        os::WaitThread(&m_lease_thread);
        os::DestroyThread(&m_lease_thread);
        LOG_INFO("ReleaseNatPunch: lease thread joined");
    }

    // =========================================================================
    // Delete Port Mapping
    // =========================================================================

    if (m_public_port != 0) {
        LOG_INFO("ReleaseNatPunch: deleting UPnP mapping for port %u", m_public_port);
        auto& mapper = UpnpPortMapper::GetInstance();
        if (mapper.DeletePortMapping(m_public_port)) {
            LOG_INFO("UPnP port mapping released: %u", m_public_port);
        } else {
            LOG_WARN("Failed to release UPnP port mapping: %u", m_public_port);
        }
        m_public_port = 0;
    }

    LOG_INFO("ReleaseNatPunch: EXIT");
    // Flush — ReleaseNatPunch is in the critical path of StopP2pProxyServer.
    // If ConnectionLost / KP / infinite loading follows, we want to be sure we
    // can tell WHEN we left ReleaseNatPunch.
    ryu_ldn::debug::g_logger.flush();
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
        g_p2p_lease_thread_stack,         // Static, page-aligned stack (BSS)
        sizeof(g_p2p_lease_thread_stack),
        os::LowestThreadPriority           // Low priority - not time-critical
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
    LOG_INFO("P2P LeaseRenewalLoop: entry, m_lease_thread_running=%d, m_disposed=%d",
             static_cast<int>(m_lease_thread_running), static_cast<int>(m_disposed));
    ryu_ldn::debug::g_logger.flush();

    // We chunk the long renewal interval into short sleeps so ReleaseNatPunch()
    // can join us within ~ChunkMs of asking instead of waiting up to the full
    // renew period (the previous code did `SleepThread(50s)` and any Stop()
    // call landing during that nap blocked WaitThread for up to 50 s — game
    // IPC sat there and the lobby never advanced past "Creating session".
    // Same external semantics as upstream Ryujinx
    // (P2pProxyServer.cs ExecuteAfterDelayAsync + _disposedCancellation.Cancel()):
    // Stop cancels the renew task and returns immediately.
    constexpr int64_t ChunkMs   = 250;
    const int64_t total_ms = static_cast<int64_t>(PORT_LEASE_RENEW) * 1000;

    while (m_lease_thread_running && !m_disposed) {
        // Wait for the full renewal interval, but in ChunkMs slices so a
        // flipped m_lease_thread_running pulls us out within one chunk.
        for (int64_t waited = 0;
             waited < total_ms && m_lease_thread_running && !m_disposed;
             waited += ChunkMs) {
            const auto chunk_ns = TimeSpan::FromMilliSeconds(ChunkMs).GetNanoSeconds();
            svc::SleepThread(chunk_ns);
        }

        // Check if we should exit (Stop / dispose flipped during the slices)
        if (!m_lease_thread_running || m_disposed) {
            LOG_INFO("LeaseRenewalLoop: exit requested (running=%d, disposed=%d)",
                     static_cast<int>(m_lease_thread_running),
                     static_cast<int>(m_disposed));
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

    LOG_INFO("LeaseRenewalLoop: thread exiting cleanly");
    // Flush — point de sortie du thread, on veut être sûr de savoir
    // exactement quand le thread a fini son travail.
    ryu_ldn::debug::g_logger.flush();
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
            //
            // The 16-byte token is a fresh GUID generated server-side per
            // joiner; the IP check is a belt-and-suspenders against someone
            // who snooped the token on a public network and tried to race
            // the legitimate joiner. When the master server, the joiner
            // and the host don't all see each other through the same
            // network path (typical dev-loop: master + joiner on one
            // machine, host is a Switch on the same LAN), `token.physical_ip`
            // (the joiner endpoint as the *server* sees it) and `remote_ip`
            // (as the *host* sees it) legitimately differ. Accept on token
            // match alone in that case and log a warning — see
            // docs/notes/p2p_local_loopback_auth_fallback.md.

            if (token_match && !ip_match) {
                // Recompute token_ip for the warning text only.
                // NOTE: the 16-byte auth token is intentionally NOT logged —
                // it is a per-joiner secret. Only the resolved physical IP is
                // surfaced, which is a routing diagnostic, not a credential.
                uint32_t token_ip_log = (static_cast<uint32_t>(token.physical_ip[0]) << 24) |
                                        (static_cast<uint32_t>(token.physical_ip[1]) << 16) |
                                        (static_cast<uint32_t>(token.physical_ip[2]) << 8)  |
                                        static_cast<uint32_t>(token.physical_ip[3]);
                LOG_WARN("P2P auth: token matches but physical IP differs "
                         "(server saw 0x%08X, we see 0x%08X). Accepting on "
                         "token alone — usually a dev-loop where the master "
                         "server and joiner share one machine.",
                         token_ip_log, remote_ip);
                ip_match = true;
            }

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
                        m_sessions[j].reset(session);
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

                // Hex-dump the encoded packet so we can compare the wire format
                // against what the master server emits in relay mode (which
                // Ryujinx joiners parse fine). 20 bytes total = 12-byte header
                // + 8-byte ProxyConfig payload.
#ifdef DEBUG_HEX_DUMP
                {
                    char hex[3 * 32 + 1] = {};
                    size_t dump_len = len < 32 ? len : 32;
                    for (size_t i = 0; i < dump_len; i++) {
                        std::snprintf(hex + i * 3, 4, "%02X ", packet[i]);
                    }
                    LOG_INFO("ProxyConfig wire (%zu B): %s", len, hex);
                }
#endif

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
// Host self-routing — in-process shortcut
// =============================================================================

void P2pProxyServer::SetHostVirtualIp(uint32_t virtual_ip) {
    std::scoped_lock lock(m_mutex);
    m_host_virtual_ip = virtual_ip;
    LOG_VERBOSE("P2P host virtual IP set to 0x%08X", virtual_ip);
}

void P2pProxyServer::SetHostDataCallback(HostProxyDataCallback callback, void* user_data) {
    std::scoped_lock lock(m_mutex);
    m_host_data_callback = callback;
    m_host_data_user_data = user_data;
}

bool P2pProxyServer::BroadcastFromHost(ryu_ldn::protocol::ProxyDataHeader& header,
                                        const uint8_t* data, size_t data_len) {
    std::scoped_lock lock(m_mutex);

    if (m_host_virtual_ip == 0) {
        LOG_WARN("BroadcastFromHost called before SetHostVirtualIp()");
        return false;
    }

    // Mirror RouteMessage's source-IP handling — fill in our virtual IP if
    // the host's BSD socket was bound on 0.0.0.0.
    if (header.info.source_ipv4 == 0) {
        header.info.source_ipv4 = m_host_virtual_ip;
    } else if (header.info.source_ipv4 != m_host_virtual_ip) {
        LOG_WARN("BroadcastFromHost: source 0x%08X != host vIP 0x%08X — refusing",
                 header.info.source_ipv4, m_host_virtual_ip);
        return false;
    }

    // Some games still emit the legacy 192.168.0.255 broadcast. Translate
    // it the same way RouteMessage does for joiner-originated traffic.
    uint32_t dest_ip = header.info.dest_ipv4;
    if (dest_ip == 0xc0a800ff) {
        dest_ip = m_broadcast_address;
        header.info.dest_ipv4 = dest_ip;
    }

    const bool is_broadcast = (dest_ip == m_broadcast_address);

    uint8_t packet[0x10000];
    size_t encoded = 0;
    auto enc = ryu_ldn::protocol::encode_with_data(
        packet, sizeof(packet),
        ryu_ldn::protocol::PacketId::ProxyData,
        header, data, data_len, encoded);
    if (enc != ryu_ldn::protocol::EncodeResult::Success) {
        LOG_WARN("BroadcastFromHost: encode failed (data_len=%zu)", data_len);
        return false;
    }

    bool any_sent = false;
    if (is_broadcast) {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (m_sessions[i] != nullptr && m_sessions[i]->IsAuthenticated()) {
                if (m_sessions[i]->Send(packet, encoded)) {
                    any_sent = true;
                }
            }
        }
    } else {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (m_sessions[i] != nullptr &&
                m_sessions[i]->IsAuthenticated() &&
                m_sessions[i]->GetVirtualIpAddress() == dest_ip) {
                any_sent = m_sessions[i]->Send(packet, encoded);
                break;
            }
        }
    }
    return any_sent;
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
 * The listen socket stays blocking, but every iteration of the loop calls
 * poll(POLLIN, AcceptPollTimeoutMs) before accept() so a flipped
 * m_running can pull us out without depending on close(listen_fd) waking
 * the kernel-side accept (Switch bsd:s does not always do that the way
 * Linux does — close on the listen fd left accept() spinning on errno=113
 * forever, blocking Stop()'s WaitThread).
 *
 * Sémantique externe identique à NetCoreServer.TcpServer.Stop()
 * (TcpServer.cs:254-295): Stop() flips a flag, the accept side stops
 * arming new accepts, the listen socket is closed, all sessions are
 * disconnected, the call returns. We just substitute their async
 * Socket.AcceptAsync + IOCP callback with a blocking poll on a
 * BSD-native loop — same observable behaviour, BSD-friendly impl.
 */
void P2pProxyServer::AcceptLoop() {
    LOG_INFO("P2P AcceptLoop: entry, listen_fd=%d, m_running=%d",
             m_listen_fd, static_cast<int>(m_running.load()));
    ryu_ldn::debug::g_logger.flush();

    constexpr int AcceptPollTimeoutMs = 250;

    while (m_running.load(std::memory_order_acquire)) {
        // Free any previously disconnected sessions before accepting another
        // connection. AcceptLoop is the natural reap site because every
        // accept potentially triggers a `new P2pProxySession` (64 KB recv
        // buffer + entry in the BSS stack pool) and we want freed slots
        // back before allocating again.
        ReapZombieSessions();

        // Wait for an incoming connection or for Stop() to flip m_running.
        // poll() returns 0 on timeout, >0 if the listen socket is readable
        // (= a connection is queued for accept), <0 on error.
        struct pollfd pfd{};
        pfd.fd = m_listen_fd;
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, AcceptPollTimeoutMs);
        if (pr == 0) {
            // Timeout — re-check m_running on the next loop iteration.
            continue;
        }
        if (pr < 0) {
            if (m_running.load(std::memory_order_acquire) && errno != EINTR) {
                LOG_ERROR("P2P accept poll failed: errno=%d", errno);
            }
            continue;
        }
        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            // Listen socket has been closed by Stop() — exit cleanly.
            if (m_running.load(std::memory_order_acquire)) {
                LOG_WARN("P2P listen socket reported revents=0x%X, exiting accept loop",
                         pfd.revents);
            }
            break;
        }

        // poll said POLLIN: at least one connection should be ready.
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(m_listen_fd,
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &client_len);

        if (client_fd < 0) {
            // Check if we're shutting down
            if (m_running.load(std::memory_order_acquire)) {
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
        //
        // On Switch the custom heap allocator (lmem::ExpHeap, 384 KB) can
        // return nullptr under pressure — the overridden `new` does NOT
        // throw. We must null-check before dereferencing `session`.

        auto* session = new P2pProxySession(this, client_fd, remote_ip);
        if (session == nullptr) {
            LOG_ERROR("P2P AcceptLoop: failed to allocate P2pProxySession "
                       "(heap exhausted?) — closing client_fd=%d", client_fd);
            close(client_fd);
            continue;
        }
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

    // Write the canonicalised dest_ip back into info — joiner-side
    // LdnProxy.ForRoutedSockets matches on info.DestPort but the IP field
    // is part of the wire dump, so emit the resolved one so multi-hop
    // routing (and the host self-callback below) stays consistent.
    info.dest_ipv4 = dest_ip;

    bool is_broadcast = (dest_ip == m_broadcast_address);

    LOG_INFO("RouteMessage: sender_vIP=0x%08X src=0x%08X dst=0x%08X (orig=0x%08X) bcast=%d, scanning sessions",
             sender->GetVirtualIpAddress(), info.source_ipv4, dest_ip, info.dest_ipv4,
             static_cast<int>(is_broadcast));
    ryu_ldn::debug::g_logger.flush();

    // =========================================================================
    // Route to Destination(s)
    // =========================================================================

    if (is_broadcast) {
        // Send to all authenticated players
        for (int i = 0; i < MAX_PLAYERS; i++) {
            P2pProxySession* s = m_sessions[i].get();
            LOG_INFO("RouteMessage: slot %d -> %p%s",
                     i, static_cast<void*>(s),
                     s == nullptr ? " (null)" :
                     (s->IsAuthenticated() ? " (auth)" : " (unauth)"));
            ryu_ldn::debug::g_logger.flush();
            if (s != nullptr && s->IsAuthenticated()) {
                send_func(s);
            }
        }
    } else {
        // Send to specific player by virtual IP
        for (int i = 0; i < MAX_PLAYERS; i++) {
            P2pProxySession* s = m_sessions[i].get();
            if (s != nullptr && s->IsAuthenticated() &&
                s->GetVirtualIpAddress() == dest_ip) {
                LOG_INFO("RouteMessage: unicast match slot %d (vIP 0x%08X)", i, dest_ip);
                ryu_ldn::debug::g_logger.flush();
                send_func(s);
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
// Whether a given dest IP, after broadcast translation, lands on the host's
// local game. Caller must hold m_mutex (or be otherwise safe against
// concurrent updates to m_host_virtual_ip / m_broadcast_address).
static inline bool HostShouldReceive(uint32_t dest_ip,
                                     uint32_t host_vip,
                                     uint32_t broadcast_address) {
    if (dest_ip == 0xc0a800ff) {
        dest_ip = broadcast_address;
    }
    if (dest_ip == broadcast_address) return true;
    if (host_vip != 0 && dest_ip == host_vip) return true;
    return false;
}

void P2pProxyServer::HandleProxyData(P2pProxySession* sender,
                                      ryu_ldn::protocol::ProxyDataHeader& header,
                                      const uint8_t* data, size_t data_len) {
    LOG_INFO("HandleProxyData ENTRY: sender_vIP=0x%08X, src=0x%08X:%u dst=0x%08X:%u proto=%u len=%zu",
             sender->GetVirtualIpAddress(),
             header.info.source_ipv4, header.info.source_port,
             header.info.dest_ipv4,   header.info.dest_port,
             static_cast<unsigned>(header.info.protocol), data_len);
    ryu_ldn::debug::g_logger.flush();

    RouteMessage(sender, header.info, [&](P2pProxySession* target) {
        LOG_INFO("HandleProxyData ROUTE: sender_vIP=0x%08X -> target_vIP=0x%08X (target=%p)",
                 sender->GetVirtualIpAddress(),
                 target ? target->GetVirtualIpAddress() : 0,
                 static_cast<void*>(target));
        ryu_ldn::debug::g_logger.flush();
        // Encode and send to target
        uint8_t packet[0x10000];  // 64KB max packet
        size_t len = 0;
        ryu_ldn::protocol::encode_with_data(packet, sizeof(packet),
                                            ryu_ldn::protocol::PacketId::ProxyData,
                                            header, data, data_len, len);
        target->Send(packet, len);
    });

    // After RouteMessage canonicalises source_ipv4 / dest_ipv4 the header
    // reflects what other joiners will see on the wire. If this packet is
    // also for us (broadcast or directly addressed to the host's vIP), feed
    // it into the host's BSD MITM via the registered callback. RouteMessage
    // has already rejected spoof attempts before reaching this point.
    HostProxyDataCallback host_cb;
    void* host_user_data;
    bool to_host;
    {
        std::scoped_lock lock(m_mutex);
        host_cb = m_host_data_callback;
        host_user_data = m_host_data_user_data;
        to_host = HostShouldReceive(header.info.dest_ipv4,
                                    m_host_virtual_ip,
                                    m_broadcast_address);
    }
    if (to_host && host_cb != nullptr) {
        host_cb(header.info.source_ipv4, header.info.source_port,
                header.info.dest_ipv4,   header.info.dest_port,
                header.info.protocol,
                data, data_len, host_user_data);
    }

    LOG_INFO("HandleProxyData EXIT: sender_vIP=0x%08X", sender->GetVirtualIpAddress());
    ryu_ldn::debug::g_logger.flush();
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
void P2pProxyServer::ReapZombieSessions() {
    // Snapshot the current zombies under the lock, release the lock, then
    // join+delete outside it. WaitThread can be slow if the recv thread is
    // mid-syscall and we don't want to hold m_mutex while it drains.
    P2pProxySession* zombies[MAX_ZOMBIE_SESSIONS];
    int taken = 0;
    {
        std::scoped_lock lock(m_mutex);
        int kept = 0;
        for (int i = 0; i < m_zombie_session_count; ++i) {
            P2pProxySession* z = m_zombie_sessions[i].get();
            if (z != nullptr && z->m_thread_done.load(std::memory_order_acquire)) {
                zombies[taken++] = z;
            } else {
                m_zombie_sessions[kept++].reset(z);
            }
        }
        m_zombie_session_count = kept;
        for (int i = kept; i < MAX_ZOMBIE_SESSIONS; ++i) {
            m_zombie_sessions[i].reset();
        }
    }

    for (int i = 0; i < taken; ++i) {
        P2pProxySession* z = zombies[i];
        // Thread has returned — join and destroy the ThreadType, then free
        // the object. The destructor releases m_stack_slot back to the BSS
        // pool so a future joiner can reuse the slot.
        os::WaitThread(&z->m_recv_thread);
        os::DestroyThread(&z->m_recv_thread);
        delete z;
    }
}

void P2pProxyServer::HandleExternalProxyStateChange(uint32_t virtual_ip, bool connected) {
    if (connected) {
        // Upstream HandleStateChange only acts on the disconnect side. The
        // connect-side notification has no effect (the peer adds itself to
        // _players via TryRegisterUser when its TCP session authenticates).
        return;
    }

    // Snapshot sessions to disconnect under m_mutex, then drop the lock
    // before calling Disconnect — Disconnect() → OnSessionDisconnected()
    // re-takes m_mutex and would deadlock if we held it across the call.
    P2pProxySession* victims[MAX_PLAYERS] = {};
    int victim_count = 0;
    {
        std::scoped_lock lock(m_mutex);

        // Drop any waiting token for this vIP so a stale slot in
        // m_waiting_tokens doesn't block future joiners (mirrors
        // upstream `_waitingTokens.RemoveAll(t => t.VirtualIp == ip)`).
        int kept = 0;
        for (int i = 0; i < m_waiting_token_count; i++) {
            if (m_waiting_tokens[i].virtual_ip != virtual_ip) {
                if (kept != i) {
                    m_waiting_tokens[kept] = m_waiting_tokens[i];
                }
                kept++;
            }
        }
        if (kept != m_waiting_token_count) {
            LOG_INFO("ExternalProxyState: dropped %d waiting token(s) for vIP 0x%08X",
                     m_waiting_token_count - kept, virtual_ip);
            m_waiting_token_count = kept;
        }

        // Find sessions matching this vIP. Don't disconnect inside the
        // loop — Disconnect → OnSessionDisconnected reenters m_mutex.
        for (int i = 0; i < MAX_PLAYERS && victim_count < MAX_PLAYERS; i++) {
            if (m_sessions[i] != nullptr &&
                m_sessions[i]->GetVirtualIpAddress() == virtual_ip) {
                victims[victim_count++] = m_sessions[i].get();
            }
        }
    }

    for (int i = 0; i < victim_count; i++) {
        LOG_INFO("ExternalProxyState: disconnecting session vIP 0x%08X (master said connected=false)",
                 virtual_ip);
        // from_master=false so OnSessionDisconnected runs the normal
        // cleanup (zombie-queue + slot release). Upstream uses
        // DisconnectAndStop which sets _masterClosed=true to skip the
        // notification back to master — but our NotifyMasterDisconnect
        // only fires inside OnSessionDisconnected when the session was
        // authenticated, which is fine here too: the master just told us
        // it's gone, sending it back is harmless and matches our
        // existing relay-side behaviour.
        victims[i]->Disconnect(false);
    }
}

void P2pProxyServer::OnSessionDisconnected(P2pProxySession* session) {
    std::scoped_lock lock(m_mutex);

    // Find and remove from session array
    bool found = false;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (m_sessions[i].get() == session) {
            m_sessions[i].reset();
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

    // Queue the session for deletion. We can't delete it here because we
    // are typically called from the session's own recv thread (Disconnect →
    // OnSessionDisconnected) and `delete this` would free the running
    // thread's stack. ReapZombieSessions() — invoked from AcceptLoop on
    // every iteration and from Stop() — joins the recv thread and frees
    // the object once m_thread_done flips. Without this queueing the
    // 64 KB recv buffer leaked on every disconnect, and ~5 reconnects
    // were enough to exhaust the 384 KB sysmodule heap and DABRT 0x101
    // on the next allocation.
    if (m_zombie_session_count < MAX_ZOMBIE_SESSIONS) {
        m_zombie_sessions[m_zombie_session_count++].reset(session);
    } else {
        LOG_ERROR("Zombie session pool full (%d), dropping pointer — heap will leak",
                  MAX_ZOMBIE_SESSIONS);
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
    , m_stack_slot(-1)
{
}

/**
 * @brief Destructor - ensure disconnection
 */
P2pProxySession::~P2pProxySession() {
    Disconnect(true);
    // Return the receive-thread stack slot to the global pool so the next
    // peer that connects can reuse it.
    if (m_stack_slot >= 0) {
        ReleaseSessionStackSlot(m_stack_slot);
        m_stack_slot = -1;
    }
}

/**
 * @brief Start the receive thread
 *
 * Creates and starts a thread that loops calling recv() on the socket.
 * The thread processes received data and dispatches to appropriate handlers.
 */
void P2pProxySession::Start() {
    // Claim a stack slot from the BSS pool — the stack lives there because
    // inlining `alignas(0x1000) uint8_t stack[N]` inside this class made
    // P2pProxySession itself over-aligned to 4 KB and the heap allocator
    // (which only guarantees 8-byte alignment) caused new P2pProxySession
    // to fall through to the unimplemented aligned operator new => DABRT
    // 0x101 the moment a peer hit our port.
    m_stack_slot = AllocateSessionStackSlot();
    if (m_stack_slot < 0) {
        LOG_ERROR("P2pProxySession::Start: stack pool exhausted (max=%d), session dropped",
                  P2P_SESSION_STACK_COUNT);
        // Without a stack we cannot run the recv thread; mark the session
        // disconnected so the caller cleans it up. We don't close the fd
        // here — the caller's session-cleanup path handles that.
        m_connected = false;
        return;
    }

    // Priority 7 — slightly below the accept thread (5) so accept can preempt
    // a slow recv handler. `HighestThreadPriority - 2` (= -2) was a system
    // priority that the kernel refuses for a normal sysmodule, same DABRT as
    // the accept thread used to take.
    R_ABORT_UNLESS(os::CreateThread(
        &m_recv_thread,
        SessionRecvThreadEntry,
        this,
        g_p2p_session_stacks[m_stack_slot],
        P2P_SESSION_STACK_SIZE,
        7                                // High user-mode priority
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
        LOG_WARN("P2pProxySession::Send skipped: m_connected=%d, fd=%d, size=%zu",
                 static_cast<int>(m_connected), m_socket_fd, size);
        return false;
    }

    // MSG_NOSIGNAL: don't raise SIGPIPE if the peer already closed the socket
    // — surface the failure as EPIPE on the return path instead. Mirrors what
    // network/socket.cpp does for the master TCP client.
    ssize_t sent = send(m_socket_fd, data, size, MSG_NOSIGNAL);

    if (sent != static_cast<ssize_t>(size)) {
        LOG_WARN("P2pProxySession::Send short/failed: fd=%d size=%zu sent=%zd errno=%d",
                 m_socket_fd, size, sent, errno);
        return false;
    }

    LOG_INFO("P2pProxySession::Send ok: fd=%d virtual_ip=0x%08X size=%zu",
             m_socket_fd, m_virtual_ip, size);
    ryu_ldn::debug::g_logger.flush();
    return true;
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
    const uint64_t entry_ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;
    LOG_INFO("P2pProxySession::ReceiveLoop: entry, fd=%d, slot=%d, this=%p",
             m_socket_fd, m_stack_slot, static_cast<void*>(this));
    ryu_ldn::debug::g_logger.flush();
    uint64_t last_recv_ms = entry_ms;
    while (m_connected) {
        ssize_t received = recv(m_socket_fd, m_recv_buffer, RECV_BUFFER_SIZE, 0);

        const uint64_t now_ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;
        if (received <= 0) {
            if (received < 0 && errno == EINTR) {
                continue;  // Interrupted by signal, retry
            }
            // Connection closed or error. The elapsed time since the previous
            // recv tells us whether the joiner closed immediately or sat
            // waiting (~4 s = Ryujinx P2pProxyClient EnsureProxyReady timeout).
            LOG_INFO("P2pProxySession::ReceiveLoop: exit (recv=%zd, errno=%d, m_connected=%d, "
                     "elapsed_since_last_recv=%llu ms)",
                     received, errno, static_cast<int>(m_connected),
                     static_cast<unsigned long long>(now_ms - last_recv_ms));
            ryu_ldn::debug::g_logger.flush();
            break;
        }

        LOG_INFO("P2pProxySession: recv %zd bytes (after %llu ms), dispatching",
                 received,
                 static_cast<unsigned long long>(now_ms - last_recv_ms));
        ryu_ldn::debug::g_logger.flush();
        last_recv_ms = now_ms;
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
