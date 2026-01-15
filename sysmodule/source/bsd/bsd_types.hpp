/**
 * @file bsd_types.hpp
 * @brief BSD Socket Types - Compatible with Nintendo Switch BSD service
 *
 * This file defines all data structures used by the BSD socket service (bsd:u)
 * for interception and proxying of LDN network traffic.
 *
 * ## Purpose
 *
 * When games create sockets to communicate over LDN (using virtual IPs like
 * 10.114.x.x), these sockets need to be intercepted and routed through the
 * RyuLdn server via ProxyData packets instead of real network sockets.
 *
 * ## Structure Compatibility
 *
 * All structures must match the Nintendo Switch BSD service interface exactly.
 * Reference: https://switchbrew.org/wiki/Sockets_services
 *
 * ## LDN Network Detection
 *
 * The LDN virtual network uses the 10.114.0.0/16 subnet:
 * - 10.114.0.1 = First player (host typically)
 * - 10.114.0.2 = Second player
 * - etc.
 *
 * Any socket operation targeting this subnet should be proxied.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <cstdint>
#include <cstring>

namespace ryu_ldn::bsd {

// =============================================================================
// Constants
// =============================================================================

/**
 * @brief LDN virtual network base IP (10.114.0.0)
 *
 * All LDN proxy addresses are in the 10.114.0.0/16 subnet.
 * This is used to detect which sockets should be proxied.
 */
constexpr uint32_t LDN_NETWORK_BASE = 0x0A720000;  // 10.114.0.0 in host byte order

/**
 * @brief LDN virtual network mask (255.255.0.0)
 */
constexpr uint32_t LDN_NETWORK_MASK = 0xFFFF0000;

/**
 * @brief Check if an IP address belongs to the LDN virtual network
 * @param ip IPv4 address in host byte order
 * @return true if the address is in 10.114.0.0/16
 */
inline bool IsLdnAddress(uint32_t ip) {
    return (ip & LDN_NETWORK_MASK) == LDN_NETWORK_BASE;
}

/**
 * @brief Maximum number of proxy sockets we can manage
 */
constexpr size_t MAX_PROXY_SOCKETS = 64;

/**
 * @brief Ephemeral port range start (like Linux)
 */
constexpr uint16_t EPHEMERAL_PORT_START = 49152;

/**
 * @brief Ephemeral port range end
 */
constexpr uint16_t EPHEMERAL_PORT_END = 65535;

// =============================================================================
// Address Family
// =============================================================================

/**
 * @brief Address families matching BSD/Nintendo definitions
 */
enum class AddressFamily : uint8_t {
    Unspecified = 0,   ///< AF_UNSPEC
    Unix = 1,          ///< AF_UNIX (local)
    Inet = 2,          ///< AF_INET (IPv4)
    Inet6 = 28,        ///< AF_INET6 (IPv6) - Nintendo uses 28, not Linux 10
};

// =============================================================================
// Socket Types
// =============================================================================

/**
 * @brief Socket types matching BSD definitions
 */
enum class SocketType : int32_t {
    Stream = 1,        ///< SOCK_STREAM (TCP)
    Dgram = 2,         ///< SOCK_DGRAM (UDP)
    Raw = 3,           ///< SOCK_RAW (raw IP)
    Seqpacket = 5,     ///< SOCK_SEQPACKET (sequenced packets)
};

/**
 * @brief Protocol types
 */
enum class ProtocolType : int32_t {
    Unspecified = 0,   ///< Default protocol for socket type
    Icmp = 1,          ///< IPPROTO_ICMP
    Tcp = 6,           ///< IPPROTO_TCP
    Udp = 17,          ///< IPPROTO_UDP
};

// =============================================================================
// Socket Address Structures
// =============================================================================

/**
 * @brief Generic socket address (sockaddr)
 *
 * Base structure for all socket addresses. The actual data depends on
 * the address family.
 */
struct __attribute__((packed)) SockAddr {
    uint8_t  sa_len;       ///< Total length (BSD style)
    uint8_t  sa_family;    ///< Address family (AddressFamily)
    uint8_t  sa_data[14];  ///< Address data
};
static_assert(sizeof(SockAddr) == 16, "SockAddr must be 16 bytes");

/**
 * @brief IPv4 socket address (sockaddr_in)
 *
 * Used for AF_INET addresses. This is the primary structure for LDN
 * proxy socket operations.
 */
struct __attribute__((packed)) SockAddrIn {
    uint8_t  sin_len;      ///< sizeof(SockAddrIn) = 16
    uint8_t  sin_family;   ///< AF_INET = 2
    uint16_t sin_port;     ///< Port number (network byte order)
    uint32_t sin_addr;     ///< IPv4 address (network byte order)
    uint8_t  sin_zero[8];  ///< Padding to 16 bytes

    /**
     * @brief Check if this address is in the LDN network
     */
    bool IsLdnAddress() const {
        // Convert from network to host byte order for comparison
        uint32_t host_addr = __builtin_bswap32(sin_addr);
        return ryu_ldn::bsd::IsLdnAddress(host_addr);
    }

    /**
     * @brief Get port in host byte order
     */
    uint16_t GetPort() const {
        return __builtin_bswap16(sin_port);
    }

    /**
     * @brief Get address in host byte order
     */
    uint32_t GetAddr() const {
        return __builtin_bswap32(sin_addr);
    }
};
static_assert(sizeof(SockAddrIn) == 16, "SockAddrIn must be 16 bytes");

/**
 * @brief IPv6 socket address (sockaddr_in6)
 *
 * Used for AF_INET6 addresses. LDN doesn't use IPv6, but we need
 * this for completeness and forwarding.
 */
struct __attribute__((packed)) SockAddrIn6 {
    uint8_t  sin6_len;       ///< sizeof(SockAddrIn6) = 28
    uint8_t  sin6_family;    ///< AF_INET6 = 28
    uint16_t sin6_port;      ///< Port number (network byte order)
    uint32_t sin6_flowinfo;  ///< IPv6 flow info
    uint8_t  sin6_addr[16];  ///< IPv6 address
    uint32_t sin6_scope_id;  ///< Scope ID
};
static_assert(sizeof(SockAddrIn6) == 28, "SockAddrIn6 must be 28 bytes");

/**
 * @brief Socket address storage (large enough for any address type)
 */
struct __attribute__((packed)) SockAddrStorage {
    uint8_t  ss_len;         ///< Length
    uint8_t  ss_family;      ///< Address family
    uint8_t  ss_padding[126];///< Padding for alignment
};
static_assert(sizeof(SockAddrStorage) == 128, "SockAddrStorage must be 128 bytes");

// =============================================================================
// BSD Service Structures
// =============================================================================

/**
 * @brief Library configuration data passed to RegisterClient
 *
 * This structure configures the BSD socket library for a client.
 */
struct __attribute__((packed)) LibraryConfigData {
    uint32_t version;             ///< Library version (current: 0xA for 19.0.0+)
    uint32_t tcp_tx_buf_size;     ///< TCP transmit buffer size
    uint32_t tcp_rx_buf_size;     ///< TCP receive buffer size
    uint32_t tcp_tx_buf_max_size; ///< TCP max transmit buffer size
    uint32_t tcp_rx_buf_max_size; ///< TCP max receive buffer size
    uint32_t udp_tx_buf_size;     ///< UDP transmit buffer size
    uint32_t udp_rx_buf_size;     ///< UDP receive buffer size
    uint32_t sb_efficiency;       ///< Socket buffer efficiency
};
static_assert(sizeof(LibraryConfigData) == 32, "LibraryConfigData must be 32 bytes");

// =============================================================================
// Socket Options
// =============================================================================

/**
 * @brief Socket option levels
 */
enum class SocketOptionLevel : int32_t {
    Socket = 0xFFFF,   ///< SOL_SOCKET
    Ip = 0,            ///< IPPROTO_IP
    Tcp = 6,           ///< IPPROTO_TCP
    Udp = 17,          ///< IPPROTO_UDP
};

/**
 * @brief Socket-level options (SOL_SOCKET)
 */
enum class SocketOption : int32_t {
    Debug = 0x0001,        ///< SO_DEBUG
    AcceptConn = 0x0002,   ///< SO_ACCEPTCONN
    ReuseAddr = 0x0004,    ///< SO_REUSEADDR
    KeepAlive = 0x0008,    ///< SO_KEEPALIVE
    DontRoute = 0x0010,    ///< SO_DONTROUTE
    Broadcast = 0x0020,    ///< SO_BROADCAST
    Linger = 0x0080,       ///< SO_LINGER
    OobInline = 0x0100,    ///< SO_OOBINLINE
    ReusePort = 0x0200,    ///< SO_REUSEPORT
    SndBuf = 0x1001,       ///< SO_SNDBUF
    RcvBuf = 0x1002,       ///< SO_RCVBUF
    SndLoWat = 0x1003,     ///< SO_SNDLOWAT
    RcvLoWat = 0x1004,     ///< SO_RCVLOWAT
    SndTimeo = 0x1005,     ///< SO_SNDTIMEO
    RcvTimeo = 0x1006,     ///< SO_RCVTIMEO
    Error = 0x1007,        ///< SO_ERROR
    Type = 0x1008,         ///< SO_TYPE
};

// =============================================================================
// Fcntl/Ioctl Constants
// =============================================================================

/**
 * @brief Fcntl commands (limited on Switch)
 */
enum class FcntlCommand : int32_t {
    GetFl = 3,   ///< F_GETFL - Get flags
    SetFl = 4,   ///< F_SETFL - Set flags
};

/**
 * @brief File status flags
 */
enum class FileStatusFlags : int32_t {
    NonBlock = 0x0004,  ///< O_NONBLOCK
};

/**
 * @brief Ioctl requests (whitelisted on Switch)
 */
enum class IoctlRequest : uint32_t {
    FioNread = 0x4004667F,    ///< FIONREAD - bytes available
    SiocAtMark = 0x40047307,  ///< SIOCATMARK - at OOB mark?
};

// =============================================================================
// Poll Structures
// =============================================================================

/**
 * @brief Poll file descriptor structure
 */
struct __attribute__((packed)) PollFd {
    int32_t  fd;       ///< File descriptor
    int16_t  events;   ///< Requested events
    int16_t  revents;  ///< Returned events
};
static_assert(sizeof(PollFd) == 8, "PollFd must be 8 bytes");

/**
 * @brief Poll event flags
 */
enum class PollEvents : int16_t {
    In = 0x0001,       ///< POLLIN - data available
    Pri = 0x0002,      ///< POLLPRI - priority data
    Out = 0x0004,      ///< POLLOUT - can write
    Err = 0x0008,      ///< POLLERR - error condition
    Hup = 0x0010,      ///< POLLHUP - hang up
    Nval = 0x0020,     ///< POLLNVAL - invalid fd
};

// =============================================================================
// Shutdown Constants
// =============================================================================

/**
 * @brief Shutdown how parameter
 */
enum class ShutdownHow : int32_t {
    Read = 0,     ///< SHUT_RD - disable reads
    Write = 1,    ///< SHUT_WR - disable writes
    Both = 2,     ///< SHUT_RDWR - disable both
};

// =============================================================================
// Error Codes
// =============================================================================

/**
 * @brief BSD errno values (Linux-compatible on Switch)
 *
 * Note: Nintendo uses Linux errno values, not FreeBSD.
 */
enum class BsdErrno : int32_t {
    Success = 0,
    Perm = 1,            ///< EPERM
    NoEnt = 2,           ///< ENOENT
    Intr = 4,            ///< EINTR
    Io = 5,              ///< EIO
    BadF = 9,            ///< EBADF
    Again = 11,          ///< EAGAIN/EWOULDBLOCK
    NoMem = 12,          ///< ENOMEM
    Access = 13,         ///< EACCES
    Fault = 14,          ///< EFAULT
    Inval = 22,          ///< EINVAL
    NFile = 23,          ///< ENFILE
    MFile = 24,          ///< EMFILE
    NotSock = 88,        ///< ENOTSOCK
    DestAddrReq = 89,    ///< EDESTADDRREQ
    MsgSize = 90,        ///< EMSGSIZE
    ProtoType = 91,      ///< EPROTOTYPE
    NoProtoOpt = 92,     ///< ENOPROTOOPT
    ProtoNoSupport = 93, ///< EPROTONOSUPPORT
    OpNotSupp = 95,      ///< EOPNOTSUPP
    AfNoSupport = 97,    ///< EAFNOSUPPORT
    AddrInUse = 98,      ///< EADDRINUSE
    AddrNotAvail = 99,   ///< EADDRNOTAVAIL
    NetDown = 100,       ///< ENETDOWN
    NetUnreach = 101,    ///< ENETUNREACH
    ConnReset = 104,     ///< ECONNRESET
    NoBufs = 105,        ///< ENOBUFS
    IsConn = 106,        ///< EISCONN
    NotConn = 107,       ///< ENOTCONN
    TimedOut = 110,      ///< ETIMEDOUT
    ConnRefused = 111,   ///< ECONNREFUSED
    InProgress = 115,    ///< EINPROGRESS
    Already = 114,       ///< EALREADY
};

// =============================================================================
// BSD IPC Command IDs
// =============================================================================

/**
 * @brief BSD service IPC command IDs
 *
 * These are the command IDs for the bsd:u and bsd:s services.
 * Reference: https://switchbrew.org/wiki/Sockets_services
 */
enum class BsdCommand : uint32_t {
    RegisterClient = 0,
    StartMonitoring = 1,
    Socket = 2,
    SocketExempt = 3,
    Open = 4,
    Select = 5,
    Poll = 6,
    Sysctl = 7,
    Recv = 8,
    RecvFrom = 9,
    Send = 10,
    SendTo = 11,
    Accept = 12,
    Bind = 13,
    Connect = 14,
    GetPeerName = 15,
    GetSockName = 16,
    GetSockOpt = 17,
    Listen = 18,
    Ioctl = 19,
    Fcntl = 20,
    SetSockOpt = 21,
    Shutdown = 22,
    ShutdownAllSockets = 23,
    Write = 24,
    Read = 25,
    Close = 26,
    DuplicateSocket = 27,
    // [4.0.0+]
    GetResourceStatistics = 28,
    // [7.0.0+]
    RecvMMsg = 29,
    SendMMsg = 30,
    // [7.0.0+]
    EventFd = 31,
    // [15.0.0+]
    RegisterResourceStatisticsName = 32,
};

} // namespace ryu_ldn::bsd
