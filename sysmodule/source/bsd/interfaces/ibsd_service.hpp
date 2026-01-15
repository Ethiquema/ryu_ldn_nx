/**
 * @file ibsd_service.hpp
 * @brief BSD Socket Service interface definition (bsd:u / bsd:s)
 *
 * This file defines the IPC interface for the BSD socket service,
 * which we intercept to proxy LDN traffic through the RyuLdn server.
 *
 * ## MITM Strategy
 *
 * We intercept bsd:u to detect and proxy sockets that:
 * 1. Are bound to LDN addresses (10.114.x.x)
 * 2. Connect to LDN addresses
 * 3. Send/receive data to/from LDN addresses
 *
 * Non-LDN sockets are forwarded to the real BSD service transparently.
 *
 * ## Command Reference
 *
 * https://switchbrew.org/wiki/Sockets_services
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include "../bsd_types.hpp"

/**
 * @brief Interface definition for IBsdService
 *
 * This macro defines all the IPC commands for the BSD socket service.
 * We only implement the commands needed for socket operations.
 *
 * Buffer types used:
 * - InBuffer (0x5): Input buffer from client
 * - OutBuffer (0x6): Output buffer to client
 * - InAutoSelectBuffer: Automatically selected input buffer
 * - OutAutoSelectBuffer: Automatically selected output buffer
 */
#define AMS_RYU_BSD_IBSD_SERVICE(C, H)                                                                                  \
    /* Cmd 0: RegisterClient - Initialize socket library for client */                                                  \
    AMS_SF_METHOD_INFO(C, H, 0,   Result, RegisterClient,                                                               \
        (ams::sf::Out<s32> out_errno, u32 config_size,                                                                  \
         const ams::sf::InAutoSelectBuffer &config,                                                                     \
         const ams::sf::ClientProcessId &client_pid,                                                                    \
         ams::sf::CopyHandle &&transfer_memory),                                                                        \
        (out_errno, config_size, config, client_pid, std::move(transfer_memory)))                                       \
    /* Cmd 1: StartMonitoring - Start socket monitoring */                                                              \
    AMS_SF_METHOD_INFO(C, H, 1,   Result, StartMonitoring,                                                              \
        (ams::sf::Out<s32> out_errno, u64 pid),                                                                         \
        (out_errno, pid))                                                                                               \
    /* Cmd 2: Socket - Create a socket */                                                                               \
    AMS_SF_METHOD_INFO(C, H, 2,   Result, Socket,                                                                       \
        (ams::sf::Out<s32> out_errno, ams::sf::Out<s32> out_fd,                                                         \
         s32 domain, s32 type, s32 protocol),                                                                           \
        (out_errno, out_fd, domain, type, protocol))                                                                    \
    /* Cmd 3: SocketExempt - Create an exempt socket */                                                                 \
    AMS_SF_METHOD_INFO(C, H, 3,   Result, SocketExempt,                                                                 \
        (ams::sf::Out<s32> out_errno, ams::sf::Out<s32> out_fd,                                                         \
         s32 domain, s32 type, s32 protocol),                                                                           \
        (out_errno, out_fd, domain, type, protocol))                                                                    \
    /* Cmd 4: Open - Open a device (limited to /dev/bpf) */                                                             \
    AMS_SF_METHOD_INFO(C, H, 4,   Result, Open,                                                                         \
        (ams::sf::Out<s32> out_errno, ams::sf::Out<s32> out_fd,                                                         \
         const ams::sf::InBuffer &path),                                                                                \
        (out_errno, out_fd, path))                                                                                      \
    /* Cmd 5: Select - Wait for socket activity */                                                                      \
    AMS_SF_METHOD_INFO(C, H, 5,   Result, Select,                                                                       \
        (ams::sf::Out<s32> out_errno, ams::sf::Out<s32> out_count,                                                      \
         s32 nfds, const ams::sf::InAutoSelectBuffer &readfds_in,                                                       \
         const ams::sf::InAutoSelectBuffer &writefds_in,                                                                \
         const ams::sf::InAutoSelectBuffer &errorfds_in,                                                                \
         const ams::sf::InAutoSelectBuffer &timeout,                                                                    \
         ams::sf::OutAutoSelectBuffer readfds_out,                                                                      \
         ams::sf::OutAutoSelectBuffer writefds_out,                                                                     \
         ams::sf::OutAutoSelectBuffer errorfds_out),                                                                    \
        (out_errno, out_count, nfds, readfds_in, writefds_in, errorfds_in, timeout,                                     \
         readfds_out, writefds_out, errorfds_out))                                                                      \
    /* Cmd 6: Poll - Poll for socket events */                                                                          \
    AMS_SF_METHOD_INFO(C, H, 6,   Result, Poll,                                                                         \
        (ams::sf::Out<s32> out_errno, ams::sf::Out<s32> out_count,                                                      \
         const ams::sf::InAutoSelectBuffer &fds_in,                                                                     \
         ams::sf::OutAutoSelectBuffer fds_out,                                                                          \
         s32 nfds, s32 timeout),                                                                                        \
        (out_errno, out_count, fds_in, fds_out, nfds, timeout))                                                         \
    /* Cmd 7: Sysctl - System control */                                                                                \
    AMS_SF_METHOD_INFO(C, H, 7,   Result, Sysctl,                                                                       \
        (ams::sf::Out<s32> out_errno,                                                                                   \
         const ams::sf::InBuffer &name,                                                                                 \
         const ams::sf::InBuffer &old_val_in,                                                                           \
         ams::sf::OutBuffer old_val_out,                                                                                \
         const ams::sf::InBuffer &new_val),                                                                             \
        (out_errno, name, old_val_in, old_val_out, new_val))                                                            \
    /* Cmd 8: Recv - Receive from connected socket */                                                                   \
    AMS_SF_METHOD_INFO(C, H, 8,   Result, Recv,                                                                         \
        (ams::sf::Out<s32> out_errno, ams::sf::Out<s32> out_size,                                                       \
         s32 fd, s32 flags,                                                                                             \
         ams::sf::OutAutoSelectBuffer buffer),                                                                          \
        (out_errno, out_size, fd, flags, buffer))                                                                       \
    /* Cmd 9: RecvFrom - Receive with source address */                                                                 \
    AMS_SF_METHOD_INFO(C, H, 9,   Result, RecvFrom,                                                                     \
        (ams::sf::Out<s32> out_errno, ams::sf::Out<s32> out_size,                                                       \
         s32 fd, s32 flags,                                                                                             \
         ams::sf::OutAutoSelectBuffer buffer,                                                                           \
         ams::sf::OutAutoSelectBuffer addr_out),                                                                        \
        (out_errno, out_size, fd, flags, buffer, addr_out))                                                             \
    /* Cmd 10: Send - Send to connected socket */                                                                       \
    AMS_SF_METHOD_INFO(C, H, 10,  Result, Send,                                                                         \
        (ams::sf::Out<s32> out_errno, ams::sf::Out<s32> out_size,                                                       \
         s32 fd, s32 flags,                                                                                             \
         const ams::sf::InAutoSelectBuffer &buffer),                                                                    \
        (out_errno, out_size, fd, flags, buffer))                                                                       \
    /* Cmd 11: SendTo - Send with destination address */                                                                \
    AMS_SF_METHOD_INFO(C, H, 11,  Result, SendTo,                                                                       \
        (ams::sf::Out<s32> out_errno, ams::sf::Out<s32> out_size,                                                       \
         s32 fd, s32 flags,                                                                                             \
         const ams::sf::InAutoSelectBuffer &buffer,                                                                     \
         const ams::sf::InAutoSelectBuffer &addr),                                                                      \
        (out_errno, out_size, fd, flags, buffer, addr))                                                                 \
    /* Cmd 12: Accept - Accept connection on listening socket */                                                        \
    AMS_SF_METHOD_INFO(C, H, 12,  Result, Accept,                                                                       \
        (ams::sf::Out<s32> out_errno, ams::sf::Out<s32> out_fd,                                                         \
         s32 fd,                                                                                                        \
         ams::sf::OutAutoSelectBuffer addr_out),                                                                        \
        (out_errno, out_fd, fd, addr_out))                                                                              \
    /* Cmd 13: Bind - Bind socket to address */                                                                         \
    AMS_SF_METHOD_INFO(C, H, 13,  Result, Bind,                                                                         \
        (ams::sf::Out<s32> out_errno,                                                                                   \
         s32 fd,                                                                                                        \
         const ams::sf::InAutoSelectBuffer &addr),                                                                      \
        (out_errno, fd, addr))                                                                                          \
    /* Cmd 14: Connect - Connect to remote address */                                                                   \
    AMS_SF_METHOD_INFO(C, H, 14,  Result, Connect,                                                                      \
        (ams::sf::Out<s32> out_errno,                                                                                   \
         s32 fd,                                                                                                        \
         const ams::sf::InAutoSelectBuffer &addr),                                                                      \
        (out_errno, fd, addr))                                                                                          \
    /* Cmd 15: GetPeerName - Get address of connected peer */                                                           \
    AMS_SF_METHOD_INFO(C, H, 15,  Result, GetPeerName,                                                                  \
        (ams::sf::Out<s32> out_errno,                                                                                   \
         s32 fd,                                                                                                        \
         ams::sf::OutAutoSelectBuffer addr_out),                                                                        \
        (out_errno, fd, addr_out))                                                                                      \
    /* Cmd 16: GetSockName - Get local address of socket */                                                             \
    AMS_SF_METHOD_INFO(C, H, 16,  Result, GetSockName,                                                                  \
        (ams::sf::Out<s32> out_errno,                                                                                   \
         s32 fd,                                                                                                        \
         ams::sf::OutAutoSelectBuffer addr_out),                                                                        \
        (out_errno, fd, addr_out))                                                                                      \
    /* Cmd 17: GetSockOpt - Get socket option */                                                                        \
    AMS_SF_METHOD_INFO(C, H, 17,  Result, GetSockOpt,                                                                   \
        (ams::sf::Out<s32> out_errno,                                                                                   \
         s32 fd, s32 level, s32 optname,                                                                                \
         ams::sf::OutAutoSelectBuffer optval),                                                                          \
        (out_errno, fd, level, optname, optval))                                                                        \
    /* Cmd 18: Listen - Listen for connections */                                                                       \
    AMS_SF_METHOD_INFO(C, H, 18,  Result, Listen,                                                                       \
        (ams::sf::Out<s32> out_errno,                                                                                   \
         s32 fd, s32 backlog),                                                                                          \
        (out_errno, fd, backlog))                                                                                       \
    /* Cmd 19: Ioctl - I/O control */                                                                                   \
    AMS_SF_METHOD_INFO(C, H, 19,  Result, Ioctl,                                                                        \
        (ams::sf::Out<s32> out_errno, ams::sf::Out<s32> out_result,                                                     \
         s32 fd, u32 request, u32 bufcount,                                                                             \
         const ams::sf::InAutoSelectBuffer &buf_in,                                                                     \
         ams::sf::OutAutoSelectBuffer buf_out),                                                                         \
        (out_errno, out_result, fd, request, bufcount, buf_in, buf_out))                                                \
    /* Cmd 20: Fcntl - File control */                                                                                  \
    AMS_SF_METHOD_INFO(C, H, 20,  Result, Fcntl,                                                                        \
        (ams::sf::Out<s32> out_errno, ams::sf::Out<s32> out_result,                                                     \
         s32 fd, s32 cmd, s32 arg),                                                                                     \
        (out_errno, out_result, fd, cmd, arg))                                                                          \
    /* Cmd 21: SetSockOpt - Set socket option */                                                                        \
    AMS_SF_METHOD_INFO(C, H, 21,  Result, SetSockOpt,                                                                   \
        (ams::sf::Out<s32> out_errno,                                                                                   \
         s32 fd, s32 level, s32 optname,                                                                                \
         const ams::sf::InAutoSelectBuffer &optval),                                                                    \
        (out_errno, fd, level, optname, optval))                                                                        \
    /* Cmd 22: Shutdown - Shutdown socket */                                                                            \
    AMS_SF_METHOD_INFO(C, H, 22,  Result, Shutdown,                                                                     \
        (ams::sf::Out<s32> out_errno,                                                                                   \
         s32 fd, s32 how),                                                                                              \
        (out_errno, fd, how))                                                                                           \
    /* Cmd 23: ShutdownAllSockets - Shutdown all sockets for PID */                                                     \
    AMS_SF_METHOD_INFO(C, H, 23,  Result, ShutdownAllSockets,                                                           \
        (ams::sf::Out<s32> out_errno,                                                                                   \
         u64 pid, s32 how),                                                                                             \
        (out_errno, pid, how))                                                                                          \
    /* Cmd 24: Write - Write to socket */                                                                               \
    AMS_SF_METHOD_INFO(C, H, 24,  Result, Write,                                                                        \
        (ams::sf::Out<s32> out_errno, ams::sf::Out<s32> out_size,                                                       \
         s32 fd,                                                                                                        \
         const ams::sf::InAutoSelectBuffer &buffer),                                                                    \
        (out_errno, out_size, fd, buffer))                                                                              \
    /* Cmd 25: Read - Read from socket */                                                                               \
    AMS_SF_METHOD_INFO(C, H, 25,  Result, Read,                                                                         \
        (ams::sf::Out<s32> out_errno, ams::sf::Out<s32> out_size,                                                       \
         s32 fd,                                                                                                        \
         ams::sf::OutAutoSelectBuffer buffer),                                                                          \
        (out_errno, out_size, fd, buffer))                                                                              \
    /* Cmd 26: Close - Close socket */                                                                                  \
    AMS_SF_METHOD_INFO(C, H, 26,  Result, Close,                                                                        \
        (ams::sf::Out<s32> out_errno,                                                                                   \
         s32 fd),                                                                                                       \
        (out_errno, fd))                                                                                                \
    /* Cmd 27: DuplicateSocket - Duplicate socket for another PID */                                                    \
    AMS_SF_METHOD_INFO(C, H, 27,  Result, DuplicateSocket,                                                              \
        (ams::sf::Out<s32> out_errno, ams::sf::Out<s32> out_fd,                                                         \
         s32 fd, u64 target_pid),                                                                                       \
        (out_errno, out_fd, fd, target_pid))

// Define the interface with a unique ID
AMS_SF_DEFINE_INTERFACE(ams::mitm::bsd, IBsdService, AMS_RYU_BSD_IBSD_SERVICE, 0xB5D50C81)

namespace ams::mitm::bsd {

/**
 * @brief Check if we should intercept BSD calls for this program
 *
 * We only intercept games that might use LDN multiplayer.
 * For now, we always intercept to be safe.
 */
inline bool ShouldInterceptBsd(const sm::MitmProcessInfo& client_info) {
    // Always intercept for now - we'll forward non-LDN calls
    // to the real service anyway
    return true;
}

} // namespace ams::mitm::bsd
