/**
 * @file bsd_mitm_service.cpp
 * @brief BSD MITM Service implementation - Socket interception for LDN proxy
 *
 * ## Overview
 *
 * This file implements all BSD socket service commands with full forwarding
 * to the real bsd:u service. The MITM service intercepts all IPC calls and
 * transparently forwards them.
 *
 * ## Forwarding Architecture
 *
 * This MITM uses Atmosphere's `serviceMitmDispatch*` macros to forward
 * IPC calls to the real bsd:u service. The macros handle:
 *
 * - Buffer marshalling (InBuffer, OutBuffer, AutoSelectBuffer)
 * - Handle forwarding (CopyHandle, MoveHandle)
 * - PID override with MITM tag (0xFFFE prefix)
 * - Response parsing and error propagation
 *
 * ## Forward Service Access
 *
 * The `m_forward_service` member (inherited from MitmServiceImplBase) holds
 * a session to the real bsd:u service. All forwarding calls use this session.
 *
 * ## Buffer Attributes (from switchbrew)
 *
 * BSD service uses specific buffer types defined by SfBufferAttr:
 * - SfBufferAttr_HipcMapAlias: Type-A (0x5) or Type-B (0x6) buffer
 * - SfBufferAttr_HipcAutoSelect: Type-0x21 (in) or Type-0x22 (out) auto-select
 * - SfBufferAttr_In: Input buffer
 * - SfBufferAttr_Out: Output buffer
 *
 * ## Command Reference
 *
 * https://switchbrew.org/wiki/Sockets_services#bsd:u.2C_bsd:s
 *
 * ## LDN Interception Strategy (Future Stories)
 *
 * In Story 8.4+, we will add:
 * 1. Socket tracking table to monitor all created sockets
 * 2. LDN address detection in Bind/Connect (10.114.x.x)
 * 3. ProxyData packet routing for LDN sockets
 * 4. Virtual socket emulation for LDN traffic
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "bsd_mitm_service.hpp"
#include "proxy_socket_manager.hpp"
#include "bsd_types.hpp"
#include "../debug/log.hpp"
#include "../ldn/ldn_shared_state.hpp"
#include "../config/game_whitelist.hpp"

// Atmosphere MITM dispatch macros for IPC forwarding
#include <stratosphere/sf/sf_mitm_dispatch.h>

extern "C" {
#include <switch/services/bsd.h>
}

namespace ams::mitm::bsd {

// =============================================================================
// Socket Type/Protocol Tracking
// =============================================================================

/**
 * @brief Info stored for each socket created via Socket()
 *
 * We need to track this because when Bind/Connect is called later,
 * we need to know the socket type and protocol to create the ProxySocket.
 */
struct SocketInfo {
    ryu_ldn::bsd::SocketType type;
    ryu_ldn::bsd::ProtocolType protocol;
    bool is_proxy;  // True if this socket is an LDN proxy socket
    bool broadcast; // SO_BROADCAST option (tracked before proxy creation)
    u32 session_id; // Session ID that owns this socket (for cleanup on session close)
};

/**
 * @brief Socket info tracking table
 *
 * Maps file descriptor to socket info. This is needed because Socket()
 * creates the fd but Bind/Connect happens later, and we need the socket
 * type/protocol to create the ProxySocket.
 *
 * NOTE: This is a global static shared across all MITM clients. Access
 * must be protected by g_socket_info_mutex. Different client processes
 * may have the same fd values, but since we're in the sysmodule context,
 * fd collisions are handled by the fact that each BsdMitmService instance
 * forwards to a different real service session.
 */
static std::unordered_map<s32, SocketInfo> g_socket_info;

/**
 * @brief Mutex protecting g_socket_info
 *
 * Must be held when reading or modifying g_socket_info.
 */
static os::Mutex g_socket_info_mutex{false};

/**
 * @brief Map of PIDs to session count for BSD MITM
 *
 * Tracks how many BSD sessions have been intercepted per process.
 * We intercept ALL BSD sessions from whitelisted games because some games
 * (like Mario Kart 8) may use multiple sessions for different operations.
 * Value: number of BSD sessions intercepted for this PID.
 */
static std::unordered_map<u64, u32> g_mitm_pid_count;
static os::Mutex g_mitm_pids_mutex{false};

/**
 * @brief List of abandoned forward services (sessions that never got RegisterClient)
 *
 * When a BSD MITM session is closed without ever receiving RegisterClient,
 * we move its forward_service here instead of closing it. This prevents
 * a system freeze that occurs when closing an unregistered bsd:u session.
 *
 * These services will be cleaned up when the game process exits or when
 * we explicitly clean them (e.g., on LDN disconnect).
 */
static std::vector<std::shared_ptr<::Service>> g_abandoned_forward_services;
static os::Mutex g_abandoned_services_mutex{false};

// =============================================================================
// Constructor / Destructor
// =============================================================================

/**
 * @brief Construct BSD MITM service for a client process
 *
 * Called by Atmosphere's server manager when a process opens bsd:u
 * and ShouldMitm() returns true.
 *
 * ## Forward Service
 *
 * The `s` parameter is a shared_ptr to a Service structure that represents
 * an open session to the real bsd:u service. This is stored in the inherited
 * `m_forward_service` member and used for all forwarding calls via
 * `serviceMitmDispatch*` macros.
 *
 * ## Client Tracking
 *
 * We store the client's PID for logging and future socket tracking.
 * Each client process gets its own BsdMitmService instance with its own
 * forward service session.
 *
 * @param s Shared pointer to the original bsd:u service session
 * @param c Process information for the client (PID, program ID, etc.)
 */
BsdMitmService::BsdMitmService(std::shared_ptr<::Service>&& s, const sm::MitmProcessInfo& c)
    : MitmServiceImplBase(std::forward<std::shared_ptr<::Service>>(s), c)
    , m_client_pid(c.process_id.value)
    , m_session_id(++s_next_session_id)
{
    // Get more info about the forward service
    ::Service* fwd = m_forward_service.get();
    Handle session_handle = fwd ? fwd->session : INVALID_HANDLE;
    bool is_domain = fwd ? serviceIsDomain(fwd) : false;
    u32 object_id = fwd ? fwd->object_id : 0;

    LOG_INFO("[BSD#%u] CONSTRUCTOR: program_id=0x%016lx, pid=%lu, fwd_srv=%p (handle=0x%x, domain=%d, object_id=%u)",
             m_session_id, c.program_id.value, m_client_pid, fwd, session_handle, is_domain, object_id);
    // FIX v6-v7 (#44): Count active IPC sessions to detect game exit.
    // Each BsdMitmService instance is one bsd:u session.
    ams::mitm::ldn::SharedState::GetInstance().IncrementSessionCount();
}

/**
 * @brief Destroy BSD MITM service
 *
 * Called when the client process closes its bsd:u session or terminates.
 * The forward_service session is automatically closed by the shared_ptr.
 *
 * ## Future Cleanup (Story 8.4)
 *
 * When socket tracking is implemented, this destructor will cleanup
 * all tracked sockets for this client to prevent resource leaks.
 */
BsdMitmService::~BsdMitmService() {
    // Clean up all socket info entries belonging to this session
    // This prevents memory leaks when games crash or exit without closing sockets
    //
    // IMPORTANT: We must NOT hold g_socket_info_mutex while calling CloseProxySocket
    // because CloseProxySocket acquires ProxySocketManager::m_mutex, which could
    // cause a deadlock if the mutex order is reversed elsewhere.
    std::vector<s32> proxy_fds_to_close;
    {
        std::scoped_lock lock(g_socket_info_mutex);
        size_t cleaned = 0;
        for (auto it = g_socket_info.begin(); it != g_socket_info.end(); ) {
            if (it->second.session_id == m_session_id) {
                // Collect proxy sockets to close after releasing the lock
                if (it->second.is_proxy) {
                    proxy_fds_to_close.push_back(it->first);
                }
                it = g_socket_info.erase(it);
                cleaned++;
            } else {
                ++it;
            }
        }
        if (cleaned > 0) {
            LOG_INFO("[BSD#%u] DESTRUCTOR: cleaned up %zu orphaned socket entries", m_session_id, cleaned);
        }
    }

    // Now close proxy sockets without holding g_socket_info_mutex
    if (!proxy_fds_to_close.empty()) {
        auto& manager = ProxySocketManager::GetInstance();
        for (s32 fd : proxy_fds_to_close) {
            manager.CloseProxySocket(fd);
        }
    }

    // Decrement the session count for this PID
    bool was_last_session = false;
    {
        std::scoped_lock lock(g_mitm_pids_mutex);
        auto it = g_mitm_pid_count.find(m_client_pid);
        if (it != g_mitm_pid_count.end()) {
            if (it->second > 0) {
                it->second--;
            }
            if (it->second == 0) {
                g_mitm_pid_count.erase(it);
                was_last_session = true;
            }
        }
    }

    // FIX v3 (#44): If this was the last BSD session for this process,
    // clean up abandoned forward services immediately.
    // Previously we relied on CleanupAbandonedServices() being called
    // from ~ICommunicationService() / DisconnectFromServer(), but if
    // the game crashes before opening ldn:u (or never opens it), those
    // paths are never hit. Abandoned bsd:u handles accumulate and can
    // exhaust the global bsd:u session pool, causing the next game's
    // nn::socket::Initialize (and CreateTransferMemory) to fail.
    if (was_last_session) {
        LOG_INFO("[BSD#%u] Last BSD session for pid=%lu — cleaning up abandoned services",
                 m_session_id, m_client_pid);
        CleanupAbandonedServices();
    }

    // FIX v6-v7 (#44): Count active IPC sessions to detect game exit.
    // When the game dies, Atmosphere closes all ldn:u/bsd:u sessions.
    // The destructor runs for each session, decrementing the counter.
    // When it hits zero, no more sessions exist → the game is gone.
    auto& shared_state = ams::mitm::ldn::SharedState::GetInstance();
    u32 remaining = shared_state.DecrementSessionCount();
    LOG_INFO("[BSD#%u] Destructor: active sessions remaining = %u", m_session_id, remaining);

    if (remaining == 0) {
        LOG_INFO("[BSD#%u] Destructor: no active sessions — game closed, terminating sysmodule", m_session_id);
        (void)sm::mitm::UninstallMitm(sm::ServiceName::Encode("ldn:u"));
        (void)sm::mitm::UninstallMitm(sm::ServiceName::Encode("bsd:u"));
        ryu_ldn::debug::g_logger.flush();
        svc::ExitProcess();
    }
    ::Service* fwd = m_forward_service.get();
    Handle session_handle = fwd ? fwd->session : INVALID_HANDLE;
    LOG_INFO("[BSD#%u] DESTRUCTOR: pid=%lu, fwd_srv=%p (handle=0x%x), commands=%u, registered=%d",
             m_session_id, m_client_pid, fwd, session_handle, m_command_count, m_registered);

    // LAZY FORWARD SERVICE HANDLING:
    // If this session was never registered (never received RegisterClient),
    // we must NOT close the forward_service. Closing an unregistered bsd:u
    // session causes a system freeze. Instead, we move it to an "abandoned"
    // list where it stays alive until the game process exits.
    if (!m_registered) {
        if (m_forward_service) {
            LOG_WARN("[BSD#%u] Session never registered - moving forward_service to abandoned list to prevent freeze",
                     m_session_id);
            std::scoped_lock lock(g_abandoned_services_mutex);
            g_abandoned_forward_services.push_back(std::move(m_forward_service));
            LOG_INFO("[BSD#%u] Abandoned services count: %zu", m_session_id, g_abandoned_forward_services.size());
        }
    }
    // For registered sessions, m_forward_service will be closed normally
    // when the base class destructor runs (shared_ptr goes out of scope)
}

/**
 * @brief Determine if we should MITM a process's BSD calls
 *
 * Atmosphere calls this for each process that opens bsd:u.
 * If we return true, our MITM service handles all their BSD calls.
 *
 * ## Strategy
 *
 * We intercept BSD sessions from applications that are in the LDN
 * game whitelist (fetched from Ryujinx server at startup).
 *
 * @param client_info Process information (PID, program ID, etc.)
 * @return true For games in the whitelist, false otherwise
 */
bool BsdMitmService::ShouldMitm(const sm::MitmProcessInfo& client_info) {
    // Static counter to track all ShouldMitm calls for debugging
    static u32 s_call_count = 0;
    u32 call_id = ++s_call_count;

    LOG_INFO("BSD ShouldMitm #%u: ENTER pid=%lu, program_id=0x%016lx",
             call_id, client_info.process_id.value, client_info.program_id.value);

    // Our sysmodule's program_id - do not intercept ourselves
    constexpr u64 OUR_PROGRAM_ID = 0x4200000000000010ULL;

    // Skip our own sysmodule to avoid infinite recursion
    if (client_info.program_id.value == OUR_PROGRAM_ID) {
        LOG_INFO("BSD ShouldMitm #%u: SKIP (our sysmodule)", call_id);
        return false;
    }

    u64 program_id = client_info.program_id.value;

    // Skip non-applications (system services, applets, etc.)
    if (program_id < 0x0100000000000000ULL) {
        LOG_INFO("BSD ShouldMitm #%u: SKIP (system 0x%016lx)", call_id, program_id);
        return false;
    }

    // First check: is this game in the LDN whitelist?
    LOG_INFO("BSD ShouldMitm #%u: checking whitelist for 0x%016lx...", call_id, program_id);
    bool is_whitelisted = ryu_ldn::config::IsGameInWhitelist(program_id);
    LOG_INFO("BSD ShouldMitm #%u: whitelist result=%s", call_id, is_whitelisted ? "YES" : "NO");

    if (!is_whitelisted) {
        return false;
    }

    // Track session count and decide whether to intercept
    {
        std::scoped_lock lock(g_mitm_pids_mutex);
        u32& count = g_mitm_pid_count[client_info.process_id.value];
        count++;

        // Skip the first BSD session from each game process.
        // Games typically open a "dummy" first session that is never used
        // (no RegisterClient is ever called on it). Intercepting this session
        // causes system instability/crashes even if we try to handle it gracefully.
        // The actual networking happens on session #2+.
        if (count == 1) {
            LOG_INFO("BSD ShouldMitm #%u: SKIP first session for pid=%lu (dummy session)",
                     call_id, client_info.process_id.value);
            return false;
        }

        LOG_INFO("BSD ShouldMitm #%u: INTERCEPTING session #%u for pid=%lu",
                 call_id, count, client_info.process_id.value);
    }

    return true;
}

/**
 * @brief Clean up abandoned forward services
 *
 * Sessions that never received RegisterClient have their forward_service
 * moved to an abandoned list to prevent system freeze. This function
 * cleans up those services by closing them safely.
 *
 * Should be called when LDN disconnects or when the game process exits.
 */
void BsdMitmService::CleanupAbandonedServices() {
    std::scoped_lock lock(g_abandoned_services_mutex);

    if (g_abandoned_forward_services.empty()) {
        return;
    }

    LOG_INFO("BSD CleanupAbandonedServices: cleaning up %zu abandoned services",
             g_abandoned_forward_services.size());

    // Clear the vector - this will call serviceClose() on each service
    // We do this here (during LDN disconnect) rather than during session
    // destruction because it's safer to close these sessions when the
    // game is no longer actively using BSD.
    g_abandoned_forward_services.clear();

    LOG_INFO("BSD CleanupAbandonedServices: cleanup complete");
}

// =============================================================================
// Session Management Commands
// =============================================================================

/**
 * @brief Initialize BSD socket library for a client (Command 0)
 *
 * First command called by games to initialize the socket library.
 * This sets up buffer sizes and transfer memory for socket operations.
 *
 * ## IPC Interface (from switchbrew)
 *
 * ```
 * Input:
 *   [4] u32 config_size (should be 0x20 for LibraryConfigData)
 *   [0x21] Type-0x21 buffer (config data, auto-select)
 *   [0xA] Copy handle (transfer memory)
 *   ClientProcessId
 *
 * Output:
 *   [4] s32 errno (0 = success)
 * ```
 *
 * ## Transfer Memory
 *
 * The transfer_memory handle is a shared memory region used for socket
 * buffers. We must forward it to the real service as a copy handle.
 *
 * @param[out] out_errno BSD errno on failure (0 on success)
 * @param[in] config_size Size of config buffer (0x20 for LibraryConfigData)
 * @param[in] config LibraryConfigData with socket buffer settings
 * @param[in] client_pid Client's process ID
 * @param[in] transfer_memory Handle to transfer memory for buffers
 *
 * @return Result code from forwarding to real service
 */
Result BsdMitmService::RegisterClient(
    sf::Out<u64> out_result,
    const ryu_ldn::bsd::LibraryConfigData& config,
    const sf::ClientProcessId& client_pid,
    u64 tmem_size,
    sf::CopyHandle&& transfer_memory)
{
    m_command_count++;

    Handle tmem_handle = transfer_memory.GetOsHandle();
    LOG_INFO("[BSD#%u] RegisterClient ENTRY: cmd_count=%u, client_pid=%lu, tmem_size=%lu, tmem_handle=0x%x",
             m_session_id, m_command_count, client_pid.GetValue().value, tmem_size, tmem_handle);

    if (tmem_handle == INVALID_HANDLE) {
        LOG_ERROR("BSD RegisterClient: INVALID transfer memory handle!");
    }

    LOG_VERBOSE("BSD RegisterClient config: version=%u, tcp_tx=%u, tcp_rx=%u, udp_tx=%u, udp_rx=%u, sb_eff=%u",
                config.version, config.tcp_tx_buf_size, config.tcp_rx_buf_size,
                config.udp_tx_buf_size, config.udp_rx_buf_size, config.sb_efficiency);

    // According to libnx source (_bsdRegisterClient in bsd.c), format is:
    // - InRaw: [config 32 bytes][pid_placeholder 8 bytes = 0][tmem_sz 8 bytes] = 48 bytes
    // - CopyHandle: transfer_memory
    // - send_pid: true
    // - Output: u64 pid_out (not errno!)

    const struct {
        ryu_ldn::bsd::LibraryConfigData config;
        u64 pid_placeholder;  // Always 0
        u64 tmem_size;
    } forward_input = { config, 0, tmem_size };

    u64 pid_out = 0;

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 0, forward_input, pid_out,
        .in_send_pid = true,
        .in_num_handles = 1,
        .in_handles = { tmem_handle },
        .override_pid = m_client_pid,
    );

    LOG_INFO("[BSD#%u] RegisterClient: forward returned rc=0x%x, pid_out=0x%lx",
             m_session_id, rc.GetValue(), pid_out);

    // CRITICAL: Detach the handle to prevent sf::CopyHandle destructor from closing it.
    // The handle was forwarded to the real BSD service via serviceMitmDispatchInOut,
    // which creates a kernel-level copy. However, sf::CopyHandle is marked as "managed"
    // by Atmosphere's command serialization, meaning its destructor would close our
    // local handle. Since we're a MITM proxy, we must NOT close handles that belong
    // to the client process - we only forward them.
    transfer_memory.Detach();

    // Mark this session as registered
    if (R_SUCCEEDED(rc)) {
        m_registered = true;
    }

    // Return the pid_out value from real service
    out_result.SetValue(pid_out);
    LOG_INFO("[BSD#%u] RegisterClient EXIT: returning rc=0x%x, pid_out=0x%lx, registered=%d", m_session_id, rc.GetValue(), pid_out, m_registered);
    R_RETURN(rc);
}

/**
 * @brief Start socket monitoring (Command 1)
 *
 * Starts monitoring socket activity for a process.
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [8] u64 pid
 *
 * Output:
 *   [4] s32 errno
 * ```
 *
 * @param[out] out_errno BSD errno on failure
 * @param[in] pid Process ID to monitor
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::StartMonitoring(sf::Out<s32> out_errno, u64 pid) {
    m_command_count++;
    LOG_INFO("[BSD#%u] StartMonitoring ENTRY: cmd_count=%u, pid=%lu", m_session_id, m_command_count, pid);

    s32 errno_out = 0;
    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 1, pid, errno_out
    );

    out_errno.SetValue(errno_out);
    LOG_INFO("[BSD#%u] StartMonitoring EXIT: rc=0x%x, errno=%d", m_session_id, rc.GetValue(), errno_out);
    R_RETURN(rc);
}

// =============================================================================
// Socket Lifecycle Commands
// =============================================================================

/**
 * @brief Create a new socket (Command 2)
 *
 * Creates a new socket and returns its file descriptor.
 * This is the primary entry point for socket creation.
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 domain (AF_INET=2, AF_INET6=28)
 *   [4] s32 type (SOCK_STREAM=1, SOCK_DGRAM=2)
 *   [4] s32 protocol (0=auto, TCP=6, UDP=17)
 *
 * Output:
 *   [4] s32 errno
 *   [4] s32 fd
 * ```
 *
 * ## Socket Tracking (Future Story 8.4)
 *
 * After creating the socket, we'll register it in our tracking table.
 * The socket is initially marked as "normal" and only becomes an
 * LDN proxy socket when it binds/connects to 10.114.x.x.
 *
 * @param[out] out_errno BSD errno on failure (0 on success)
 * @param[out] out_fd File descriptor for the new socket
 * @param[in] domain Address family (AF_INET=2, AF_INET6=28)
 * @param[in] type Socket type (SOCK_STREAM=1, SOCK_DGRAM=2)
 * @param[in] protocol Protocol (0=default, TCP=6, UDP=17)
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Socket(
    sf::Out<s32> out_errno, sf::Out<s32> out_fd,
    s32 domain, s32 type, s32 protocol)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] Socket ENTRY: cmd_count=%u, domain=%d, type=%d, protocol=%d", m_session_id, m_command_count, domain, type, protocol);

    struct {
        s32 domain;
        s32 type;
        s32 protocol;
    } in = { domain, type, protocol };

    struct {
        s32 errno_val;
        s32 fd;
    } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 2, in, out
    );

    out_errno.SetValue(out.errno_val);
    out_fd.SetValue(out.fd);

    LOG_INFO("[BSD#%u] Socket result: rc=0x%x fd=%d errno=%d", m_session_id, rc.GetValue(), out.fd, out.errno_val);

    // Track socket info for later Bind/Connect calls
    if (R_SUCCEEDED(rc) && out.errno_val == 0 && out.fd >= 0) {
        // Determine protocol from type if not specified
        ryu_ldn::bsd::ProtocolType proto = static_cast<ryu_ldn::bsd::ProtocolType>(protocol);
        if (protocol == 0) {
            // Default protocol based on type
            if (type == static_cast<s32>(ryu_ldn::bsd::SocketType::Stream)) {
                proto = ryu_ldn::bsd::ProtocolType::Tcp;
            } else if (type == static_cast<s32>(ryu_ldn::bsd::SocketType::Dgram)) {
                proto = ryu_ldn::bsd::ProtocolType::Udp;
            }
        }

        {
            std::scoped_lock lock(g_socket_info_mutex);
            g_socket_info[out.fd] = SocketInfo{
                .type = static_cast<ryu_ldn::bsd::SocketType>(type),
                .protocol = proto,
                .is_proxy = false,
                .broadcast = false,
                .session_id = m_session_id,
            };
        }
        LOG_VERBOSE("BSD Socket tracked fd=%d type=%d proto=%d session=%u", out.fd, type, static_cast<s32>(proto), m_session_id);
    }

    R_RETURN(rc);
}

/**
 * @brief Create an exempt socket (Command 3)
 *
 * Creates a socket exempt from certain restrictions.
 * Same interface and tracking logic as Socket.
 *
 * ## IPC Interface
 *
 * Same as Socket (Command 2)
 *
 * @param[out] out_errno BSD errno
 * @param[out] out_fd File descriptor
 * @param[in] domain Address family
 * @param[in] type Socket type
 * @param[in] protocol Protocol
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::SocketExempt(
    sf::Out<s32> out_errno, sf::Out<s32> out_fd,
    s32 domain, s32 type, s32 protocol)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] SocketExempt ENTRY: cmd_count=%u, domain=%d, type=%d, protocol=%d", m_session_id, m_command_count, domain, type, protocol);

    struct {
        s32 domain;
        s32 type;
        s32 protocol;
    } in = { domain, type, protocol };

    struct {
        s32 errno_val;
        s32 fd;
    } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 3, in, out
    );

    out_errno.SetValue(out.errno_val);
    out_fd.SetValue(out.fd);

    // Track socket info (same logic as Socket)
    if (R_SUCCEEDED(rc) && out.errno_val == 0 && out.fd >= 0) {
        ryu_ldn::bsd::ProtocolType proto = static_cast<ryu_ldn::bsd::ProtocolType>(protocol);
        if (protocol == 0) {
            if (type == static_cast<s32>(ryu_ldn::bsd::SocketType::Stream)) {
                proto = ryu_ldn::bsd::ProtocolType::Tcp;
            } else if (type == static_cast<s32>(ryu_ldn::bsd::SocketType::Dgram)) {
                proto = ryu_ldn::bsd::ProtocolType::Udp;
            }
        }

        {
            std::scoped_lock lock(g_socket_info_mutex);
            g_socket_info[out.fd] = SocketInfo{
                .type = static_cast<ryu_ldn::bsd::SocketType>(type),
                .protocol = proto,
                .is_proxy = false,
                .broadcast = false,
                .session_id = m_session_id,
            };
        }
        LOG_VERBOSE("BSD SocketExempt tracked fd=%d type=%d proto=%d session=%u", out.fd, type, static_cast<s32>(proto), m_session_id);
    }

    R_RETURN(rc);
}

/**
 * @brief Open a device (Command 4)
 *
 * Opens a device file. Limited to /dev/bpf on Switch.
 * Not relevant for LDN proxy - just forward transparently.
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [0x5] Type-A buffer (path string)
 *
 * Output:
 *   [4] s32 errno
 *   [4] s32 fd
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[out] out_fd File descriptor
 * @param[in] path Device path
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Open(
    sf::Out<s32> out_errno, sf::Out<s32> out_fd,
    const sf::InBuffer& path)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] Open ENTRY: cmd_count=%u, path_size=%zu", m_session_id, m_command_count, path.GetSize());

    struct {
        s32 errno_val;
        s32 fd;
    } out = {};

    Result rc = serviceMitmDispatchOut(
        m_forward_service.get(), 4, out,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcMapAlias,
        },
        .buffers = {
            { path.GetPointer(), path.GetSize() },
        }
    );

    out_errno.SetValue(out.errno_val);
    out_fd.SetValue(out.fd);
    R_RETURN(rc);
}

/**
 * @brief Close a socket (Command 26)
 *
 * Closes a socket and releases its resources.
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *
 * Output:
 *   [4] s32 errno
 * ```
 *
 * ## Socket Cleanup (Future Story 8.4)
 *
 * When socket tracking is implemented, we'll remove the socket
 * from our tracking table before forwarding the close.
 *
 * @param[out] out_errno BSD errno
 * @param[in] fd File descriptor to close
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Close(
    sf::Out<s32> out_errno,
    s32 fd)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] Close ENTRY: cmd_count=%u, fd=%d", m_session_id, m_command_count, fd);

    // Check if this is a proxy socket
    {
        std::scoped_lock lock(g_socket_info_mutex);
        auto it = g_socket_info.find(fd);
        if (it != g_socket_info.end()) {
            if (it->second.is_proxy) {
                // Close the proxy socket
                auto& manager = ProxySocketManager::GetInstance();
                if (manager.CloseProxySocket(fd)) {
                    LOG_INFO("BSD Close fd=%d closed LDN proxy socket", fd);
                }
            }
            // Remove from tracking table
            g_socket_info.erase(it);
        }
    }

    // Forward close to real service (the fd still exists there)
    s32 errno_out = 0;
    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 26, fd, errno_out
    );

    out_errno.SetValue(errno_out);
    R_RETURN(rc);
}

/**
 * @brief Duplicate socket for another process (Command 27)
 *
 * Duplicates a socket for use by another process.
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *   [8] u64 target_pid
 *
 * Output:
 *   [4] s32 errno
 *   [4] s32 new_fd
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[out] out_fd New file descriptor
 * @param[in] fd Socket to duplicate
 * @param[in] target_pid Target process ID
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::DuplicateSocket(
    sf::Out<s32> out_errno, sf::Out<s32> out_fd,
    s32 fd, u64 target_pid)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] DuplicateSocket ENTRY: cmd_count=%u, fd=%d, target_pid=%lu", m_session_id, m_command_count, fd, target_pid);

    struct {
        s32 fd;
        u32 _pad;
        u64 target_pid;
    } in = { fd, 0, target_pid };

    struct {
        s32 errno_val;
        s32 new_fd;
    } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 27, in, out
    );

    out_errno.SetValue(out.errno_val);
    out_fd.SetValue(out.new_fd);
    R_RETURN(rc);
}

/**
 * @brief Initialize BSD socket library (shared memory variant) (Command 33)
 *
 * Same as RegisterClient but the work-buffer is allocated in the sysmodule's
 * memory instead of using TransferMemory from the client. Available from 10.0.0+.
 *
 * ## IPC Interface (from switchbrew)
 *
 * Same input/output as RegisterClient except this doesn't take an input handle.
 * Output: s32 bsd_errno (0 on success)
 *
 * @param[out] out_errno BSD errno on failure (0 on success)
 * @param[in] config LibraryConfigData with socket buffer settings
 * @param[in] client_pid Client's process ID
 * @param[in] tmem_size Size for work buffer
 *
 * @return Result code from forwarding to real service
 */
Result BsdMitmService::RegisterClientShared(
    sf::Out<u64> out_result,
    const ryu_ldn::bsd::LibraryConfigData& config,
    const sf::ClientProcessId& client_pid,
    u64 tmem_size)
{
    m_command_count++;

    LOG_INFO("[BSD#%u] RegisterClientShared ENTRY: cmd_count=%u, client_pid=%lu, tmem_size=%lu",
             m_session_id, m_command_count, client_pid.GetValue().value, tmem_size);

    LOG_VERBOSE("BSD RegisterClientShared config: version=%u, tcp_tx=%u, tcp_rx=%u, udp_tx=%u, udp_rx=%u, sb_eff=%u",
                config.version, config.tcp_tx_buf_size, config.tcp_rx_buf_size,
                config.udp_tx_buf_size, config.udp_rx_buf_size, config.sb_efficiency);

    // Same format as RegisterClient but without the CopyHandle.
    // InRaw: [config 32 bytes][pid_placeholder 8 bytes = 0][tmem_size 8 bytes] = 48 bytes
    const struct {
        ryu_ldn::bsd::LibraryConfigData config;
        u64 pid_placeholder;
        u64 tmem_size;
    } forward_input = { config, 0, tmem_size };

    u64 pid_out = 0;

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 33, forward_input, pid_out,
        .in_send_pid = true,
        .override_pid = m_client_pid,
    );

    LOG_INFO("[BSD#%u] RegisterClientShared: forward returned rc=0x%x, pid_out=0x%lx",
             m_session_id, rc.GetValue(), pid_out);

    // Mark this session as registered
    if (R_SUCCEEDED(rc)) {
        m_registered = true;
    }

    out_result.SetValue(pid_out);
    R_RETURN(rc);
}

// =============================================================================
// Commands 28-32 (Resource Statistics and Multi-Message)
// =============================================================================

Result BsdMitmService::GetResourceStatistics(
    sf::Out<s32> out_errno,
    sf::OutBuffer out_stats,
    u64 pid)
{
    m_command_count++;
    LOG_INFO("BSD GetResourceStatistics: pid=%lu", pid);

    s32 errno_out = 0;
    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 28, pid, errno_out,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { out_stats.GetPointer(), out_stats.GetSize() } },
    );

    out_errno.SetValue(errno_out);
    R_RETURN(rc);
}

Result BsdMitmService::RecvMMsg(
    sf::Out<s32> out_errno, sf::Out<s32> out_count,
    s32 fd, s32 vlen, s32 flags, s32 timeout,
    sf::OutAutoSelectBuffer out_data)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] RecvMMsg ENTRY: cmd_count=%u, fd=%d, vlen=%d, flags=%d, timeout=%d",
             m_session_id, m_command_count, fd, vlen, flags, timeout);

    // Check if this is a proxy socket
    bool is_proxy = false;
    {
        std::scoped_lock lock(g_socket_info_mutex);
        auto it = g_socket_info.find(fd);
        if (it != g_socket_info.end() && it->second.is_proxy) {
            is_proxy = true;
        }
    }

    if (is_proxy) {
        // RecvMMsg on proxy sockets is complex due to the mmsghdr format.
        // For now, log a warning - if this is hit, we need to investigate the game.
        LOG_WARN("BSD RecvMMsg on proxy socket fd=%d - multi-message not fully supported, forwarding anyway", fd);
        // Fall through to forward to real service
    }

    // Forward to real service
    struct { s32 fd; s32 vlen; s32 flags; s32 timeout; } in = { fd, vlen, flags, timeout };
    struct { s32 errno_val; s32 count; } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 29, in, out,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect },
        .buffers = { { out_data.GetPointer(), out_data.GetSize() } },
    );

    out_errno.SetValue(out.errno_val);
    out_count.SetValue(out.count);
    R_RETURN(rc);
}

Result BsdMitmService::SendMMsg(
    sf::Out<s32> out_errno, sf::Out<s32> out_count,
    s32 fd, s32 vlen, s32 flags,
    const sf::InAutoSelectBuffer& in_data)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] SendMMsg ENTRY: cmd_count=%u, fd=%d, vlen=%d, flags=%d, data_size=%zu",
             m_session_id, m_command_count, fd, vlen, flags, in_data.GetSize());

    // Check if this is a proxy socket
    bool is_proxy = false;
    {
        std::scoped_lock lock(g_socket_info_mutex);
        auto it = g_socket_info.find(fd);
        if (it != g_socket_info.end() && it->second.is_proxy) {
            is_proxy = true;
        }
    }

    if (is_proxy) {
        // SendMMsg on proxy sockets is complex due to the mmsghdr format.
        // For now, log a warning - if this is hit, we need to investigate the game.
        LOG_WARN("BSD SendMMsg on proxy socket fd=%d - multi-message not fully supported, forwarding anyway", fd);
        // Fall through to forward to real service - this will likely fail for LDN addresses
        // but at least we'll see this in the logs
    }

    // Forward to real service
    struct { s32 fd; s32 vlen; s32 flags; } in = { fd, vlen, flags };
    struct { s32 errno_val; s32 count; } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 30, in, out,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcAutoSelect },
        .buffers = { { const_cast<void*>(static_cast<const void*>(in_data.GetPointer())), in_data.GetSize() } },
    );

    out_errno.SetValue(out.errno_val);
    out_count.SetValue(out.count);
    R_RETURN(rc);
}

Result BsdMitmService::EventFd(
    sf::Out<s32> out_errno, sf::Out<s32> out_fd,
    u64 initval, s32 flags)
{
    m_command_count++;
    LOG_INFO("BSD EventFd: initval=%lu, flags=%d", initval, flags);

    struct { u64 initval; s32 flags; u32 _pad; } in = { initval, flags, 0 };
    struct { s32 errno_val; s32 fd; } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 31, in, out
    );

    out_errno.SetValue(out.errno_val);
    out_fd.SetValue(out.fd);
    R_RETURN(rc);
}

Result BsdMitmService::RegisterResourceStatisticsName(
    sf::Out<s32> out_errno,
    u64 pid,
    const sf::InBuffer& name)
{
    m_command_count++;
    LOG_INFO("BSD RegisterResourceStatisticsName: pid=%lu", pid);

    s32 errno_out = 0;
    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 32, pid, errno_out,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcMapAlias },
        .buffers = { { const_cast<void*>(static_cast<const void*>(name.GetPointer())), name.GetSize() } },
    );

    out_errno.SetValue(errno_out);
    R_RETURN(rc);
}

// =============================================================================
// Address Operations (LDN Detection Points)
// =============================================================================

/**
 * @brief Bind socket to local address (Command 13)
 *
 * Binds a socket to a local address for listening or sending.
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *   [0x21] Type-0x21 buffer (sockaddr, auto-select)
 *
 * Output:
 *   [4] s32 errno
 * ```
 *
 * ## LDN Detection (Future Story 8.5)
 *
 * If the address is in 10.114.0.0/16, this socket becomes an LDN proxy:
 * - Mark socket as LDN in tracking table
 * - Store local address for later use
 * - Return success WITHOUT calling real bind (virtual binding)
 *
 * @param[out] out_errno BSD errno
 * @param[in] fd Socket file descriptor
 * @param[in] addr Address to bind (SockAddrIn for IPv4)
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Bind(
    sf::Out<s32> out_errno,
    s32 fd,
    const sf::InAutoSelectBuffer& addr)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] Bind ENTRY: cmd_count=%u, fd=%d, addr_size=%zu", m_session_id, m_command_count, fd, addr.GetSize());

    // Check if this is an LDN address (10.114.x.x)
    // Copy to aligned stack buffer — IPC pointer may be misaligned,
    // and SockAddrIn::IsLdnAddress() does __builtin_bswap32 which faults on ARM64.
    if (addr.GetSize() >= sizeof(ryu_ldn::bsd::SockAddrIn) && addr.GetPointer() != nullptr) {
        ryu_ldn::bsd::SockAddrIn sock_addr_copy;
        __builtin_memcpy(&sock_addr_copy, addr.GetPointer(), sizeof(sock_addr_copy));

        // Log the bind address for debugging
        LOG_INFO("[BSD#%u] Bind fd=%d address: family=%u, addr=0x%08x (%u.%u.%u.%u), port=%u",
                 m_session_id, fd,
                 sock_addr_copy.sin_family,
                 sock_addr_copy.sin_addr,
                 (sock_addr_copy.sin_addr >> 0) & 0xFF,
                 (sock_addr_copy.sin_addr >> 8) & 0xFF,
                 (sock_addr_copy.sin_addr >> 16) & 0xFF,
                 (sock_addr_copy.sin_addr >> 24) & 0xFF,
                 sock_addr_copy.GetPort());

        // Check address family is IPv4
        if (sock_addr_copy.sin_family == static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet)) {
            // Check if this is an LDN address OR INADDR_ANY (0.0.0.0)
            // Games often bind to 0.0.0.0 to accept from any interface - we need to proxy these too
            bool is_ldn = sock_addr_copy.IsLdnAddress();
            bool is_inaddr_any = (sock_addr_copy.sin_addr == 0);

            if (is_ldn || is_inaddr_any) {
                LOG_INFO("BSD Bind fd=%d detected %s address, port=%u",
                         fd,
                         is_ldn ? "LDN" : "INADDR_ANY",
                         sock_addr_copy.GetPort());

                // Get socket info (type and protocol) under lock
                SocketInfo socket_info;
                bool found = false;
                {
                    std::scoped_lock lock(g_socket_info_mutex);
                    auto it = g_socket_info.find(fd);
                    if (it != g_socket_info.end()) {
                        socket_info = it->second;
                        found = true;
                    }
                }

                if (!found) {
                    LOG_WARN("BSD Bind fd=%d not tracked, forwarding to real service", fd);
                    // Fall through to normal bind
                } else {
                    // Create proxy socket
                    auto& manager = ProxySocketManager::GetInstance();
                    ProxySocket* proxy = manager.CreateProxySocket(fd, socket_info.type, socket_info.protocol);

                    if (proxy != nullptr) {
                        // Apply saved socket options
                        if (socket_info.broadcast) {
                            proxy->SetBroadcastEnabled(true);
                            LOG_INFO("BSD Bind fd=%d: applied saved SO_BROADCAST=1", fd);
                        }
                        // Build bind address - use local LDN IP for INADDR_ANY
                        ryu_ldn::bsd::SockAddrIn bind_addr = sock_addr_copy;
                        if (is_inaddr_any) {
                            // Replace 0.0.0.0 with our local LDN IP
                            // CRITICAL: Do NOT bswap32 - must match Ryujinx/NetworkInfo format
                            uint32_t local_ip = manager.GetLocalIp();
                            if (local_ip == 0) {
                                // Local IP not set yet - use 0.0.0.0 which will match any dest
                                LOG_WARN("BSD Bind fd=%d: local LDN IP not set, using INADDR_ANY", fd);
                            } else {
                                bind_addr.sin_addr = local_ip;  // NO bswap32 - Ryujinx format
                                LOG_INFO("BSD Bind fd=%d: using local LDN IP 0x%08x", fd, local_ip);
                            }
                        }

                        // Handle ephemeral port (port 0)
                        if (bind_addr.GetPort() == 0) {
                            uint16_t ephemeral = manager.AllocatePort(socket_info.protocol);
                            if (ephemeral == 0) {
                                LOG_ERROR("BSD Bind fd=%d failed to allocate ephemeral port", fd);
                                out_errno.SetValue(static_cast<s32>(ryu_ldn::bsd::BsdErrno::AddrInUse));
                                R_SUCCEED();
                            }
                            bind_addr.sin_port = __builtin_bswap16(ephemeral);
                            LOG_VERBOSE("BSD Bind fd=%d allocated ephemeral port %u", fd, ephemeral);
                        } else {
                            // Reserve the specific port
                            if (!manager.ReservePort(bind_addr.GetPort(), socket_info.protocol)) {
                                LOG_WARN("BSD Bind fd=%d port %u already in use", fd, bind_addr.GetPort());
                                out_errno.SetValue(static_cast<s32>(ryu_ldn::bsd::BsdErrno::AddrInUse));
                                R_SUCCEED();
                            }
                        }

                        // Bind the proxy socket
                        Result bind_result = proxy->Bind(bind_addr);
                        if (R_FAILED(bind_result)) {
                            LOG_ERROR("BSD Bind fd=%d proxy bind failed: 0x%x", fd, bind_result.GetValue());
                            out_errno.SetValue(bind_result.GetValue());
                            R_SUCCEED();
                        }

                        // Mark as proxy socket
                        {
                            std::scoped_lock lock(g_socket_info_mutex);
                            auto it = g_socket_info.find(fd);
                            if (it != g_socket_info.end()) {
                                it->second.is_proxy = true;
                            }
                        }

                        LOG_INFO("BSD Bind fd=%d successfully bound to LDN proxy (addr=0x%08x, port=%u)",
                                 fd, bind_addr.GetAddr(), bind_addr.GetPort());

                        // Deliver any pending packets that arrived before bind
                        manager.DeliverPendingPackets(proxy, bind_addr.GetPort(), socket_info.protocol);

                        // For INADDR_ANY binds, also forward to real service
                        // so the game can also use non-LDN addresses
                        if (is_inaddr_any) {
                            s32 real_errno = 0;
                            Result real_rc = serviceMitmDispatchInOut(
                                m_forward_service.get(), 13, fd, real_errno,
                                .buffer_attrs = {
                                    SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
                                },
                                .buffers = {
                                    { addr.GetPointer(), addr.GetSize() },
                                }
                            );
                            if (R_FAILED(real_rc) || real_errno != 0) {
                                LOG_WARN("BSD Bind fd=%d real bind failed (rc=0x%x, errno=%d), proxy-only mode",
                                         fd, real_rc.GetValue(), real_errno);
                            }
                        }

                        out_errno.SetValue(0);
                        R_SUCCEED();
                    } else {
                        LOG_ERROR("BSD Bind fd=%d failed to create proxy socket", fd);
                        out_errno.SetValue(static_cast<s32>(ryu_ldn::bsd::BsdErrno::NoMem));
                        R_SUCCEED();
                    }
                }
            }
        }
    }

    // Not LDN address - forward to real service
    s32 errno_out = 0;
    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 13, fd, errno_out,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { addr.GetPointer(), addr.GetSize() },
        }
    );

    out_errno.SetValue(errno_out);
    R_RETURN(rc);
}

/**
 * @brief Connect socket to remote address (Command 14)
 *
 * Connects a socket to a remote address for communication.
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *   [0x21] Type-0x21 buffer (sockaddr, auto-select)
 *
 * Output:
 *   [4] s32 errno
 * ```
 *
 * ## LDN Detection (Future Story 8.5)
 *
 * If the address is in 10.114.0.0/16, this socket becomes an LDN proxy:
 * - Mark socket as LDN in tracking table
 * - Store peer address for ProxyData packets
 * - Return success WITHOUT calling real connect (virtual connection)
 *
 * @param[out] out_errno BSD errno
 * @param[in] fd Socket file descriptor
 * @param[in] addr Remote address to connect to
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Connect(
    sf::Out<s32> out_errno,
    s32 fd,
    const sf::InAutoSelectBuffer& addr)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] Connect ENTRY: cmd_count=%u, fd=%d, addr_size=%zu", m_session_id, m_command_count, fd, addr.GetSize());

    // Check if this is an LDN address (10.114.x.x)
    // Copy to aligned stack buffer — IPC pointer may be misaligned,
    // and SockAddrIn::IsLdnAddress() does __builtin_bswap32 which faults on ARM64.
    if (addr.GetSize() >= sizeof(ryu_ldn::bsd::SockAddrIn) && addr.GetPointer() != nullptr) {
        ryu_ldn::bsd::SockAddrIn sock_addr_copy;
        __builtin_memcpy(&sock_addr_copy, addr.GetPointer(), sizeof(sock_addr_copy));

        // Check address family is IPv4 and address is LDN
        if (sock_addr_copy.sin_family == static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet) &&
            sock_addr_copy.IsLdnAddress())
        {
            LOG_INFO("BSD Connect fd=%d detected LDN address %u.%u.%u.%u:%u",
                     fd,
                     (sock_addr_copy.sin_addr >> 0) & 0xFF,
                     (sock_addr_copy.sin_addr >> 8) & 0xFF,
                     (sock_addr_copy.sin_addr >> 16) & 0xFF,
                     (sock_addr_copy.sin_addr >> 24) & 0xFF,
                     sock_addr_copy.GetPort());

            // Get socket info (type and protocol) under lock
            SocketInfo socket_info;
            bool found = false;
            {
                std::scoped_lock lock(g_socket_info_mutex);
                auto it = g_socket_info.find(fd);
                if (it != g_socket_info.end()) {
                    socket_info = it->second;
                    found = true;
                }
            }

            if (!found) {
                LOG_WARN("BSD Connect fd=%d not tracked, forwarding to real service", fd);
                // Fall through to normal connect
            } else {
                auto& manager = ProxySocketManager::GetInstance();

                // Check if we already have a proxy socket (from Bind)
                ProxySocket* proxy = manager.GetProxySocket(fd);

                // If not bound yet, create one and auto-bind
                if (proxy == nullptr) {
                    proxy = manager.CreateProxySocket(fd, socket_info.type, socket_info.protocol);
                    if (proxy == nullptr) {
                        LOG_ERROR("BSD Connect fd=%d failed to create proxy socket", fd);
                        out_errno.SetValue(static_cast<s32>(ryu_ldn::bsd::BsdErrno::NoMem));
                        R_SUCCEED();
                    }

                    // Auto-bind with ephemeral port and local LDN IP
                    uint16_t ephemeral = manager.AllocatePort(socket_info.protocol);
                    if (ephemeral == 0) {
                        LOG_ERROR("BSD Connect fd=%d failed to allocate ephemeral port", fd);
                        out_errno.SetValue(static_cast<s32>(ryu_ldn::bsd::BsdErrno::AddrInUse));
                        R_SUCCEED();
                    }

                    // Create local address using our LDN IP
                    // CRITICAL: Do NOT bswap32 - must match Ryujinx/NetworkInfo format
                    ryu_ldn::bsd::SockAddrIn local_addr{};
                    local_addr.sin_len = sizeof(local_addr);
                    local_addr.sin_family = static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet);
                    local_addr.sin_port = __builtin_bswap16(ephemeral);
                    local_addr.sin_addr = manager.GetLocalIp();  // NO bswap32 - Ryujinx format

                    Result bind_result = proxy->Bind(local_addr);
                    if (R_FAILED(bind_result)) {
                        LOG_ERROR("BSD Connect fd=%d auto-bind failed: 0x%x", fd, bind_result.GetValue());
                        manager.ReleasePort(ephemeral, socket_info.protocol);
                        out_errno.SetValue(bind_result.GetValue());
                        R_SUCCEED();
                    }

                    LOG_VERBOSE("BSD Connect fd=%d auto-bound to port %u", fd, ephemeral);
                }

                // Connect the proxy socket to the remote address
                Result connect_result = proxy->Connect(sock_addr_copy);
                if (R_FAILED(connect_result)) {
                    LOG_ERROR("BSD Connect fd=%d proxy connect failed: 0x%x", fd, connect_result.GetValue());
                    out_errno.SetValue(connect_result.GetValue());
                    R_SUCCEED();
                }

                // Mark as proxy socket
                {
                    std::scoped_lock lock(g_socket_info_mutex);
                    auto it = g_socket_info.find(fd);
                    if (it != g_socket_info.end()) {
                        it->second.is_proxy = true;
                    }
                }

                LOG_INFO("BSD Connect fd=%d successfully connected to LDN proxy", fd);
                out_errno.SetValue(0);
                R_SUCCEED();
            }
        }
    }

    // Not LDN address - forward to real service
    s32 errno_out = 0;
    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 14, fd, errno_out,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { addr.GetPointer(), addr.GetSize() },
        }
    );

    out_errno.SetValue(errno_out);
    R_RETURN(rc);
}

/**
 * @brief Accept incoming connection (Command 12)
 *
 * Accepts a connection on a listening socket.
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *
 * Output:
 *   [4] s32 errno
 *   [4] s32 new_fd
 *   [0x22] Type-0x22 buffer (sockaddr, auto-select)
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[out] out_fd New socket for accepted connection
 * @param[in] fd Listening socket
 * @param[out] addr_out Remote address of connector
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Accept(
    sf::Out<s32> out_errno, sf::Out<s32> out_fd,
    s32 fd,
    sf::OutAutoSelectBuffer addr_out)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] Accept ENTRY: cmd_count=%u, fd=%d", m_session_id, m_command_count, fd);

    struct {
        s32 errno_val;
        s32 new_fd;
    } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 12, fd, out,
        .buffer_attrs = {
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { addr_out.GetPointer(), addr_out.GetSize() },
        }
    );

    out_errno.SetValue(out.errno_val);
    out_fd.SetValue(out.new_fd);
    R_RETURN(rc);
}

/**
 * @brief Get peer address (Command 15)
 *
 * Gets the address of the connected peer.
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *
 * Output:
 *   [4] s32 errno
 *   [0x22] Type-0x22 buffer (sockaddr, auto-select)
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[in] fd Socket file descriptor
 * @param[out] addr_out Peer address buffer
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::GetPeerName(
    sf::Out<s32> out_errno,
    s32 fd,
    sf::OutAutoSelectBuffer addr_out)
{
    m_command_count++;
    LOG_VERBOSE("[BSD#%u] GetPeerName: cmd_count=%u, fd=%d", m_session_id, m_command_count, fd);

    // Check if this is a proxy socket
    auto& manager = ProxySocketManager::GetInstance();
    ProxySocket* proxy = manager.GetProxySocket(fd);
    if (proxy != nullptr) {
        // Return the stored LDN peer address
        const auto& peer_addr = proxy->GetRemoteAddr();

        // Check if connected
        if (proxy->GetState() != ProxySocketState::Connected) {
            out_errno.SetValue(static_cast<s32>(ryu_ldn::bsd::BsdErrno::NotConn));
            R_SUCCEED();
        }

        // Copy to output buffer
        if (addr_out.GetSize() >= sizeof(ryu_ldn::bsd::SockAddrIn)) {
            std::memcpy(addr_out.GetPointer(), &peer_addr, sizeof(ryu_ldn::bsd::SockAddrIn));
        }

        out_errno.SetValue(0);
        LOG_INFO("BSD GetPeerName fd=%d -> LDN proxy peer %08x:%d",
                  fd, peer_addr.GetAddr(), peer_addr.GetPort());
        R_SUCCEED();
    }

    // Forward to real BSD service for non-proxy sockets
    s32 errno_out = 0;
    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 15, fd, errno_out,
        .buffer_attrs = {
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { addr_out.GetPointer(), addr_out.GetSize() },
        }
    );

    out_errno.SetValue(errno_out);
    R_RETURN(rc);
}

/**
 * @brief Get local address (Command 16)
 *
 * Gets the local address of a socket.
 *
 * ## IPC Interface
 *
 * Same as GetPeerName
 *
 * @param[out] out_errno BSD errno
 * @param[in] fd Socket file descriptor
 * @param[out] addr_out Local address buffer
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::GetSockName(
    sf::Out<s32> out_errno,
    s32 fd,
    sf::OutAutoSelectBuffer addr_out)
{
    m_command_count++;
    LOG_VERBOSE("[BSD#%u] GetSockName: cmd_count=%u, fd=%d", m_session_id, m_command_count, fd);

    // Check if this is a proxy socket
    auto& manager = ProxySocketManager::GetInstance();
    ProxySocket* proxy = manager.GetProxySocket(fd);
    if (proxy != nullptr) {
        // Return the stored LDN local address
        const auto& local_addr = proxy->GetLocalAddr();

        // Copy to output buffer
        if (addr_out.GetSize() >= sizeof(ryu_ldn::bsd::SockAddrIn)) {
            std::memcpy(addr_out.GetPointer(), &local_addr, sizeof(ryu_ldn::bsd::SockAddrIn));
        }

        out_errno.SetValue(0);
        LOG_INFO("BSD GetSockName fd=%d -> LDN proxy local %08x:%d",
                  fd, local_addr.GetAddr(), local_addr.GetPort());
        R_SUCCEED();
    }

    // Forward to real BSD service for non-proxy sockets
    s32 errno_out = 0;
    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 16, fd, errno_out,
        .buffer_attrs = {
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { addr_out.GetPointer(), addr_out.GetSize() },
        }
    );

    out_errno.SetValue(errno_out);
    R_RETURN(rc);
}

// =============================================================================
// Data Transfer Commands (LDN Proxy Points)
// =============================================================================

/**
 * @brief Send data to connected socket (Command 10)
 *
 * Sends data to a connected socket.
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *   [4] s32 flags
 *   [0x21] Type-0x21 buffer (data, auto-select)
 *
 * Output:
 *   [4] s32 errno
 *   [4] s32 size (bytes sent)
 * ```
 *
 * ## LDN Proxy (Future Story 8.6)
 *
 * For LDN sockets, instead of sending to real network:
 * 1. Wrap data in ProxyData packet with addresses
 * 2. Send via RyuLdn server connection
 * 3. Return data size as if sent
 *
 * @param[out] out_errno BSD errno
 * @param[out] out_size Bytes sent
 * @param[in] fd Socket file descriptor
 * @param[in] flags Send flags (MSG_DONTWAIT, etc.)
 * @param[in] buffer Data to send
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Send(
    sf::Out<s32> out_errno, sf::Out<s32> out_size,
    s32 fd, s32 flags,
    const sf::InAutoSelectBuffer& buffer)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] Send ENTRY: cmd_count=%u, fd=%d, flags=%d, size=%zu", m_session_id, m_command_count, fd, flags, buffer.GetSize());

    // Check if this is a proxy socket
    bool is_proxy = false;
    {
        std::scoped_lock lock(g_socket_info_mutex);
        auto it = g_socket_info.find(fd);
        if (it != g_socket_info.end() && it->second.is_proxy) {
            is_proxy = true;
        }
    }

    if (is_proxy) {
        auto& manager = ProxySocketManager::GetInstance();
        ProxySocket* proxy = manager.GetProxySocket(fd);

        if (proxy != nullptr) {
            // Send via proxy socket
            s32 result = proxy->Send(buffer.GetPointer(), buffer.GetSize(), flags);

            if (result < 0) {
                // Negative result is -errno
                out_errno.SetValue(-result);
                out_size.SetValue(0);
            } else {
                out_errno.SetValue(0);
                out_size.SetValue(result);
            }

            LOG_VERBOSE("BSD Send fd=%d proxy sent %d bytes", fd, result);
            R_SUCCEED();
        }
    }

    // Not a proxy socket - forward to real service
    struct {
        s32 fd;
        s32 flags;
    } in = { fd, flags };

    struct {
        s32 errno_val;
        s32 size;
    } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 10, in, out,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { buffer.GetPointer(), buffer.GetSize() },
        }
    );

    out_errno.SetValue(out.errno_val);
    out_size.SetValue(out.size);
    R_RETURN(rc);
}

/**
 * @brief Send data to specific address (Command 11)
 *
 * Sends data to a specific address (used for UDP).
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *   [4] s32 flags
 *   [0x21] Type-0x21 buffer (data, auto-select)
 *   [0x21] Type-0x21 buffer (sockaddr, auto-select)
 *
 * Output:
 *   [4] s32 errno
 *   [4] s32 size
 * ```
 *
 * ## LDN Proxy (Future Story 8.6)
 *
 * If destination address is 10.114.x.x:
 * - Create ProxyData packet with src/dst addresses
 * - Send via RyuLdn server
 * - Return data size as if sent
 *
 * @param[out] out_errno BSD errno
 * @param[out] out_size Bytes sent
 * @param[in] fd Socket file descriptor
 * @param[in] flags Send flags
 * @param[in] buffer Data to send
 * @param[in] addr Destination address
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::SendTo(
    sf::Out<s32> out_errno, sf::Out<s32> out_size,
    s32 fd, s32 flags,
    const sf::InAutoSelectBuffer& buffer,
    const sf::InAutoSelectBuffer& addr)
{
    m_command_count++;
    LOG_VERBOSE("[BSD#%u] SendTo ENTRY: cmd_count=%u, fd=%d, flags=%d, size=%zu",
                m_session_id, m_command_count, fd, flags, buffer.GetSize());

    // Check if this is a proxy socket or if dest is LDN
    bool is_proxy = false;
    SocketInfo socket_info;
    bool found = false;

    {
        std::scoped_lock lock(g_socket_info_mutex);
        auto it = g_socket_info.find(fd);
        if (it != g_socket_info.end()) {
            socket_info = it->second;
            found = true;
            is_proxy = it->second.is_proxy;
        }
    }

    // Also check destination address for LDN.
    // Copy to stack-aligned buffer before accessing — the IPC buffer pointer
    // may be misaligned, and SockAddrIn::IsLdnAddress() does
    // __builtin_bswap32 on sin_addr which faults on ARM64 if unaligned.
    if (addr.GetSize() >= sizeof(ryu_ldn::bsd::SockAddrIn) && addr.GetPointer() != nullptr) {
        ryu_ldn::bsd::SockAddrIn dest_addr_copy;
        __builtin_memcpy(&dest_addr_copy, addr.GetPointer(), sizeof(dest_addr_copy));

        if (dest_addr_copy.sin_family == static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet) &&
            dest_addr_copy.IsLdnAddress())
        {
            // LDN destination - use proxy
            is_proxy = true;

            // If socket not yet marked as proxy, mark it now
            if (found && !socket_info.is_proxy) {
                auto& manager = ProxySocketManager::GetInstance();

                // Create proxy socket if needed
                ProxySocket* proxy = manager.GetProxySocket(fd);
                if (proxy == nullptr) {
                    proxy = manager.CreateProxySocket(fd, socket_info.type, socket_info.protocol);
                    if (proxy != nullptr) {
                        // Auto-bind
                        // CRITICAL: Do NOT bswap32 - must match Ryujinx/NetworkInfo format
                        uint16_t ephemeral = manager.AllocatePort(socket_info.protocol);
                        if (ephemeral != 0) {
                            ryu_ldn::bsd::SockAddrIn local_addr{};
                            local_addr.sin_len = sizeof(local_addr);
                            local_addr.sin_family = static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet);
                            local_addr.sin_port = __builtin_bswap16(ephemeral);
                            local_addr.sin_addr = manager.GetLocalIp();  // NO bswap32 - Ryujinx format
                            R_TRY(proxy->Bind(local_addr));
                        }
                    }
                }

                // Mark as proxy
                {
                    std::scoped_lock lock(g_socket_info_mutex);
                    auto it = g_socket_info.find(fd);
                    if (it != g_socket_info.end()) {
                        it->second.is_proxy = true;
                    }
                }
            }
        }
    }

    if (is_proxy) {
        auto& manager = ProxySocketManager::GetInstance();
        ProxySocket* proxy = manager.GetProxySocket(fd);

        if (proxy != nullptr && addr.GetSize() >= sizeof(ryu_ldn::bsd::SockAddrIn) && addr.GetPointer() != nullptr) {
            // Copy to aligned stack buffer — IPC buffer pointer may be misaligned,
            // and dereferencing a packed struct via reinterpret_cast faults on ARM64
            // when IsLdnAddress() uses __builtin_bswap32 on sin_addr.
            ryu_ldn::bsd::SockAddrIn dest_addr_copy;
            __builtin_memcpy(&dest_addr_copy, addr.GetPointer(), sizeof(dest_addr_copy));

            // Send via proxy socket
            s32 result = proxy->SendTo(buffer.GetPointer(), buffer.GetSize(), flags, dest_addr_copy);

            if (result < 0) {
                out_errno.SetValue(-result);
                out_size.SetValue(0);
            } else {
                out_errno.SetValue(0);
                out_size.SetValue(result);
            }

            LOG_VERBOSE("BSD SendTo fd=%d proxy sent %d bytes", fd, result);
            R_SUCCEED();
        }

        // Socket is marked proxy but proxy unavailable — return NETUNREACH
        // instead of falling through to real BSD (which wouldn't know LDN addresses)
        out_errno.SetValue(static_cast<s32>(ryu_ldn::bsd::BsdErrno::NetUnreach));
        out_size.SetValue(0);
        R_SUCCEED();
    }

    // Not a proxy socket - forward to real service
    struct {
        s32 fd;
        s32 flags;
    } in = { fd, flags };

    struct {
        s32 errno_val;
        s32 size;
    } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 11, in, out,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { buffer.GetPointer(), buffer.GetSize() },
            { addr.GetPointer(), addr.GetSize() },
        }
    );

    out_errno.SetValue(out.errno_val);
    out_size.SetValue(out.size);
    R_RETURN(rc);
}

/**
 * @brief Receive data from connected socket (Command 8)
 *
 * Receives data from a connected socket.
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *   [4] s32 flags
 *
 * Output:
 *   [4] s32 errno
 *   [4] s32 size
 *   [0x22] Type-0x22 buffer (data, auto-select)
 * ```
 *
 * ## LDN Proxy (Future Story 8.6)
 *
 * For LDN sockets:
 * - Check ProxyData queue for incoming data
 * - If data available: copy to buffer, return size
 * - If non-blocking and no data: errno=EAGAIN
 * - If blocking: wait for data
 *
 * @param[out] out_errno BSD errno
 * @param[out] out_size Bytes received
 * @param[in] fd Socket file descriptor
 * @param[in] flags Recv flags
 * @param[out] buffer Buffer to receive into
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Recv(
    sf::Out<s32> out_errno, sf::Out<s32> out_size,
    s32 fd, s32 flags,
    sf::OutAutoSelectBuffer buffer)
{
    m_command_count++;
    LOG_VERBOSE("[BSD#%u] Recv: cmd_count=%u, fd=%d, flags=%d, buf_size=%zu", m_session_id, m_command_count, fd, flags, buffer.GetSize());

    // Check if this is a proxy socket
    bool is_proxy = false;
    {
        std::scoped_lock lock(g_socket_info_mutex);
        auto it = g_socket_info.find(fd);
        if (it != g_socket_info.end() && it->second.is_proxy) {
            is_proxy = true;
        }
    }

    if (is_proxy) {
        auto& manager = ProxySocketManager::GetInstance();
        ProxySocket* proxy = manager.GetProxySocket(fd);

        if (proxy != nullptr) {
            // Receive from proxy socket queue
            s32 result = proxy->Recv(buffer.GetPointer(), buffer.GetSize(), flags);

            if (result < 0) {
                // Negative result is -errno
                out_errno.SetValue(-result);
                out_size.SetValue(0);
            } else {
                out_errno.SetValue(0);
                out_size.SetValue(result);
            }

            LOG_VERBOSE("BSD Recv fd=%d proxy received %d bytes", fd, result);
            R_SUCCEED();
        }
    }

    // Not a proxy socket - forward to real service
    struct {
        s32 fd;
        s32 flags;
    } in = { fd, flags };

    struct {
        s32 errno_val;
        s32 size;
    } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 8, in, out,
        .buffer_attrs = {
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { buffer.GetPointer(), buffer.GetSize() },
        }
    );

    out_errno.SetValue(out.errno_val);
    out_size.SetValue(out.size);
    R_RETURN(rc);
}

/**
 * @brief Receive data with source address (Command 9)
 *
 * Receives data and the source address (used for UDP).
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *   [4] s32 flags
 *
 * Output:
 *   [4] s32 errno
 *   [4] s32 size
 *   [0x22] Type-0x22 buffer (data, auto-select)
 *   [0x22] Type-0x22 buffer (sockaddr, auto-select)
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[out] out_size Bytes received
 * @param[in] fd Socket file descriptor
 * @param[in] flags Recv flags
 * @param[out] buffer Data buffer
 * @param[out] addr_out Source address
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::RecvFrom(
    sf::Out<s32> out_ret, sf::Out<s32> out_errno, sf::Out<u32> out_addrlen,
    s32 fd, s32 flags,
    sf::OutAutoSelectBuffer buffer,
    sf::OutAutoSelectBuffer addr_out)
{
    m_command_count++;
    LOG_VERBOSE("[BSD#%u] RecvFrom: cmd_count=%u, fd=%d, flags=%d, buf_size=%zu", m_session_id, m_command_count, fd, flags, buffer.GetSize());

    // Check if this is a proxy socket
    bool is_proxy = false;
    {
        std::scoped_lock lock(g_socket_info_mutex);
        auto it = g_socket_info.find(fd);
        if (it != g_socket_info.end() && it->second.is_proxy) {
            is_proxy = true;
        }
    }

    if (is_proxy) {
        auto& manager = ProxySocketManager::GetInstance();
        ProxySocket* proxy = manager.GetProxySocket(fd);

        if (proxy != nullptr) {
            // Receive from proxy socket queue with source address
            ryu_ldn::bsd::SockAddrIn from_addr{};
            s32 result = proxy->RecvFrom(buffer.GetPointer(), buffer.GetSize(), flags, &from_addr);

            if (result < 0) {
                out_ret.SetValue(-1);
                out_errno.SetValue(-result);
                out_addrlen.SetValue(0);
            } else {
                out_ret.SetValue(result);
                out_errno.SetValue(0);

                u32 written_addr_len = 0;
                if (addr_out.GetSize() >= sizeof(ryu_ldn::bsd::SockAddrIn)) {
                    std::memcpy(addr_out.GetPointer(), &from_addr, sizeof(from_addr));
                    written_addr_len = sizeof(ryu_ldn::bsd::SockAddrIn);
                }
                out_addrlen.SetValue(written_addr_len);

                LOG_VERBOSE("BSD RecvFrom fd=%d proxy: %d bytes from %08x:%u (len=%u, family=%u)",
                            fd, result, from_addr.GetAddr(), from_addr.GetPort(),
                            from_addr.sin_len, from_addr.sin_family);
            }

            LOG_VERBOSE("BSD RecvFrom fd=%d proxy received %d bytes", fd, result);
            R_SUCCEED();
        }
    }

    // Not a proxy socket - forward to real service.
    // Wire layout per libnx bsdRecvFrom disasm: raw[0]=ret, raw[4]=errno, raw[8]=addrlen.
    struct {
        s32 fd;
        s32 flags;
    } in = { fd, flags };

    struct {
        s32 ret;
        s32 errno_val;
        u32 addrlen;
    } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 9, in, out,
        .buffer_attrs = {
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { buffer.GetPointer(), buffer.GetSize() },
            { addr_out.GetPointer(), addr_out.GetSize() },
        }
    );

    out_ret.SetValue(out.ret);
    out_errno.SetValue(out.errno_val);
    out_addrlen.SetValue(out.addrlen);
    R_RETURN(rc);
}

/**
 * @brief Write to socket (Command 24)
 *
 * Equivalent to Send with no flags.
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *   [0x21] Type-0x21 buffer (data, auto-select)
 *
 * Output:
 *   [4] s32 errno
 *   [4] s32 size
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[out] out_size Bytes written
 * @param[in] fd Socket file descriptor
 * @param[in] buffer Data to write
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Write(
    sf::Out<s32> out_errno, sf::Out<s32> out_size,
    s32 fd,
    const sf::InAutoSelectBuffer& buffer)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] Write ENTRY: cmd_count=%u, fd=%d, size=%zu", m_session_id, m_command_count, fd, buffer.GetSize());

    // Check if this is a proxy socket - treat same as Send()
    bool is_proxy = false;
    {
        std::scoped_lock lock(g_socket_info_mutex);
        auto it = g_socket_info.find(fd);
        if (it != g_socket_info.end() && it->second.is_proxy) {
            is_proxy = true;
        }
    }

    if (is_proxy) {
        auto& manager = ProxySocketManager::GetInstance();
        ProxySocket* proxy = manager.GetProxySocket(fd);

        if (proxy != nullptr) {
            // Write is equivalent to Send with no flags
            s32 result = proxy->Send(buffer.GetPointer(), buffer.GetSize(), 0);

            if (result < 0) {
                // Negative result is -errno
                out_errno.SetValue(-result);
                out_size.SetValue(0);
            } else {
                out_errno.SetValue(0);
                out_size.SetValue(result);
            }

            LOG_INFO("BSD Write fd=%d proxy sent %d bytes", fd, result);
            R_SUCCEED();
        }
    }

    // Not a proxy socket - forward to real service
    struct {
        s32 errno_val;
        s32 size;
    } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 24, fd, out,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { buffer.GetPointer(), buffer.GetSize() },
        }
    );

    out_errno.SetValue(out.errno_val);
    out_size.SetValue(out.size);
    R_RETURN(rc);
}

/**
 * @brief Read from socket (Command 25)
 *
 * Equivalent to Recv with no flags.
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *
 * Output:
 *   [4] s32 errno
 *   [4] s32 size
 *   [0x22] Type-0x22 buffer (data, auto-select)
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[out] out_size Bytes read
 * @param[in] fd Socket file descriptor
 * @param[out] buffer Buffer to read into
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Read(
    sf::Out<s32> out_errno, sf::Out<s32> out_size,
    s32 fd,
    sf::OutAutoSelectBuffer buffer)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] Read ENTRY: cmd_count=%u, fd=%d, buf_size=%zu", m_session_id, m_command_count, fd, buffer.GetSize());

    // Check if this is a proxy socket - treat same as Recv() with no flags
    bool is_proxy = false;
    {
        std::scoped_lock lock(g_socket_info_mutex);
        auto it = g_socket_info.find(fd);
        if (it != g_socket_info.end() && it->second.is_proxy) {
            is_proxy = true;
        }
    }

    if (is_proxy) {
        auto& manager = ProxySocketManager::GetInstance();
        ProxySocket* proxy = manager.GetProxySocket(fd);

        if (proxy != nullptr) {
            // Read is equivalent to Recv with no flags
            s32 result = proxy->Recv(buffer.GetPointer(), buffer.GetSize(), 0);

            if (result < 0) {
                // Negative result is -errno
                out_errno.SetValue(-result);
                out_size.SetValue(0);
            } else {
                out_errno.SetValue(0);
                out_size.SetValue(result);
            }

            LOG_INFO("BSD Read fd=%d proxy received %d bytes", fd, result);
            R_SUCCEED();
        }
    }

    // Not a proxy socket - forward to real service
    struct {
        s32 errno_val;
        s32 size;
    } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 25, fd, out,
        .buffer_attrs = {
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { buffer.GetPointer(), buffer.GetSize() },
        }
    );

    out_errno.SetValue(out.errno_val);
    out_size.SetValue(out.size);
    R_RETURN(rc);
}

// =============================================================================
// Socket Control Commands
// =============================================================================

/**
 * @brief Wait for socket activity - select (Command 5)
 *
 * Waits for activity on multiple sockets.
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 nfds
 *   [0x21] Type-0x21 buffer (readfds, auto-select)
 *   [0x21] Type-0x21 buffer (writefds, auto-select)
 *   [0x21] Type-0x21 buffer (errorfds, auto-select)
 *   [0x21] Type-0x21 buffer (timeout, auto-select)
 *
 * Output:
 *   [4] s32 errno
 *   [4] s32 count
 *   [0x22] Type-0x22 buffer (readfds, auto-select)
 *   [0x22] Type-0x22 buffer (writefds, auto-select)
 *   [0x22] Type-0x22 buffer (errorfds, auto-select)
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[out] out_count Number of ready sockets
 * @param[in] nfds Highest fd + 1
 * @param[in] readfds_in Sockets to check for reading
 * @param[in] writefds_in Sockets to check for writing
 * @param[in] errorfds_in Sockets to check for errors
 * @param[in] timeout Timeout
 * @param[out] readfds_out Ready for reading
 * @param[out] writefds_out Ready for writing
 * @param[out] errorfds_out Have errors
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Select(
    sf::Out<s32> out_errno, sf::Out<s32> out_count,
    s32 nfds, const sf::InAutoSelectBuffer& readfds_in,
    const sf::InAutoSelectBuffer& writefds_in,
    const sf::InAutoSelectBuffer& errorfds_in,
    const sf::InAutoSelectBuffer& timeout,
    sf::OutAutoSelectBuffer readfds_out,
    sf::OutAutoSelectBuffer writefds_out,
    sf::OutAutoSelectBuffer errorfds_out)
{
    m_command_count++;
    LOG_VERBOSE("[BSD#%u] Select: cmd_count=%u, nfds=%d", m_session_id, m_command_count, nfds);

    // fd_set is a bitmask array: each bit represents an fd
    // FD_SETSIZE on Switch is typically 1024, so max ~128 bytes per set
    auto& manager = ProxySocketManager::GetInstance();

    // Helper to check if fd is set in fd_set
    auto fd_isset = [](s32 fd, const void* fds, size_t size) -> bool {
        if (fds == nullptr || fd < 0) return false;
        size_t byte_idx = static_cast<size_t>(fd) / 8;
        if (byte_idx >= size) return false;
        uint8_t bit = 1u << (fd % 8);
        return (static_cast<const uint8_t*>(fds)[byte_idx] & bit) != 0;
    };

    // Helper to set fd in fd_set
    auto fd_set_bit = [](s32 fd, void* fds, size_t size) {
        if (fds == nullptr || fd < 0) return;
        size_t byte_idx = static_cast<size_t>(fd) / 8;
        if (byte_idx >= size) return;
        uint8_t bit = 1u << (fd % 8);
        static_cast<uint8_t*>(fds)[byte_idx] |= bit;
    };

    // Initialize output fd_sets to zero
    if (readfds_out.GetSize() > 0) {
        std::memset(readfds_out.GetPointer(), 0, readfds_out.GetSize());
    }
    if (writefds_out.GetSize() > 0) {
        std::memset(writefds_out.GetPointer(), 0, writefds_out.GetSize());
    }
    if (errorfds_out.GetSize() > 0) {
        std::memset(errorfds_out.GetPointer(), 0, errorfds_out.GetSize());
    }

    // Check for LDN proxy sockets in the fd sets
    bool has_proxy_sockets = false;
    bool has_real_sockets = false;
    s32 ready_count = 0;

    for (s32 fd = 0; fd < nfds; fd++) {
        bool in_read = fd_isset(fd, readfds_in.GetPointer(), readfds_in.GetSize());
        bool in_write = fd_isset(fd, writefds_in.GetPointer(), writefds_in.GetSize());
        bool in_error = fd_isset(fd, errorfds_in.GetPointer(), errorfds_in.GetSize());

        if (!in_read && !in_write && !in_error) continue;

        ProxySocket* proxy = manager.GetProxySocket(fd);
        if (proxy != nullptr) {
            has_proxy_sockets = true;

            // Check read readiness
            if (in_read && proxy->HasPendingData()) {
                fd_set_bit(fd, readfds_out.GetPointer(), readfds_out.GetSize());
                ready_count++;
            }

            // Proxy sockets are always writable
            if (in_write) {
                fd_set_bit(fd, writefds_out.GetPointer(), writefds_out.GetSize());
                ready_count++;
            }

            // Check for errors (closed socket)
            if (in_error && proxy->GetState() == ProxySocketState::Closed) {
                fd_set_bit(fd, errorfds_out.GetPointer(), errorfds_out.GetSize());
                ready_count++;
            }
        } else {
            has_real_sockets = true;
        }
    }

    // If we only have proxy sockets, return immediately
    if (has_proxy_sockets && !has_real_sockets) {
        out_errno.SetValue(0);
        out_count.SetValue(ready_count);
        LOG_INFO("BSD Select (proxy only) -> %d ready", ready_count);
        R_SUCCEED();
    }

    // If proxy sockets are ready, return immediately with those results
    if (has_proxy_sockets && ready_count > 0) {
        out_errno.SetValue(0);
        out_count.SetValue(ready_count);
        LOG_INFO("BSD Select (mixed, proxy ready) -> %d ready", ready_count);
        R_SUCCEED();
    }

    // Forward to real BSD service
    struct {
        s32 errno_val;
        s32 count;
    } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 5, nfds, out,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { readfds_in.GetPointer(), readfds_in.GetSize() },
            { writefds_in.GetPointer(), writefds_in.GetSize() },
            { errorfds_in.GetPointer(), errorfds_in.GetSize() },
            { timeout.GetPointer(), timeout.GetSize() },
            { readfds_out.GetPointer(), readfds_out.GetSize() },
            { writefds_out.GetPointer(), writefds_out.GetSize() },
            { errorfds_out.GetPointer(), errorfds_out.GetSize() },
        }
    );

    // If we have proxy sockets, merge results after real select
    if (has_proxy_sockets && R_SUCCEEDED(rc)) {
        for (s32 fd = 0; fd < nfds; fd++) {
            ProxySocket* proxy = manager.GetProxySocket(fd);
            if (proxy != nullptr) {
                bool in_read = fd_isset(fd, readfds_in.GetPointer(), readfds_in.GetSize());
                bool in_write = fd_isset(fd, writefds_in.GetPointer(), writefds_in.GetSize());
                bool in_error = fd_isset(fd, errorfds_in.GetPointer(), errorfds_in.GetSize());

                if (in_read && proxy->HasPendingData()) {
                    fd_set_bit(fd, readfds_out.GetPointer(), readfds_out.GetSize());
                    out.count++;
                }
                if (in_write) {
                    fd_set_bit(fd, writefds_out.GetPointer(), writefds_out.GetSize());
                    out.count++;
                }
                if (in_error && proxy->GetState() == ProxySocketState::Closed) {
                    fd_set_bit(fd, errorfds_out.GetPointer(), errorfds_out.GetSize());
                    out.count++;
                }
            }
        }
    }

    out_errno.SetValue(out.errno_val);
    out_count.SetValue(out.count);
    R_RETURN(rc);
}

/**
 * @brief Wait for socket activity - poll (Command 6)
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 nfds
 *   [4] s32 timeout
 *   [0x21] Type-0x21 buffer (pollfd array, auto-select)
 *
 * Output:
 *   [4] s32 errno
 *   [4] s32 count
 *   [0x22] Type-0x22 buffer (pollfd array with revents, auto-select)
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[out] out_count Number of ready sockets
 * @param[in] fds_in Array of PollFd structures
 * @param[out] fds_out Updated PollFd array with revents
 * @param[in] nfds Number of file descriptors
 * @param[in] timeout Timeout in milliseconds
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Poll(
    sf::Out<s32> out_errno, sf::Out<s32> out_count,
    const sf::InAutoSelectBuffer& fds_in,
    sf::OutAutoSelectBuffer fds_out,
    s32 nfds, s32 timeout)
{
    m_command_count++;
    LOG_VERBOSE("[BSD#%u] Poll: cmd_count=%u, nfds=%d, timeout=%d", m_session_id, m_command_count, nfds, timeout);

    // Copy input to output first
    if (fds_out.GetSize() >= fds_in.GetSize()) {
        std::memcpy(fds_out.GetPointer(), fds_in.GetPointer(), fds_in.GetSize());
    }

    // Check for LDN proxy sockets in the poll array
    auto& manager = ProxySocketManager::GetInstance();
    auto* poll_fds = reinterpret_cast<ryu_ldn::bsd::PollFd*>(fds_out.GetPointer());
    size_t num_fds = std::min(static_cast<size_t>(nfds),
                               fds_out.GetSize() / sizeof(ryu_ldn::bsd::PollFd));

    // Check which fds are proxy sockets and handle them
    bool has_proxy_sockets = false;
    bool has_real_sockets = false;
    s32 ready_count = 0;

    for (size_t i = 0; i < num_fds; i++) {
        poll_fds[i].revents = 0;  // Clear revents

        ProxySocket* proxy = manager.GetProxySocket(poll_fds[i].fd);
        if (proxy != nullptr) {
            has_proxy_sockets = true;

            // Use system POLL* macros from poll.h
            // Check read readiness
            if ((poll_fds[i].events & POLLIN) && proxy->HasPendingData()) {
                poll_fds[i].revents |= static_cast<int16_t>(POLLIN);
            }

            // Proxy sockets are always writable (write goes to queue)
            if (poll_fds[i].events & POLLOUT) {
                poll_fds[i].revents |= static_cast<int16_t>(POLLOUT);
            }

            // Check for errors/hangup
            if (proxy->GetState() == ProxySocketState::Closed) {
                poll_fds[i].revents |= static_cast<int16_t>(POLLHUP);
            }

            if (poll_fds[i].revents != 0) {
                ready_count++;
            }
        } else {
            has_real_sockets = true;
        }
    }

    // If we only have proxy sockets, return immediately
    if (has_proxy_sockets && !has_real_sockets) {
        // If no proxy sockets are ready and timeout != 0, we should wait
        // For now, just return immediately (games typically use short timeouts)
        out_errno.SetValue(0);
        out_count.SetValue(ready_count);
        LOG_INFO("BSD Poll (proxy only) -> %d ready", ready_count);
        R_SUCCEED();
    }

    // If we have a mix, we need to handle both
    // For now, if any proxy socket is ready, return immediately
    if (has_proxy_sockets && ready_count > 0) {
        // Mark non-proxy fds as not ready (they weren't checked)
        for (size_t i = 0; i < num_fds; i++) {
            if (manager.GetProxySocket(poll_fds[i].fd) == nullptr) {
                poll_fds[i].revents = 0;
            }
        }
        out_errno.SetValue(0);
        out_count.SetValue(ready_count);
        LOG_INFO("BSD Poll (mixed, proxy ready) -> %d ready", ready_count);
        R_SUCCEED();
    }

    // Forward to real BSD service (no proxy sockets, or none ready with timeout)
    struct {
        s32 nfds;
        s32 timeout;
    } in = { nfds, timeout };

    struct {
        s32 errno_val;
        s32 count;
    } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 6, in, out,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { fds_in.GetPointer(), fds_in.GetSize() },
            { fds_out.GetPointer(), fds_out.GetSize() },
        }
    );

    // If we have proxy sockets, merge results
    if (has_proxy_sockets && R_SUCCEEDED(rc)) {
        // Re-check proxy sockets after real poll returned
        for (size_t i = 0; i < num_fds; i++) {
            ProxySocket* proxy = manager.GetProxySocket(poll_fds[i].fd);
            if (proxy != nullptr) {
                poll_fds[i].revents = 0;

                if ((poll_fds[i].events & POLLIN) && proxy->HasPendingData()) {
                    poll_fds[i].revents |= static_cast<int16_t>(POLLIN);
                    out.count++;
                }
                if (poll_fds[i].events & POLLOUT) {
                    poll_fds[i].revents |= static_cast<int16_t>(POLLOUT);
                    out.count++;
                }
                if (proxy->GetState() == ProxySocketState::Closed) {
                    poll_fds[i].revents |= static_cast<int16_t>(POLLHUP);
                    out.count++;
                }
            }
        }
    }

    out_errno.SetValue(out.errno_val);
    out_count.SetValue(out.count);
    R_RETURN(rc);
}

/**
 * @brief System control (Command 7)
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [0x5] Type-A buffer (name)
 *   [0x5] Type-A buffer (old_val)
 *   [0x5] Type-A buffer (new_val)
 *
 * Output:
 *   [4] s32 errno
 *   [0x6] Type-B buffer (old_val)
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[in] name Sysctl name
 * @param[in] old_val_in Old value buffer
 * @param[out] old_val_out Current value
 * @param[in] new_val New value to set
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Sysctl(
    sf::Out<s32> out_errno,
    const sf::InBuffer& name,
    const sf::InBuffer& old_val_in,
    sf::OutBuffer old_val_out,
    const sf::InBuffer& new_val)
{
    m_command_count++;
    LOG_VERBOSE("[BSD#%u] Sysctl: cmd_count=%u", m_session_id, m_command_count);

    s32 errno_out = 0;
    Result rc = serviceMitmDispatchOut(
        m_forward_service.get(), 7, errno_out,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcMapAlias,
            SfBufferAttr_In | SfBufferAttr_HipcMapAlias,
            SfBufferAttr_Out | SfBufferAttr_HipcMapAlias,
            SfBufferAttr_In | SfBufferAttr_HipcMapAlias,
        },
        .buffers = {
            { name.GetPointer(), name.GetSize() },
            { old_val_in.GetPointer(), old_val_in.GetSize() },
            { old_val_out.GetPointer(), old_val_out.GetSize() },
            { new_val.GetPointer(), new_val.GetSize() },
        }
    );

    out_errno.SetValue(errno_out);
    R_RETURN(rc);
}

/**
 * @brief I/O control (Command 19)
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *   [4] u32 request
 *   [4] u32 bufcount
 *   [0x21] Type-0x21 buffer (auto-select)
 *
 * Output:
 *   [4] s32 errno
 *   [4] s32 result
 *   [0x22] Type-0x22 buffer (auto-select)
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[out] out_result Ioctl result
 * @param[in] fd Socket file descriptor
 * @param[in] request Ioctl request code
 * @param[in] bufcount Number of buffers
 * @param[in] buf_in Input buffer
 * @param[out] buf_out Output buffer
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Ioctl(
    sf::Out<s32> out_errno, sf::Out<s32> out_result,
    s32 fd, u32 request, u32 bufcount,
    const sf::InAutoSelectBuffer& buf_in,
    sf::OutAutoSelectBuffer buf_out)
{
    m_command_count++;
    LOG_VERBOSE("[BSD#%u] Ioctl: cmd_count=%u, fd=%d, request=0x%08x, bufcount=%u", m_session_id, m_command_count, fd, request, bufcount);

    // Check if this is a proxy socket and handle FIONREAD
    auto& manager = ProxySocketManager::GetInstance();
    ProxySocket* proxy = manager.GetProxySocket(fd);
    if (proxy != nullptr) {
        constexpr u32 FIONREAD = 0x4004667F;

        if (request == FIONREAD) {
            // Return bytes available in receive queue
            size_t pending = proxy->GetPendingDataSize();
            if (buf_out.GetSize() >= sizeof(s32)) {
                *reinterpret_cast<s32*>(buf_out.GetPointer()) = static_cast<s32>(pending);
            }
            out_errno.SetValue(0);
            out_result.SetValue(0);
            LOG_INFO("BSD Ioctl FIONREAD fd=%d -> %zu bytes", fd, pending);
            R_SUCCEED();
        }

        // Other ioctl requests not supported for proxy sockets
        out_errno.SetValue(static_cast<s32>(ryu_ldn::bsd::BsdErrno::Inval));
        out_result.SetValue(-1);
        R_SUCCEED();
    }

    // Forward to real BSD service for non-proxy sockets
    struct {
        s32 fd;
        u32 request;
        u32 bufcount;
    } in = { fd, request, bufcount };

    struct {
        s32 errno_val;
        s32 result;
    } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 19, in, out,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { buf_in.GetPointer(), buf_in.GetSize() },
            { buf_out.GetPointer(), buf_out.GetSize() },
        }
    );

    out_errno.SetValue(out.errno_val);
    out_result.SetValue(out.result);
    R_RETURN(rc);
}

/**
 * @brief File control (Command 20)
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *   [4] s32 cmd
 *   [4] s32 arg
 *
 * Output:
 *   [4] s32 errno
 *   [4] s32 result
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[out] out_result Fcntl result
 * @param[in] fd Socket file descriptor
 * @param[in] cmd Fcntl command
 * @param[in] arg Argument
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Fcntl(
    sf::Out<s32> out_errno, sf::Out<s32> out_result,
    s32 fd, s32 cmd, s32 arg)
{
    m_command_count++;
    LOG_VERBOSE("[BSD#%u] Fcntl: cmd_count=%u, fd=%d, cmd=%d, arg=%d", m_session_id, m_command_count, fd, cmd, arg);

    // Check if this is a proxy socket and handle non-blocking flag
    auto& manager = ProxySocketManager::GetInstance();
    ProxySocket* proxy = manager.GetProxySocket(fd);
    if (proxy != nullptr) {
        constexpr s32 F_GETFL = 3;
        constexpr s32 F_SETFL = 4;
        constexpr s32 O_NONBLOCK = 0x0004;

        if (cmd == F_GETFL) {
            // Return current flags (only O_NONBLOCK is tracked)
            s32 flags = proxy->IsNonBlocking() ? O_NONBLOCK : 0;
            out_errno.SetValue(0);
            out_result.SetValue(flags);
            LOG_INFO("BSD Fcntl F_GETFL fd=%d -> flags=0x%x", fd, flags);
            R_SUCCEED();
        } else if (cmd == F_SETFL) {
            // Set non-blocking flag
            bool non_blocking = (arg & O_NONBLOCK) != 0;
            proxy->SetNonBlocking(non_blocking);
            out_errno.SetValue(0);
            out_result.SetValue(0);
            LOG_INFO("BSD Fcntl F_SETFL fd=%d non_blocking=%d", fd, non_blocking);
            R_SUCCEED();
        }

        // Unknown command for proxy socket
        out_errno.SetValue(static_cast<s32>(ryu_ldn::bsd::BsdErrno::Inval));
        out_result.SetValue(-1);
        R_SUCCEED();
    }

    // Forward to real BSD service for non-proxy sockets
    struct {
        s32 fd;
        s32 cmd;
        s32 arg;
    } in = { fd, cmd, arg };

    struct {
        s32 errno_val;
        s32 result;
    } out = {};

    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 20, in, out
    );

    out_errno.SetValue(out.errno_val);
    out_result.SetValue(out.result);
    R_RETURN(rc);
}

/**
 * @brief Get socket option (Command 17)
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *   [4] s32 level
 *   [4] s32 optname
 *
 * Output:
 *   [4] s32 errno
 *   [0x22] Type-0x22 buffer (optval, auto-select)
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[in] fd Socket file descriptor
 * @param[in] level Option level
 * @param[in] optname Option name
 * @param[out] optval Option value
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::GetSockOpt(
    sf::Out<s32> out_errno,
    s32 fd, s32 level, s32 optname,
    sf::OutAutoSelectBuffer optval)
{
    m_command_count++;
    LOG_VERBOSE("[BSD#%u] GetSockOpt: cmd_count=%u, fd=%d, level=%d, optname=%d", m_session_id, m_command_count, fd, level, optname);

    // Check if this is a proxy socket
    auto& manager = ProxySocketManager::GetInstance();
    ProxySocket* proxy = manager.GetProxySocket(fd);
    if (proxy != nullptr) {
        // Delegate to ProxySocket::GetSockOpt
        size_t optlen = optval.GetSize();
        Result rc = proxy->GetSockOpt(level, optname, optval.GetPointer(), &optlen);
        if (R_SUCCEEDED(rc)) {
            out_errno.SetValue(0);
        } else {
            // Result contains the errno
            out_errno.SetValue(static_cast<s32>(rc.GetValue()));
        }
        LOG_INFO("BSD GetSockOpt fd=%d level=%d optname=%d -> proxy", fd, level, optname);
        R_SUCCEED();
    }

    // Forward to real BSD service for non-proxy sockets
    struct {
        s32 fd;
        s32 level;
        s32 optname;
    } in = { fd, level, optname };

    s32 errno_out = 0;
    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 17, in, errno_out,
        .buffer_attrs = {
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { optval.GetPointer(), optval.GetSize() },
        }
    );

    out_errno.SetValue(errno_out);
    R_RETURN(rc);
}

/**
 * @brief Set socket option (Command 21)
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *   [4] s32 level
 *   [4] s32 optname
 *   [0x21] Type-0x21 buffer (optval, auto-select)
 *
 * Output:
 *   [4] s32 errno
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[in] fd Socket file descriptor
 * @param[in] level Option level
 * @param[in] optname Option name
 * @param[in] optval Option value
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::SetSockOpt(
    sf::Out<s32> out_errno,
    s32 fd, s32 level, s32 optname,
    const sf::InAutoSelectBuffer& optval)
{
    m_command_count++;
    LOG_VERBOSE("[BSD#%u] SetSockOpt: cmd_count=%u, fd=%d, level=%d, optname=%d", m_session_id, m_command_count, fd, level, optname);

    // Check if this is a proxy socket
    auto& manager = ProxySocketManager::GetInstance();
    ProxySocket* proxy = manager.GetProxySocket(fd);
    if (proxy != nullptr) {
        // Delegate to ProxySocket::SetSockOpt
        Result rc = proxy->SetSockOpt(level, optname, optval.GetPointer(), optval.GetSize());
        if (R_SUCCEEDED(rc)) {
            out_errno.SetValue(0);
        } else {
            // Result contains the errno
            out_errno.SetValue(static_cast<s32>(rc.GetValue()));
        }
        LOG_INFO("BSD SetSockOpt fd=%d level=%d optname=%d -> proxy", fd, level, optname);
        R_SUCCEED();
    }

    // Track SO_BROADCAST in SocketInfo for when proxy is created later
    if (level == static_cast<s32>(ryu_ldn::bsd::SocketOptionLevel::Socket) &&
        optname == static_cast<s32>(ryu_ldn::bsd::SocketOption::Broadcast)) {
        if (optval.GetSize() >= sizeof(s32)) {
            s32 value = *reinterpret_cast<const s32*>(optval.GetPointer());
            std::scoped_lock lock(g_socket_info_mutex);
            auto it = g_socket_info.find(fd);
            if (it != g_socket_info.end()) {
                it->second.broadcast = (value != 0);
                LOG_INFO("BSD SetSockOpt fd=%d SO_BROADCAST=%d (saved for proxy)", fd, value);
            }
        }
    }

    // Forward to real BSD service for non-proxy sockets
    struct {
        s32 fd;
        s32 level;
        s32 optname;
    } in = { fd, level, optname };

    s32 errno_out = 0;
    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 21, in, errno_out,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { optval.GetPointer(), optval.GetSize() },
        }
    );

    out_errno.SetValue(errno_out);
    R_RETURN(rc);
}

/**
 * @brief Mark socket as listening (Command 18)
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *   [4] s32 backlog
 *
 * Output:
 *   [4] s32 errno
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[in] fd Socket file descriptor
 * @param[in] backlog Connection queue size
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Listen(
    sf::Out<s32> out_errno,
    s32 fd, s32 backlog)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] Listen ENTRY: cmd_count=%u, fd=%d, backlog=%d", m_session_id, m_command_count, fd, backlog);

    struct {
        s32 fd;
        s32 backlog;
    } in = { fd, backlog };

    s32 errno_out = 0;
    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 18, in, errno_out
    );

    out_errno.SetValue(errno_out);
    R_RETURN(rc);
}

/**
 * @brief Shutdown socket I/O (Command 22)
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [4] s32 fd
 *   [4] s32 how
 *
 * Output:
 *   [4] s32 errno
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[in] fd Socket file descriptor
 * @param[in] how Shutdown direction
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::Shutdown(
    sf::Out<s32> out_errno,
    s32 fd, s32 how)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] Shutdown ENTRY: cmd_count=%u, fd=%d, how=%d", m_session_id, m_command_count, fd, how);

    struct {
        s32 fd;
        s32 how;
    } in = { fd, how };

    s32 errno_out = 0;
    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 22, in, errno_out
    );

    out_errno.SetValue(errno_out);
    R_RETURN(rc);
}

/**
 * @brief Shutdown all sockets for a process (Command 23)
 *
 * ## IPC Interface
 *
 * ```
 * Input:
 *   [8] u64 pid
 *   [4] s32 how
 *
 * Output:
 *   [4] s32 errno
 * ```
 *
 * @param[out] out_errno BSD errno
 * @param[in] pid Process ID
 * @param[in] how Shutdown direction
 *
 * @return Result code from forwarding
 */
Result BsdMitmService::ShutdownAllSockets(
    sf::Out<s32> out_errno,
    u64 pid, s32 how)
{
    m_command_count++;
    LOG_INFO("[BSD#%u] ShutdownAllSockets ENTRY: cmd_count=%u, pid=%lu, how=%d", m_session_id, m_command_count, pid, how);

    struct {
        u64 pid;
        s32 how;
    } in = { pid, how };

    s32 errno_out = 0;
    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 23, in, errno_out
    );

    out_errno.SetValue(errno_out);
    R_RETURN(rc);
}

} // namespace ams::mitm::bsd
