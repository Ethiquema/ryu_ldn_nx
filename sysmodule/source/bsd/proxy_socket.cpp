/**
 * @file proxy_socket.cpp
 * @brief Implementation of the Proxy Socket for LDN Traffic Routing
 *
 * This file implements the ProxySocket class. Data transfer operations
 * (Send/Recv) interact with the ProxySocketManager to encode/decode
 * ProxyData packets.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "proxy_socket.hpp"
#include "proxy_socket_manager.hpp"

namespace ams::mitm::bsd {

// =============================================================================
// BSD Error Codes (from bsd_types.hpp)
// =============================================================================

using Errno = ryu_ldn::bsd::BsdErrno;

// =============================================================================
// Construction / Destruction
// =============================================================================

ProxySocket::ProxySocket(ryu_ldn::bsd::SocketType type, ryu_ldn::bsd::ProtocolType protocol)
    : m_type(type)
    , m_protocol(protocol)
    , m_state(ProxySocketState::Created)
    , m_non_blocking(false)
    , m_shutdown_read(false)
    , m_shutdown_write(false)
{
    // Initialize addresses to zero
    std::memset(&m_local_addr, 0, sizeof(m_local_addr));
    std::memset(&m_remote_addr, 0, sizeof(m_remote_addr));

    // Set address family
    m_local_addr.sin_family = static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet);
    m_local_addr.sin_len = sizeof(ryu_ldn::bsd::SockAddrIn);
    m_remote_addr.sin_family = static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet);
    m_remote_addr.sin_len = sizeof(ryu_ldn::bsd::SockAddrIn);
}

ProxySocket::~ProxySocket() {
    // Just cleanup, don't send disconnect (should have called Close())
    m_state = ProxySocketState::Closed;
}

// =============================================================================
// Address Management
// =============================================================================

Result ProxySocket::Bind(const ryu_ldn::bsd::SockAddrIn& addr) {
    // Check state
    if (m_state != ProxySocketState::Created) {
        // Already bound or in invalid state
        R_THROW(static_cast<s32>(Errno::Inval));
    }

    // Validate address family
    if (addr.sin_family != static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet)) {
        R_THROW(static_cast<s32>(Errno::AfNoSupport));
    }

    // Store local address
    m_local_addr = addr;
    m_state = ProxySocketState::Bound;

    R_SUCCEED();
}

Result ProxySocket::Connect(const ryu_ldn::bsd::SockAddrIn& addr) {
    // Check state - must be at least created (can connect unbound socket)
    if (m_state == ProxySocketState::Closed) {
        R_THROW(static_cast<s32>(Errno::BadF));
    }

    // If not bound yet, auto-bind will happen with ephemeral port
    // (caller should have done this before calling Connect)

    // Validate address family
    if (addr.sin_family != static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet)) {
        R_THROW(static_cast<s32>(Errno::AfNoSupport));
    }

    // Store remote address
    m_remote_addr = addr;
    m_state = ProxySocketState::Connected;

    // TODO: For TCP, send ProxyConnect packet to server
    // This will be implemented when we integrate with ProxySocketManager

    R_SUCCEED();
}

Result ProxySocket::GetSockName(ryu_ldn::bsd::SockAddrIn* out_addr) const {
    if (out_addr == nullptr) {
        R_THROW(static_cast<s32>(Errno::Fault));
    }

    if (m_state == ProxySocketState::Created) {
        // Not bound yet, return zeroed address
        std::memset(out_addr, 0, sizeof(*out_addr));
        out_addr->sin_family = static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet);
        out_addr->sin_len = sizeof(ryu_ldn::bsd::SockAddrIn);
    } else {
        *out_addr = m_local_addr;
    }

    R_SUCCEED();
}

Result ProxySocket::GetPeerName(ryu_ldn::bsd::SockAddrIn* out_addr) const {
    if (out_addr == nullptr) {
        R_THROW(static_cast<s32>(Errno::Fault));
    }

    if (m_state != ProxySocketState::Connected) {
        R_THROW(static_cast<s32>(Errno::NotConn));
    }

    *out_addr = m_remote_addr;
    R_SUCCEED();
}

// =============================================================================
// Data Transfer
// =============================================================================

s32 ProxySocket::Send(const void* data, size_t len, s32 flags) {
    // Must be connected for Send()
    if (m_state != ProxySocketState::Connected) {
        return -static_cast<s32>(Errno::NotConn);
    }

    // Use SendTo with connected remote address
    return SendTo(data, len, flags, m_remote_addr);
}

s32 ProxySocket::SendTo(const void* data, size_t len, s32 flags, const ryu_ldn::bsd::SockAddrIn& dest) {
    AMS_UNUSED(flags);

    // Check write shutdown
    if (m_shutdown_write) {
        return -static_cast<s32>(Errno::Inval);
    }

    // Check socket state
    if (m_state == ProxySocketState::Closed) {
        return -static_cast<s32>(Errno::BadF);
    }

    // Must be at least bound to send
    if (m_state == ProxySocketState::Created) {
        return -static_cast<s32>(Errno::DestAddrReq);
    }

    // Validate data
    if (data == nullptr && len > 0) {
        return -static_cast<s32>(Errno::Fault);
    }

    // Check max payload size
    if (len > PROXY_SOCKET_MAX_PAYLOAD) {
        return -static_cast<s32>(Errno::MsgSize);
    }

    // Send via ProxySocketManager which routes to LDN server
    // Extract addresses in host byte order for the manager
    uint32_t source_ip = m_local_addr.GetAddr();
    uint16_t source_port = m_local_addr.GetPort();
    uint32_t dest_ip = dest.GetAddr();
    uint16_t dest_port = dest.GetPort();

    auto& manager = ProxySocketManager::GetInstance();
    bool sent = manager.SendProxyData(source_ip, source_port, dest_ip, dest_port,
                                       m_protocol, data, len);

    if (!sent) {
        // No send callback registered or send failed
        // Return ENETUNREACH to indicate network is unreachable
        return -static_cast<s32>(Errno::NetUnreach);
    }

    return static_cast<s32>(len);
}

s32 ProxySocket::Recv(void* buffer, size_t len, s32 flags) {
    // For connected sockets, use RecvFrom and discard the address
    if (m_state == ProxySocketState::Connected) {
        return RecvFrom(buffer, len, flags, nullptr);
    }

    // For unconnected sockets, Recv requires a default peer (connected state)
    return -static_cast<s32>(Errno::NotConn);
}

s32 ProxySocket::RecvFrom(void* buffer, size_t len, s32 flags, ryu_ldn::bsd::SockAddrIn* from) {
    // Check read shutdown
    if (m_shutdown_read) {
        return 0; // EOF
    }

    // Check socket state
    if (m_state == ProxySocketState::Closed) {
        return -static_cast<s32>(Errno::BadF);
    }

    // Validate buffer
    if (buffer == nullptr && len > 0) {
        return -static_cast<s32>(Errno::Fault);
    }

    bool peek = (flags & 0x2) != 0; // MSG_PEEK = 0x2
    bool dontwait = (flags & 0x40) != 0 || m_non_blocking; // MSG_DONTWAIT = 0x40

    // Try to get data from queue
    {
        std::scoped_lock lock(m_queue_mutex);

        if (m_receive_queue.empty()) {
            if (dontwait) {
                // Non-blocking and no data
                return -static_cast<s32>(Errno::Again);
            }
        } else {
            // Data available - get packet
            ReceivedPacket packet = PopFrontPacket(peek);

            // Copy data to buffer
            size_t copy_len = std::min(len, packet.data.size());
            std::memcpy(buffer, packet.data.data(), copy_len);

            // Set source address if requested
            if (from != nullptr) {
                *from = packet.from;
            }

            // Clear event if queue is now empty
            if (m_receive_queue.empty()) {
                m_receive_event.Clear();
            }

            return static_cast<s32>(copy_len);
        }
    }

    // Blocking wait for data
    if (!dontwait) {
        // Wait for receive event
        m_receive_event.Wait();

        // Try again after waking up
        std::scoped_lock lock(m_queue_mutex);

        if (!m_receive_queue.empty()) {
            ReceivedPacket packet = PopFrontPacket(peek);

            size_t copy_len = std::min(len, packet.data.size());
            std::memcpy(buffer, packet.data.data(), copy_len);

            if (from != nullptr) {
                *from = packet.from;
            }

            if (m_receive_queue.empty()) {
                m_receive_event.Clear();
            }

            return static_cast<s32>(copy_len);
        }
    }

    // No data and non-blocking
    return -static_cast<s32>(Errno::Again);
}

void ProxySocket::IncomingData(const void* data, size_t len, const ryu_ldn::bsd::SockAddrIn& from) {
    std::scoped_lock lock(m_queue_mutex);

    // Drop if queue is full (UDP behavior)
    if (m_receive_queue.size() >= PROXY_SOCKET_MAX_QUEUE_SIZE) {
        m_receive_queue.pop_front(); // Drop oldest
    }

    // Create packet and add to queue
    ReceivedPacket packet;
    packet.data.resize(len);
    if (len > 0 && data != nullptr) {
        std::memcpy(packet.data.data(), data, len);
    }
    packet.from = from;

    m_receive_queue.push_back(std::move(packet));

    // Signal that data is available
    m_receive_event.Signal();
}

ReceivedPacket ProxySocket::PopFrontPacket(bool peek) {
    // Caller must hold m_queue_mutex
    if (m_receive_queue.empty()) {
        return {};
    }

    if (peek) {
        // Return copy, don't remove
        return m_receive_queue.front();
    } else {
        // Move out and remove
        ReceivedPacket packet = std::move(m_receive_queue.front());
        m_receive_queue.pop_front();
        return packet;
    }
}

// =============================================================================
// Socket Options
// =============================================================================

Result ProxySocket::SetSockOpt(s32 level, s32 optname, const void* optval, size_t optlen) {
    AMS_UNUSED(level, optname, optval, optlen);

    // Most socket options are stored locally but don't affect real behavior
    // since we're proxying through the server
    // TODO: Store options if needed for specific games

    // Accept but ignore most options
    R_SUCCEED();
}

Result ProxySocket::GetSockOpt(s32 level, s32 optname, void* optval, size_t* optlen) const {
    if (optval == nullptr || optlen == nullptr) {
        R_THROW(static_cast<s32>(Errno::Fault));
    }

    // Handle specific options
    if (level == static_cast<s32>(ryu_ldn::bsd::SocketOptionLevel::Socket)) {
        switch (static_cast<ryu_ldn::bsd::SocketOption>(optname)) {
            case ryu_ldn::bsd::SocketOption::Type:
                // SO_TYPE - return socket type
                if (*optlen >= sizeof(s32)) {
                    *reinterpret_cast<s32*>(optval) = static_cast<s32>(m_type);
                    *optlen = sizeof(s32);
                    R_SUCCEED();
                }
                break;

            case ryu_ldn::bsd::SocketOption::Error:
                // SO_ERROR - return 0 (no error)
                if (*optlen >= sizeof(s32)) {
                    *reinterpret_cast<s32*>(optval) = 0;
                    *optlen = sizeof(s32);
                    R_SUCCEED();
                }
                break;

            default:
                break;
        }
    }

    // Unknown option - return not supported
    R_THROW(static_cast<s32>(Errno::NoProtoOpt));
}

// =============================================================================
// TCP-specific Operations
// =============================================================================

Result ProxySocket::Listen(s32 backlog) {
    AMS_UNUSED(backlog);

    // Must be TCP
    if (m_type != ryu_ldn::bsd::SocketType::Stream) {
        R_THROW(static_cast<s32>(Errno::OpNotSupp));
    }

    // Must be bound
    if (m_state == ProxySocketState::Created) {
        R_THROW(static_cast<s32>(Errno::Inval));
    }

    m_state = ProxySocketState::Listening;
    R_SUCCEED();
}

std::unique_ptr<ProxySocket> ProxySocket::Accept(ryu_ldn::bsd::SockAddrIn* out_addr) {
    // Must be listening
    if (m_state != ProxySocketState::Listening) {
        return nullptr;
    }

    // TODO: Implement TCP accept from accept queue
    // For now, LDN games primarily use UDP

    AMS_UNUSED(out_addr);
    return nullptr;
}

// =============================================================================
// Shutdown and Close
// =============================================================================

Result ProxySocket::Shutdown(ryu_ldn::bsd::ShutdownHow how) {
    switch (how) {
        case ryu_ldn::bsd::ShutdownHow::Read:
            m_shutdown_read = true;
            break;
        case ryu_ldn::bsd::ShutdownHow::Write:
            m_shutdown_write = true;
            break;
        case ryu_ldn::bsd::ShutdownHow::Both:
            m_shutdown_read = true;
            m_shutdown_write = true;
            break;
        default:
            R_THROW(static_cast<s32>(Errno::Inval));
    }

    // Signal any blocked receivers
    m_receive_event.Signal();

    R_SUCCEED();
}

Result ProxySocket::Close() {
    m_state = ProxySocketState::Closed;
    m_shutdown_read = true;
    m_shutdown_write = true;

    // Signal any blocked receivers
    m_receive_event.Signal();

    // Clear queues
    {
        std::scoped_lock lock(m_queue_mutex);
        m_receive_queue.clear();
    }

    // TODO: For TCP, send ProxyDisconnect to server

    R_SUCCEED();
}

// =============================================================================
// Event Handling
// =============================================================================

bool ProxySocket::HasPendingData() const {
    std::scoped_lock lock(m_queue_mutex);
    return !m_receive_queue.empty();
}

size_t ProxySocket::GetPendingDataSize() const {
    std::scoped_lock lock(m_queue_mutex);
    size_t total = 0;
    for (const auto& packet : m_receive_queue) {
        total += packet.data.size();
    }
    return total;
}

bool ProxySocket::WaitForData(u64 timeout_ms) {
    if (HasPendingData()) {
        return true;
    }

    if (timeout_ms == 0) {
        m_receive_event.Wait();
        return true;
    } else {
        return m_receive_event.TimedWait(TimeSpan::FromMilliSeconds(timeout_ms));
    }
}

} // namespace ams::mitm::bsd
