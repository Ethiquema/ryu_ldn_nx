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
};

/**
 * @brief Socket info tracking table
 *
 * Maps file descriptor to socket info. This is needed because Socket()
 * creates the fd but Bind/Connect happens later, and we need the socket
 * type/protocol to create the ProxySocket.
 *
 * TODO: This should be per-client (m_socket_info member) but for now
 * we use a global static. This is safe because:
 * 1. Each game process has its own fd namespace
 * 2. We only track fds for the MITMed client
 */
static std::unordered_map<s32, SocketInfo> g_socket_info;

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
{
    LOG_INFO("BSD MITM service created for program_id=0x%016lx, pid=%lu",
             c.program_id.value, m_client_pid);
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
    LOG_INFO("BSD MITM service destroyed for pid=%lu", m_client_pid);
    // TODO: Story 8.4 - Cleanup tracked proxy sockets for this client
}

/**
 * @brief Determine if we should MITM a process's BSD calls
 *
 * Atmosphere calls this for each process that opens bsd:u.
 * If we return true, our MITM service handles all their BSD calls.
 *
 * ## Strategy
 *
 * We intercept ALL application processes (program_id >= 0x0100000000000000).
 * This is necessary because games typically open bsd:u BEFORE ldn:u, so we
 * can't know at this point if they will use LDN.
 *
 * The overhead is minimal because:
 * 1. We only intercept applications, not system services
 * 2. All calls are forwarded transparently to the real service
 * 3. Proxy sockets are only created when LDN addresses are detected
 *
 * @param client_info Process information (PID, program ID, etc.)
 * @return true For application processes, false for system services
 */
bool BsdMitmService::ShouldMitm(const sm::MitmProcessInfo& client_info) {
    // Our sysmodule's program_id - do not intercept ourselves
    constexpr u64 OUR_PROGRAM_ID = 0x4200000000000010ULL;

    // Skip our own sysmodule to avoid infinite recursion
    if (client_info.program_id.value == OUR_PROGRAM_ID) {
        return false;
    }

    // Application program IDs start at 0x0100000000000000
    // System services have lower program IDs
    constexpr u64 APPLICATION_PROGRAM_ID_BASE = 0x0100000000000000ULL;

    u64 program_id = client_info.program_id.value;

    // Intercept all application processes (games, homebrew)
    // Skip system services to save memory
    if (program_id >= APPLICATION_PROGRAM_ID_BASE) {
        LOG_INFO("BSD ShouldMitm: intercepting application pid=%lu, program_id=0x%016lx",
                 client_info.process_id.value, program_id);
        return true;
    }

    LOG_VERBOSE("BSD ShouldMitm: skipping system service pid=%lu, program_id=0x%016lx",
                client_info.process_id.value, program_id);
    return false;
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
    sf::Out<s32> out_errno, u32 config_size,
    const sf::InAutoSelectBuffer& config,
    const sf::ClientProcessId& client_pid,
    sf::CopyHandle&& transfer_memory)
{
    LOG_VERBOSE("BSD RegisterClient for pid=%lu, config_size=%u",
                client_pid.GetValue().value, config_size);

    struct {
        u32 config_size;
    } in = { config_size };

    s32 errno_out = 0;
    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 0, in, errno_out,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { config.GetPointer(), config.GetSize() },
        },
        .in_send_pid = true,
        .in_num_handles = 1,
        .in_handles = { transfer_memory.GetOsHandle() },
        .override_pid = m_client_pid,
    );

    out_errno.SetValue(errno_out);
    LOG_VERBOSE("BSD RegisterClient result: rc=0x%x errno=%d", rc.GetValue(), errno_out);
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
    LOG_VERBOSE("BSD StartMonitoring for pid=%lu", pid);

    s32 errno_out = 0;
    Result rc = serviceMitmDispatchInOut(
        m_forward_service.get(), 1, pid, errno_out
    );

    out_errno.SetValue(errno_out);
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
    LOG_VERBOSE("BSD Socket domain=%d type=%d protocol=%d", domain, type, protocol);

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

    LOG_VERBOSE("BSD Socket result: rc=0x%x fd=%d errno=%d", rc.GetValue(), out.fd, out.errno_val);

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

        g_socket_info[out.fd] = SocketInfo{
            .type = static_cast<ryu_ldn::bsd::SocketType>(type),
            .protocol = proto,
            .is_proxy = false,
        };
        LOG_VERBOSE("BSD Socket tracked fd=%d type=%d proto=%d", out.fd, type, static_cast<s32>(proto));
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
    LOG_VERBOSE("BSD SocketExempt domain=%d type=%d protocol=%d", domain, type, protocol);

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

        g_socket_info[out.fd] = SocketInfo{
            .type = static_cast<ryu_ldn::bsd::SocketType>(type),
            .protocol = proto,
            .is_proxy = false,
        };
        LOG_VERBOSE("BSD SocketExempt tracked fd=%d type=%d proto=%d", out.fd, type, static_cast<s32>(proto));
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
    LOG_VERBOSE("BSD Open path_size=%zu", path.GetSize());

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
    LOG_VERBOSE("BSD Close fd=%d", fd);

    // Check if this is a proxy socket
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
    LOG_VERBOSE("BSD DuplicateSocket fd=%d target_pid=%lu", fd, target_pid);

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
    LOG_VERBOSE("BSD Bind fd=%d addr_size=%zu", fd, addr.GetSize());

    // Check if this is an LDN address (10.114.x.x)
    if (addr.GetSize() >= sizeof(ryu_ldn::bsd::SockAddrIn)) {
        const auto* sock_addr = reinterpret_cast<const ryu_ldn::bsd::SockAddrIn*>(addr.GetPointer());

        // Check address family is IPv4 and address is LDN
        if (sock_addr->sin_family == static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet) &&
            sock_addr->IsLdnAddress())
        {
            LOG_INFO("BSD Bind fd=%d detected LDN address %u.%u.%u.%u:%u",
                     fd,
                     (sock_addr->sin_addr >> 0) & 0xFF,
                     (sock_addr->sin_addr >> 8) & 0xFF,
                     (sock_addr->sin_addr >> 16) & 0xFF,
                     (sock_addr->sin_addr >> 24) & 0xFF,
                     sock_addr->GetPort());

            // Get socket info (type and protocol)
            auto it = g_socket_info.find(fd);
            if (it == g_socket_info.end()) {
                LOG_WARN("BSD Bind fd=%d not tracked, forwarding to real service", fd);
                // Fall through to normal bind
            } else {
                // Create proxy socket
                auto& manager = ProxySocketManager::GetInstance();
                ProxySocket* proxy = manager.CreateProxySocket(fd, it->second.type, it->second.protocol);

                if (proxy != nullptr) {
                    // Handle ephemeral port (port 0)
                    ryu_ldn::bsd::SockAddrIn bind_addr = *sock_addr;
                    if (bind_addr.GetPort() == 0) {
                        uint16_t ephemeral = manager.AllocatePort(it->second.protocol);
                        if (ephemeral == 0) {
                            LOG_ERROR("BSD Bind fd=%d failed to allocate ephemeral port", fd);
                            out_errno.SetValue(static_cast<s32>(ryu_ldn::bsd::BsdErrno::AddrInUse));
                            R_SUCCEED();
                        }
                        bind_addr.sin_port = __builtin_bswap16(ephemeral);
                        LOG_VERBOSE("BSD Bind fd=%d allocated ephemeral port %u", fd, ephemeral);
                    } else {
                        // Reserve the specific port
                        if (!manager.ReservePort(bind_addr.GetPort(), it->second.protocol)) {
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
                    it->second.is_proxy = true;

                    LOG_INFO("BSD Bind fd=%d successfully bound to LDN proxy", fd);
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
    LOG_VERBOSE("BSD Connect fd=%d addr_size=%zu", fd, addr.GetSize());

    // Check if this is an LDN address (10.114.x.x)
    if (addr.GetSize() >= sizeof(ryu_ldn::bsd::SockAddrIn)) {
        const auto* sock_addr = reinterpret_cast<const ryu_ldn::bsd::SockAddrIn*>(addr.GetPointer());

        // Check address family is IPv4 and address is LDN
        if (sock_addr->sin_family == static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet) &&
            sock_addr->IsLdnAddress())
        {
            LOG_INFO("BSD Connect fd=%d detected LDN address %u.%u.%u.%u:%u",
                     fd,
                     (sock_addr->sin_addr >> 0) & 0xFF,
                     (sock_addr->sin_addr >> 8) & 0xFF,
                     (sock_addr->sin_addr >> 16) & 0xFF,
                     (sock_addr->sin_addr >> 24) & 0xFF,
                     sock_addr->GetPort());

            // Get socket info (type and protocol)
            auto it = g_socket_info.find(fd);
            if (it == g_socket_info.end()) {
                LOG_WARN("BSD Connect fd=%d not tracked, forwarding to real service", fd);
                // Fall through to normal connect
            } else {
                auto& manager = ProxySocketManager::GetInstance();

                // Check if we already have a proxy socket (from Bind)
                ProxySocket* proxy = manager.GetProxySocket(fd);

                // If not bound yet, create one and auto-bind
                if (proxy == nullptr) {
                    proxy = manager.CreateProxySocket(fd, it->second.type, it->second.protocol);
                    if (proxy == nullptr) {
                        LOG_ERROR("BSD Connect fd=%d failed to create proxy socket", fd);
                        out_errno.SetValue(static_cast<s32>(ryu_ldn::bsd::BsdErrno::NoMem));
                        R_SUCCEED();
                    }

                    // Auto-bind with ephemeral port and local LDN IP
                    uint16_t ephemeral = manager.AllocatePort(it->second.protocol);
                    if (ephemeral == 0) {
                        LOG_ERROR("BSD Connect fd=%d failed to allocate ephemeral port", fd);
                        out_errno.SetValue(static_cast<s32>(ryu_ldn::bsd::BsdErrno::AddrInUse));
                        R_SUCCEED();
                    }

                    // Create local address using our LDN IP
                    ryu_ldn::bsd::SockAddrIn local_addr{};
                    local_addr.sin_len = sizeof(local_addr);
                    local_addr.sin_family = static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet);
                    local_addr.sin_port = __builtin_bswap16(ephemeral);
                    local_addr.sin_addr = __builtin_bswap32(manager.GetLocalIp());

                    Result bind_result = proxy->Bind(local_addr);
                    if (R_FAILED(bind_result)) {
                        LOG_ERROR("BSD Connect fd=%d auto-bind failed: 0x%x", fd, bind_result.GetValue());
                        manager.ReleasePort(ephemeral, it->second.protocol);
                        out_errno.SetValue(bind_result.GetValue());
                        R_SUCCEED();
                    }

                    LOG_VERBOSE("BSD Connect fd=%d auto-bound to port %u", fd, ephemeral);
                }

                // Connect the proxy socket to the remote address
                Result connect_result = proxy->Connect(*sock_addr);
                if (R_FAILED(connect_result)) {
                    LOG_ERROR("BSD Connect fd=%d proxy connect failed: 0x%x", fd, connect_result.GetValue());
                    out_errno.SetValue(connect_result.GetValue());
                    R_SUCCEED();
                }

                // Mark as proxy socket
                it->second.is_proxy = true;

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
    LOG_VERBOSE("BSD Accept fd=%d", fd);

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
    LOG_VERBOSE("BSD GetPeerName fd=%d", fd);

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
    LOG_VERBOSE("BSD GetSockName fd=%d", fd);

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
    LOG_VERBOSE("BSD Send fd=%d flags=%d size=%zu", fd, flags, buffer.GetSize());

    // Check if this is a proxy socket
    auto it = g_socket_info.find(fd);
    if (it != g_socket_info.end() && it->second.is_proxy) {
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
    LOG_VERBOSE("BSD SendTo fd=%d flags=%d size=%zu addr_size=%zu",
                fd, flags, buffer.GetSize(), addr.GetSize());

    // Check if this is a proxy socket or if dest is LDN
    auto it = g_socket_info.find(fd);
    bool is_proxy = (it != g_socket_info.end() && it->second.is_proxy);

    // Also check destination address for LDN
    if (addr.GetSize() >= sizeof(ryu_ldn::bsd::SockAddrIn)) {
        const auto* dest_addr = reinterpret_cast<const ryu_ldn::bsd::SockAddrIn*>(addr.GetPointer());

        if (dest_addr->sin_family == static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet) &&
            dest_addr->IsLdnAddress())
        {
            // LDN destination - use proxy
            is_proxy = true;

            // If socket not yet marked as proxy, mark it now
            if (it != g_socket_info.end() && !it->second.is_proxy) {
                auto& manager = ProxySocketManager::GetInstance();

                // Create proxy socket if needed
                ProxySocket* proxy = manager.GetProxySocket(fd);
                if (proxy == nullptr) {
                    proxy = manager.CreateProxySocket(fd, it->second.type, it->second.protocol);
                    if (proxy != nullptr) {
                        // Auto-bind
                        uint16_t ephemeral = manager.AllocatePort(it->second.protocol);
                        if (ephemeral != 0) {
                            ryu_ldn::bsd::SockAddrIn local_addr{};
                            local_addr.sin_len = sizeof(local_addr);
                            local_addr.sin_family = static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet);
                            local_addr.sin_port = __builtin_bswap16(ephemeral);
                            local_addr.sin_addr = __builtin_bswap32(manager.GetLocalIp());
                            R_TRY(proxy->Bind(local_addr));
                        }
                    }
                }
                it->second.is_proxy = true;
            }
        }
    }

    if (is_proxy) {
        auto& manager = ProxySocketManager::GetInstance();
        ProxySocket* proxy = manager.GetProxySocket(fd);

        if (proxy != nullptr && addr.GetSize() >= sizeof(ryu_ldn::bsd::SockAddrIn)) {
            const auto* dest_addr = reinterpret_cast<const ryu_ldn::bsd::SockAddrIn*>(addr.GetPointer());

            // Send via proxy socket
            s32 result = proxy->SendTo(buffer.GetPointer(), buffer.GetSize(), flags, *dest_addr);

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
    LOG_VERBOSE("BSD Recv fd=%d flags=%d buf_size=%zu", fd, flags, buffer.GetSize());

    // Check if this is a proxy socket
    auto it = g_socket_info.find(fd);
    if (it != g_socket_info.end() && it->second.is_proxy) {
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
    sf::Out<s32> out_errno, sf::Out<s32> out_size,
    s32 fd, s32 flags,
    sf::OutAutoSelectBuffer buffer,
    sf::OutAutoSelectBuffer addr_out)
{
    LOG_VERBOSE("BSD RecvFrom fd=%d flags=%d buf_size=%zu", fd, flags, buffer.GetSize());

    // Check if this is a proxy socket
    auto it = g_socket_info.find(fd);
    if (it != g_socket_info.end() && it->second.is_proxy) {
        auto& manager = ProxySocketManager::GetInstance();
        ProxySocket* proxy = manager.GetProxySocket(fd);

        if (proxy != nullptr) {
            // Receive from proxy socket queue with source address
            ryu_ldn::bsd::SockAddrIn from_addr{};
            s32 result = proxy->RecvFrom(buffer.GetPointer(), buffer.GetSize(), flags, &from_addr);

            if (result < 0) {
                // Negative result is -errno
                out_errno.SetValue(-result);
                out_size.SetValue(0);
            } else {
                out_errno.SetValue(0);
                out_size.SetValue(result);

                // Copy source address to output buffer
                if (addr_out.GetSize() >= sizeof(ryu_ldn::bsd::SockAddrIn)) {
                    std::memcpy(addr_out.GetPointer(), &from_addr, sizeof(from_addr));
                }
            }

            LOG_VERBOSE("BSD RecvFrom fd=%d proxy received %d bytes", fd, result);
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

    out_errno.SetValue(out.errno_val);
    out_size.SetValue(out.size);
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
    LOG_VERBOSE("BSD Write fd=%d size=%zu", fd, buffer.GetSize());

    // TODO: Story 8.6 - Same as Send for LDN sockets

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
    LOG_VERBOSE("BSD Read fd=%d buf_size=%zu", fd, buffer.GetSize());

    // TODO: Story 8.6 - Same as Recv for LDN sockets

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
    LOG_VERBOSE("BSD Select nfds=%d", nfds);

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
    LOG_VERBOSE("BSD Poll nfds=%d timeout=%d", nfds, timeout);

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
    LOG_VERBOSE("BSD Sysctl");

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
    LOG_VERBOSE("BSD Ioctl fd=%d request=0x%08x bufcount=%u", fd, request, bufcount);

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
    LOG_VERBOSE("BSD Fcntl fd=%d cmd=%d arg=%d", fd, cmd, arg);

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
    LOG_VERBOSE("BSD GetSockOpt fd=%d level=%d optname=%d", fd, level, optname);

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
    LOG_VERBOSE("BSD SetSockOpt fd=%d level=%d optname=%d", fd, level, optname);

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
    LOG_VERBOSE("BSD Listen fd=%d backlog=%d", fd, backlog);

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
    LOG_VERBOSE("BSD Shutdown fd=%d how=%d", fd, how);

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
    LOG_VERBOSE("BSD ShutdownAllSockets pid=%lu how=%d", pid, how);

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
