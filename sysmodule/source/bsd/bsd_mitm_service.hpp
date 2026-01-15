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
    BsdMitmService(std::shared_ptr<::Service>&& s, const sm::MitmProcessInfo& c);

    /**
     * @brief Destructor - cleanup tracked sockets
     */
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
    static bool ShouldMitm(const sm::MitmProcessInfo& client_info);

public:
    // =========================================================================
    // Command Implementations
    // All commands forward to the real service by default
    // =========================================================================

    Result RegisterClient(
        sf::Out<s32> out_errno, u32 config_size,
        const sf::InAutoSelectBuffer& config,
        const sf::ClientProcessId& client_pid,
        sf::CopyHandle&& transfer_memory);

    Result StartMonitoring(sf::Out<s32> out_errno, u64 pid);

    Result Socket(
        sf::Out<s32> out_errno, sf::Out<s32> out_fd,
        s32 domain, s32 type, s32 protocol);

    Result SocketExempt(
        sf::Out<s32> out_errno, sf::Out<s32> out_fd,
        s32 domain, s32 type, s32 protocol);

    Result Open(
        sf::Out<s32> out_errno, sf::Out<s32> out_fd,
        const sf::InBuffer& path);

    Result Select(
        sf::Out<s32> out_errno, sf::Out<s32> out_count,
        s32 nfds, const sf::InAutoSelectBuffer& readfds_in,
        const sf::InAutoSelectBuffer& writefds_in,
        const sf::InAutoSelectBuffer& errorfds_in,
        const sf::InAutoSelectBuffer& timeout,
        sf::OutAutoSelectBuffer readfds_out,
        sf::OutAutoSelectBuffer writefds_out,
        sf::OutAutoSelectBuffer errorfds_out);

    Result Poll(
        sf::Out<s32> out_errno, sf::Out<s32> out_count,
        const sf::InAutoSelectBuffer& fds_in,
        sf::OutAutoSelectBuffer fds_out,
        s32 nfds, s32 timeout);

    Result Sysctl(
        sf::Out<s32> out_errno,
        const sf::InBuffer& name,
        const sf::InBuffer& old_val_in,
        sf::OutBuffer old_val_out,
        const sf::InBuffer& new_val);

    Result Recv(
        sf::Out<s32> out_errno, sf::Out<s32> out_size,
        s32 fd, s32 flags,
        sf::OutAutoSelectBuffer buffer);

    Result RecvFrom(
        sf::Out<s32> out_errno, sf::Out<s32> out_size,
        s32 fd, s32 flags,
        sf::OutAutoSelectBuffer buffer,
        sf::OutAutoSelectBuffer addr_out);

    Result Send(
        sf::Out<s32> out_errno, sf::Out<s32> out_size,
        s32 fd, s32 flags,
        const sf::InAutoSelectBuffer& buffer);

    Result SendTo(
        sf::Out<s32> out_errno, sf::Out<s32> out_size,
        s32 fd, s32 flags,
        const sf::InAutoSelectBuffer& buffer,
        const sf::InAutoSelectBuffer& addr);

    Result Accept(
        sf::Out<s32> out_errno, sf::Out<s32> out_fd,
        s32 fd,
        sf::OutAutoSelectBuffer addr_out);

    Result Bind(
        sf::Out<s32> out_errno,
        s32 fd,
        const sf::InAutoSelectBuffer& addr);

    Result Connect(
        sf::Out<s32> out_errno,
        s32 fd,
        const sf::InAutoSelectBuffer& addr);

    Result GetPeerName(
        sf::Out<s32> out_errno,
        s32 fd,
        sf::OutAutoSelectBuffer addr_out);

    Result GetSockName(
        sf::Out<s32> out_errno,
        s32 fd,
        sf::OutAutoSelectBuffer addr_out);

    Result GetSockOpt(
        sf::Out<s32> out_errno,
        s32 fd, s32 level, s32 optname,
        sf::OutAutoSelectBuffer optval);

    Result Listen(
        sf::Out<s32> out_errno,
        s32 fd, s32 backlog);

    Result Ioctl(
        sf::Out<s32> out_errno, sf::Out<s32> out_result,
        s32 fd, u32 request, u32 bufcount,
        const sf::InAutoSelectBuffer& buf_in,
        sf::OutAutoSelectBuffer buf_out);

    Result Fcntl(
        sf::Out<s32> out_errno, sf::Out<s32> out_result,
        s32 fd, s32 cmd, s32 arg);

    Result SetSockOpt(
        sf::Out<s32> out_errno,
        s32 fd, s32 level, s32 optname,
        const sf::InAutoSelectBuffer& optval);

    Result Shutdown(
        sf::Out<s32> out_errno,
        s32 fd, s32 how);

    Result ShutdownAllSockets(
        sf::Out<s32> out_errno,
        u64 pid, s32 how);

    Result Write(
        sf::Out<s32> out_errno, sf::Out<s32> out_size,
        s32 fd,
        const sf::InAutoSelectBuffer& buffer);

    Result Read(
        sf::Out<s32> out_errno, sf::Out<s32> out_size,
        s32 fd,
        sf::OutAutoSelectBuffer buffer);

    Result Close(
        sf::Out<s32> out_errno,
        s32 fd);

    Result DuplicateSocket(
        sf::Out<s32> out_errno, sf::Out<s32> out_fd,
        s32 fd, u64 target_pid);

private:
    /// Client process ID for this session
    u64 m_client_pid;
};

// Verify interface compliance
static_assert(ams::mitm::bsd::IsIBsdMitmService<BsdMitmService>);

} // namespace ams::mitm::bsd
