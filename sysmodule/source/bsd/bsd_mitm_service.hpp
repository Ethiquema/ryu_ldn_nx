/**
 * @file bsd_mitm_service.hpp
 * @brief BSD MITM Service - Main service class for bsd:u interception
 *
 * This service intercepts calls to the Nintendo bsd:u service to detect and
 * proxy LDN traffic through the RyuLdn server.
 *
 * ## How It Works
 *
 * 1. All BSD calls are intercepted
 * 2. For sockets that don't target LDN addresses, calls are forwarded to the
 *    real bsd:u service transparently
 * 3. For sockets that bind/connect to LDN addresses (10.114.x.x), we track
 *    them as "proxy sockets"
 * 4. Send/Recv on proxy sockets are routed through ProxyData packets instead
 *    of real network traffic
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include "interfaces/ibsd_mitm_service.hpp"
#include "bsd_types.hpp"

namespace ams::mitm::bsd {

/**
 * @brief BSD MITM Service implementation
 *
 * This class implements the bsd:u MITM service. It forwards most calls to
 * the real service but intercepts and proxies LDN-related socket operations.
 */
class BsdMitmService : public sf::MitmServiceImplBase {
public:
    /**
     * @brief Constructor
     *
     * @param s Shared pointer to the original service
     * @param c MITM process info for the client
     */
    /// @gdb{tag="BSD:LIFECYCLE", msg="Constructor: program_id=0x%lx, pid=%lu", args="$x2, $x1"}
    BsdMitmService(std::shared_ptr<::Service>&& s, const sm::MitmProcessInfo& c);

    /**
     * @brief Destructor - cleanup tracked sockets
     */
    /// @gdb{tag="BSD:LIFECYCLE", msg="Destructor"}
    ~BsdMitmService();

    /**
     * @brief Determine if we should MITM this process
     *
     * We MITM all processes for now to ensure we catch any LDN traffic.
     * In the future, we could filter by title ID.
     *
     * @param client_info Process information for the client
     * @return true Always intercept
     */
    /// @gdb{tag="BSD:LIFECYCLE", msg="ShouldMitm: program_id=0x%lx", args="$x1"}
    static bool ShouldMitm(const sm::MitmProcessInfo& client_info);

    /**
     * @brief Clean up abandoned forward services
     *
     * Sessions that never received RegisterClient have their forward_service
     * moved to an abandoned list to prevent system freeze. This function
     * cleans up those services. Should be called when LDN disconnects or
     * when the game process exits.
     */
    static void CleanupAbandonedServices();

public:
    // =========================================================================
    // Command Implementations
    // All commands forward to the real service by default
    // =========================================================================

    /**
     * @brief Register a BSD client with this MITM session
     *
     * Forwards to the real bsd:u RegisterClient and records the client's
     * transfer memory + configuration so subsequent socket calls can be
     * scoped to this process. Sessions that skip this call are moved to
     * the abandoned list (see CleanupAbandonedServices).
     *
     * @param out_result Output result code from the real service
     * @param config Library-side BSD configuration (version, flags)
     * @param client_pid Client process ID supplied by the framework
     * @param tmem_size Size of the client's transfer memory region
     * @param transfer_memory Handle to the client transfer memory
     * @return Result code
     */
    /// @gdb{tag="BSD:LIFECYCLE", msg="RegisterClient: config_size=%u", args="$x2"}
    Result RegisterClient(
        sf::Out<u64> out_result,
        const ryu_ldn::bsd::LibraryConfigData& config,
        const sf::ClientProcessId& client_pid,
        u64 tmem_size,
        sf::CopyHandle&& transfer_memory);

    /**
     * @brief Start monitoring a client process
     *
     * Forwards to the real bsd:u StartMonitoring call. Used by the BSD
     * service to track per-process resource usage. Marked with the
     * LIFECYCLE tag because it is part of session bring-up.
     *
     * @param out_errno Output errno value from the real service
     * @param pid Process ID to monitor
     * @return Result code
     */
    /// @gdb{tag="BSD:LIFECYCLE", msg="StartMonitoring: pid=%lu", args="$x2"}
    Result StartMonitoring(sf::Out<s32> out_errno, u64 pid);

    /**
     * @brief Create a new socket
     *
     * Forwards to the real bsd:u Socket. The returned file descriptor is
     * tracked by ProxySocketManager only if a subsequent bind/connect
     * targets the LDN subnet (10.114.x.x); otherwise it behaves exactly
     * like a normal BSD socket.
     *
     * @param out_errno Output errno (0 on success)
     * @param out_fd Output file descriptor
     * @param domain Address family (AF_INET, etc.)
     * @param type Socket type (SOCK_STREAM, SOCK_DGRAM, etc.)
     * @param protocol Protocol number
     * @return Result code
     */
    /// @gdb{tag="BSD:SOCKET", msg="Socket: domain=%d type=%d protocol=%d", args="$x3, $x4, $x5"}
    Result Socket(
        sf::Out<s32> out_errno, sf::Out<s32> out_fd,
        s32 domain, s32 type, s32 protocol);

    /**
     * @brief Create a new exempt socket
     *
     * Sockets created via SocketExempt are not subject to certain BSD
     * resource limits. The MITM layer applies the same LDN-subnet
     * detection logic as for Socket().
     *
     * @param out_errno Output errno (0 on success)
     * @param out_fd Output file descriptor
     * @param domain Address family
     * @param type Socket type
     * @param protocol Protocol number
     * @return Result code
     */
    /// @gdb{tag="BSD:SOCKET", msg="SocketExempt: domain=%d type=%d protocol=%d", args="$x3, $x4, $x5"}
    Result SocketExempt(
        sf::Out<s32> out_errno, sf::Out<s32> out_fd,
        s32 domain, s32 type, s32 protocol);

    /**
     * @brief Open a file descriptor by path
     *
     * Forwards to the real bsd:u Open. Not LDN-specific; included for
     * interface completeness.
     *
     * @param out_errno Output errno
     * @param out_fd Output file descriptor
     * @param path Path buffer
     * @return Result code
     */
    /// @gdb{tag="BSD:SOCKET", msg="Open"}
    Result Open(
        sf::Out<s32> out_errno, sf::Out<s32> out_fd,
        const sf::InBuffer& path);

    /**
     * @brief Monitor multiple file descriptors for readiness
     *
     * Forwards to the real bsd:u Select. Proxy sockets participate in
     * select normally — the receive thread signals readiness when
     * ProxyData packets arrive for them.
     *
     * @param out_errno Output errno
     * @param out_count Output number of ready descriptors
     * @param nfds Number of descriptors in the sets
     * @param readfds_in Input read set
     * @param writefds_in Input write set
     * @param errorfds_in Input error set
     * @param timeout Timeout value
     * @param readfds_out Output read set
     * @param writefds_out Output write set
     * @param errorfds_out Output error set
     * @return Result code
     */
    /// @gdb{tag="BSD:CONFIG", msg="Select: nfds=%d", args="$x3"}
    Result Select(
        sf::Out<s32> out_errno, sf::Out<s32> out_count,
        s32 nfds, const sf::InAutoSelectBuffer& readfds_in,
        const sf::InAutoSelectBuffer& writefds_in,
        const sf::InAutoSelectBuffer& errorfds_in,
        const sf::InAutoSelectBuffer& timeout,
        sf::OutAutoSelectBuffer readfds_out,
        sf::OutAutoSelectBuffer writefds_out,
        sf::OutAutoSelectBuffer errorfds_out);

    /**
     * @brief Poll file descriptors for readiness
     *
     * Forwards to the real bsd:u Poll. Proxy sockets are signalled by the
     * receive thread when matching ProxyData packets arrive.
     *
     * @param out_errno Output errno
     * @param out_count Output number of ready descriptors
     * @param fds_in Input pollfd array
     * @param fds_out Output pollfd array
     * @param nfds Number of pollfd entries
     * @param timeout Timeout in milliseconds (-1 = infinite)
     * @return Result code
     */
    /// @gdb{tag="BSD:CONFIG", msg="Poll: nfds=%d timeout=%d", args="$x4, $x5"}
    Result Poll(
        sf::Out<s32> out_errno, sf::Out<s32> out_count,
        const sf::InAutoSelectBuffer& fds_in,
        sf::OutAutoSelectBuffer fds_out,
        s32 nfds, s32 timeout);

    /**
     * @brief Query or set kernel sysctl values
     *
     * Forwards to the real bsd:u Sysctl. Not LDN-specific; included for
     * interface completeness.
     *
     * @param out_errno Output errno
     * @param name MIB name buffer
     * @param old_val_in Input old value buffer
     * @param old_val_out Output old value buffer
     * @param new_val New value buffer
     * @return Result code
     */
    /// @gdb{tag="BSD:CONFIG", msg="Sysctl"}
    Result Sysctl(
        sf::Out<s32> out_errno,
        const sf::InBuffer& name,
        const sf::InBuffer& old_val_in,
        sf::OutBuffer old_val_out,
        const sf::InBuffer& new_val);

    /**
     * @brief Receive data on a socket
     *
     * For proxy sockets, returns data delivered by the receive thread from
     * ProxyData packets. For regular sockets, forwards to the real bsd:u
     * Recv.
     *
     * @param out_errno Output errno
     * @param out_size Output number of bytes received
     * @param fd File descriptor
     * @param flags Recv flags (MSG_DONTWAIT, etc.)
     * @param buffer Output receive buffer
     * @return Result code
     */
    /// @gdb{tag="BSD:DATA", msg="Recv: fd=%d flags=%d", args="$x3, $x4"}
    Result Recv(
        sf::Out<s32> out_errno, sf::Out<s32> out_size,
        s32 fd, s32 flags,
        sf::OutAutoSelectBuffer buffer);

    /**
     * @brief Receive data on a socket and capture the source address
     *
     * For proxy sockets, the source address is synthesized from the
     * ProxyData header (originating peer IP/port).
     *
     * @param out_ret Output return value (bytes received or -1)
     * @param out_errno Output errno
     * @param out_addrlen Output source address length
     * @param fd File descriptor
     * @param flags Recv flags
     * @param buffer Output receive buffer
     * @param addr_out Output source address
     * @return Result code
     */
    /// @gdb{tag="BSD:DATA", msg="RecvFrom: fd=%d flags=%d", args="$x3, $x4"}
    Result RecvFrom(
        sf::Out<s32> out_ret, sf::Out<s32> out_errno, sf::Out<u32> out_addrlen,
        s32 fd, s32 flags,
        sf::OutAutoSelectBuffer buffer,
        sf::OutAutoSelectBuffer addr_out);

    /**
     * @brief Send data on a socket
     *
     * For proxy sockets, the payload is wrapped in a ProxyData packet and
     * tunneled through the RyuLdn TCP connection instead of going out on
     * the real network.
     *
     * @param out_errno Output errno
     * @param out_size Output number of bytes sent
     * @param fd File descriptor
     * @param flags Send flags
     * @param buffer Input send buffer
     * @return Result code
     */
    /// @gdb{tag="BSD:DATA", msg="Send: fd=%d flags=%d", args="$x3, $x4"}
    Result Send(
        sf::Out<s32> out_errno, sf::Out<s32> out_size,
        s32 fd, s32 flags,
        const sf::InAutoSelectBuffer& buffer);

    /**
     * @brief Send data to a specific destination address
     *
     * For proxy sockets, the destination address from `addr` is encoded in
     * the ProxyData header so the Ryujinx server can route the packet to
     * the correct peer. Broadcast destinations are delivered to all
     * matching proxy sockets on the receiving side (PIA mesh discovery).
     *
     * @param out_errno Output errno
     * @param out_size Output number of bytes sent
     * @param fd File descriptor
     * @param flags Send flags
     * @param buffer Input send buffer
     * @param addr Destination address
     * @return Result code
     */
    /// @gdb{tag="BSD:DATA", msg="SendTo: fd=%d flags=%d", args="$x3, $x4"}
    Result SendTo(
        sf::Out<s32> out_errno, sf::Out<s32> out_size,
        s32 fd, s32 flags,
        const sf::InAutoSelectBuffer& buffer,
        const sf::InAutoSelectBuffer& addr);

    /**
     * @brief Accept an incoming connection on a listening socket
     *
     * Forwards to the real bsd:u Accept. Proxy sockets rarely use Accept
     * (PIA traffic is UDP), but TCP proxy sockets may.
     *
     * @param out_errno Output errno
     * @param out_fd Output accepted file descriptor
     * @param fd Listening socket file descriptor
     * @param addr_out Output peer address
     * @return Result code
     */
    /// @gdb{tag="BSD:CONNECT", msg="Accept: fd=%d", args="$x3"}
    Result Accept(
        sf::Out<s32> out_errno, sf::Out<s32> out_fd,
        s32 fd,
        sf::OutAutoSelectBuffer addr_out);

    /**
     * @brief Bind a socket to a local address
     *
     * If the bind address is in the LDN subnet (10.114.x.x), the socket is
     * registered with ProxySocketManager and subsequent send/recv calls on
     * this fd are tunneled through ProxyData. Otherwise the call is
     * forwarded to the real bsd:u Bind.
     *
     * @param out_errno Output errno
     * @param fd File descriptor
     * @param addr Local address to bind to
     * @return Result code
     */
    /// @gdb{tag="BSD:CONNECT", msg="Bind: fd=%d", args="$x2"}
    Result Bind(
        sf::Out<s32> out_errno,
        s32 fd,
        const sf::InAutoSelectBuffer& addr);

    /**
     * @brief Connect a socket to a remote address
     *
     * If the destination is in the LDN subnet (10.114.x.x), the socket is
     * tracked as a proxy socket and the connect is satisfied locally —
     * the actual tunneling happens via ProxyData over the RyuLdn TCP
     * connection. Otherwise the call is forwarded to the real bsd:u
     * Connect.
     *
     * @param out_errno Output errno
     * @param fd File descriptor
     * @param addr Destination address
     * @return Result code
     */
    /// @gdb{tag="BSD:CONNECT", msg="Connect: fd=%d", args="$x2"}
    Result Connect(
        sf::Out<s32> out_errno,
        s32 fd,
        const sf::InAutoSelectBuffer& addr);

    /**
     * @brief Get the address of the peer connected to a socket
     *
     * For proxy sockets, returns the synthesized peer address from the
     * ProxyData / ExternalProxy state. For regular sockets, forwards to
     * the real bsd:u GetPeerName.
     *
     * @param out_errno Output errno
     * @param fd File descriptor
     * @param addr_out Output peer address
     * @return Result code
     */
    /// @gdb{tag="BSD:CONNECT", msg="GetPeerName: fd=%d", args="$x2"}
    Result GetPeerName(
        sf::Out<s32> out_errno,
        s32 fd,
        sf::OutAutoSelectBuffer addr_out);

    /**
     * @brief Get the local address bound to a socket
     *
     * For proxy sockets, returns the virtual LDN address assigned by
     * GetIpv4Address. For regular sockets, forwards to the real bsd:u
     * GetSockName.
     *
     * @param out_errno Output errno
     * @param fd File descriptor
     * @param addr_out Output local address
     * @return Result code
     */
    /// @gdb{tag="BSD:CONNECT", msg="GetSockName: fd=%d", args="$x2"}
    Result GetSockName(
        sf::Out<s32> out_errno,
        s32 fd,
        sf::OutAutoSelectBuffer addr_out);

    /**
     * @brief Retrieve a socket option value
     *
     * Forwards to the real bsd:u GetSockOpt. Proxy sockets report the
     * same options as regular sockets for compatibility with game code
     * that queries SO_ERROR etc.
     *
     * @param out_errno Output errno
     * @param fd File descriptor
     * @param level Protocol level (SOL_SOCKET, IPPROTO_*)
     * @param optname Option name
     * @param optval Output option value buffer
     * @return Result code
     */
    /// @gdb{tag="BSD:CONFIG", msg="GetSockOpt: fd=%d level=%d optname=%d", args="$x2, $x3, $x4"}
    Result GetSockOpt(
        sf::Out<s32> out_errno,
        s32 fd, s32 level, s32 optname,
        sf::OutAutoSelectBuffer optval);

    /**
     * @brief Mark a socket as accepting incoming connections
     *
     * Forwards to the real bsd:u Listen. Rarely used by PIA (UDP) but
     * included for TCP proxy sockets.
     *
     * @param out_errno Output errno
     * @param fd File descriptor
     * @param backlog Maximum queue length for pending connections
     * @return Result code
     */
    /// @gdb{tag="BSD:CONNECT", msg="Listen: fd=%d backlog=%d", args="$x2, $x3"}
    Result Listen(
        sf::Out<s32> out_errno,
        s32 fd, s32 backlog);

    /**
     * @brief Perform an ioctl on a socket
     *
     * Forwards to the real bsd:u Ioctl. Used by game code for interface
     * configuration queries (SIOCGIFADDR, etc.).
     *
     * @param out_errno Output errno
     * @param out_result Output ioctl return value
     * @param fd File descriptor
     * @param request ioctl request code
     * @param bufcount Number of buffer pairs
     * @param buf_in Input buffer
     * @param buf_out Output buffer
     * @return Result code
     */
    /// @gdb{tag="BSD:CONFIG", msg="Ioctl: fd=%d request=0x%x", args="$x3, $x4"}
    Result Ioctl(
        sf::Out<s32> out_errno, sf::Out<s32> out_result,
        s32 fd, u32 request, u32 bufcount,
        const sf::InAutoSelectBuffer& buf_in,
        sf::OutAutoSelectBuffer buf_out);

    /**
     * @brief Perform a fcntl operation on a socket
     *
     * Forwards to the real bsd:u Fcntl. Commonly used by game code to set
     * O_NONBLOCK on game sockets.
     *
     * @param out_errno Output errno
     * @param out_result Output fcntl return value
     * @param fd File descriptor
     * @param cmd fcntl command
     * @param arg Command argument
     * @return Result code
     */
    /// @gdb{tag="BSD:CONFIG", msg="Fcntl: fd=%d cmd=%d arg=%d", args="$x3, $x4, $x5"}
    Result Fcntl(
        sf::Out<s32> out_errno, sf::Out<s32> out_result,
        s32 fd, s32 cmd, s32 arg);

    /**
     * @brief Set a socket option
     *
     * Forwards to the real bsd:u SetSockOpt. Some options (e.g.
     * IP_MULTICAST_TTL, IP_ADD_MEMBERSHIP) are blocked by bsd:u with
     * EPERM; the P2P subsystem uses bsd:s for those instead.
     *
     * @param out_errno Output errno
     * @param fd File descriptor
     * @param level Protocol level (SOL_SOCKET, IPPROTO_*)
     * @param optname Option name
     * @param optval Input option value buffer
     * @return Result code
     */
    /// @gdb{tag="BSD:CONFIG", msg="SetSockOpt: fd=%d level=%d optname=%d", args="$x2, $x3, $x4"}
    Result SetSockOpt(
        sf::Out<s32> out_errno,
        s32 fd, s32 level, s32 optname,
        const sf::InAutoSelectBuffer& optval);

    /**
     * @brief Shut down part of a full-duplex connection
     *
     * For proxy sockets, flushes pending ProxyData and unregisters the
     * socket from ProxySocketManager before signalling shutdown. For
     * regular sockets, forwards to the real bsd:u Shutdown.
     *
     * @param out_errno Output errno
     * @param fd File descriptor
     * @param how Direction to shut down (SHUT_RD / SHUT_WR / SHUT_RDWR)
     * @return Result code
     */
    /// @gdb{tag="BSD:CONNECT", msg="Shutdown: fd=%d how=%d", args="$x2, $x3"}
    Result Shutdown(
        sf::Out<s32> out_errno,
        s32 fd, s32 how);

    /**
     * @brief Shut down all sockets owned by a process
     *
     * Forwards to the real bsd:u ShutdownAllSockets. Called by the game
     * on exit; the MITM layer also cleans up any proxy sockets owned by
     * the terminating process.
     *
     * @param out_errno Output errno
     * @param pid Process ID whose sockets should be closed
     * @param how Direction to shut down
     * @return Result code
     */
    /// @gdb{tag="BSD:CONNECT", msg="ShutdownAllSockets: pid=%lu how=%d", args="$x2, $x3"}
    Result ShutdownAllSockets(
        sf::Out<s32> out_errno,
        u64 pid, s32 how);

    /**
     * @brief Write data to a socket (BSD write path)
     *
     * For proxy sockets, the payload is wrapped in a ProxyData packet and
     * tunneled through the RyuLdn TCP connection. Equivalent to Send()
     * with no flags.
     *
     * @param out_errno Output errno
     * @param out_size Output number of bytes written
     * @param fd File descriptor
     * @param buffer Input buffer
     * @return Result code
     */
    /// @gdb{tag="BSD:DATA", msg="Write: fd=%d", args="$x3"}
    Result Write(
        sf::Out<s32> out_errno, sf::Out<s32> out_size,
        s32 fd,
        const sf::InAutoSelectBuffer& buffer);

    /**
     * @brief Read data from a socket (BSD read path)
     *
     * For proxy sockets, returns data delivered by the receive thread from
     * ProxyData packets. Equivalent to Recv() with no flags.
     *
     * @param out_errno Output errno
     * @param out_size Output number of bytes read
     * @param fd File descriptor
     * @param buffer Output read buffer
     * @return Result code
     */
    /// @gdb{tag="BSD:DATA", msg="Read: fd=%d", args="$x3"}
    Result Read(
        sf::Out<s32> out_errno, sf::Out<s32> out_size,
        s32 fd,
        sf::OutAutoSelectBuffer buffer);

    /**
     * @brief Close a socket
     *
     * For proxy sockets, unregisters the socket from ProxySocketManager
     * and flushes any pending ProxyData before forwarding the close to
     * the real bsd:u Close.
     *
     * @param out_errno Output errno
     * @param fd File descriptor
     * @return Result code
     */
    /// @gdb{tag="BSD:SOCKET", msg="Close: fd=%d", args="$x2"}
    Result Close(
        sf::Out<s32> out_errno,
        s32 fd);

    /**
     * @brief Duplicate a socket file descriptor to another process
     *
     * Forwards to the real bsd:u DuplicateSocket. Used when a game
     * transfers socket ownership between processes.
     *
     * @param out_errno Output errno
     * @param out_fd Output duplicated file descriptor
     * @param fd Source file descriptor
     * @param target_pid Target process ID
     * @return Result code
     */
    /// @gdb{tag="BSD:SOCKET", msg="DuplicateSocket: fd=%d target_pid=%lu", args="$x3, $x4"}
    Result DuplicateSocket(
        sf::Out<s32> out_errno, sf::Out<s32> out_fd,
        s32 fd, u64 target_pid);

    /**
     * @brief Retrieve resource statistics for a process
     *
     * Forwards to the real bsd:u GetResourceStatistics. Not LDN-specific;
     * included for interface completeness.
     *
     * @param out_errno Output errno
     * @param out_stats Output statistics buffer
     * @param pid Process ID to query
     * @return Result code
     */
    Result GetResourceStatistics(
        sf::Out<s32> out_errno,
        sf::OutBuffer out_stats,
        u64 pid);

    /**
     * @brief Receive multiple messages in a single call (recvmmsg)
     *
     * Forwards to the real bsd:u RecvMMsg. Batches multiple RecvFrom
     * operations for throughput-sensitive code.
     *
     * @param out_errno Output errno
     * @param out_count Output number of messages received
     * @param fd File descriptor
     * @param vlen Maximum number of messages
     * @param flags Recv flags
     * @param timeout Timeout in milliseconds
     * @param out_data Output message buffer
     * @return Result code
     */
    Result RecvMMsg(
        sf::Out<s32> out_errno, sf::Out<s32> out_count,
        s32 fd, s32 vlen, s32 flags, s32 timeout,
        sf::OutAutoSelectBuffer out_data);

    /**
     * @brief Send multiple messages in a single call (sendmmsg)
     *
     * Forwards to the real bsd:u SendMMsg. Batches multiple SendTo
     * operations for throughput-sensitive code.
     *
     * @param out_errno Output errno
     * @param out_count Output number of messages sent
     * @param fd File descriptor
     * @param vlen Maximum number of messages
     * @param flags Send flags
     * @param in_data Input message buffer
     * @return Result code
     */
    Result SendMMsg(
        sf::Out<s32> out_errno, sf::Out<s32> out_count,
        s32 fd, s32 vlen, s32 flags,
        const sf::InAutoSelectBuffer& in_data);

    /**
     * @brief Create an event file descriptor (eventfd)
     *
     * Forwards to the real bsd:u EventFd. Used by some game networking
     * stacks for wait/wakeup primitives.
     *
     * @param out_errno Output errno
     * @param out_fd Output file descriptor
     * @param initval Initial counter value
     * @param flags eventfd flags
     * @return Result code
     */
    Result EventFd(
        sf::Out<s32> out_errno, sf::Out<s32> out_fd,
        u64 initval, s32 flags);

    /**
     * @brief Register a name with the resource statistics subsystem
     *
     * Forwards to the real bsd:u RegisterResourceStatisticsName. Not
     * LDN-specific; included for interface completeness.
     *
     * @param out_errno Output errno
     * @param pid Process ID
     * @param name Name buffer to register
     * @return Result code
     */
    Result RegisterResourceStatisticsName(
        sf::Out<s32> out_errno,
        u64 pid,
        const sf::InBuffer& name);

    /**
     * @brief Register a BSD client with shared configuration (no transfer memory)
     *
     * Variant of RegisterClient used by processes that share BSD
     * configuration with another process. Behaves like RegisterClient
     * except no transfer memory handle is supplied.
     *
     * @param out_result Output result code from the real service
     * @param config Library-side BSD configuration
     * @param client_pid Client process ID supplied by the framework
     * @param tmem_size Size of the shared transfer memory region
     * @return Result code
     */
    Result RegisterClientShared(
        sf::Out<u64> out_result,
        const ryu_ldn::bsd::LibraryConfigData& config,
        const sf::ClientProcessId& client_pid,
        u64 tmem_size);

private:
    /// Client process ID for this session
    u64 m_client_pid;
    /// Number of commands received on this session (for debugging)
    u32 m_command_count = 0;
    /// Unique session ID for debugging (assigned in constructor)
    u32 m_session_id = 0;
    /// Whether RegisterClient was called on this session
    /// Sessions without RegisterClient should not be used for socket operations
    bool m_registered = false;
    /// Static counter for session IDs (atomic for thread safety)
    static inline std::atomic<u32> s_next_session_id{0};
};

// Verify interface compliance
static_assert(ams::mitm::bsd::IsIBsdMitmService<BsdMitmService>);

} // namespace ams::mitm::bsd
