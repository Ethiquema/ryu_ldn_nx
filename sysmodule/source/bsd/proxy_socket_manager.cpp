/**
 * @file proxy_socket_manager.cpp
 * @brief Implementation of the Proxy Socket Manager
 *
 * This file implements the central registry for proxy sockets.
 * See proxy_socket_manager.hpp for design documentation.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "proxy_socket_manager.hpp"
#include "../debug/log.hpp"

namespace ams::mitm::bsd {

// =============================================================================
// Singleton
// =============================================================================

ProxySocketManager& ProxySocketManager::GetInstance() {
    static ProxySocketManager instance;
    return instance;
}

// =============================================================================
// Socket Management
// =============================================================================

ProxySocket* ProxySocketManager::CreateProxySocket(s32 fd, ryu_ldn::bsd::SocketType type,
                                                    ryu_ldn::bsd::ProtocolType protocol) {
    std::scoped_lock lock(m_mutex);

    // Check if fd already has a proxy socket
    if (ProxySocketEntry* existing = FindSocketEntry(fd)) {
        // Already exists - return existing
        return existing->socket.get();
    }

    // Check limit
    if (m_socket_count >= MAX_PROXY_SOCKETS) {
        return nullptr;
    }

    // Find first free slot (fd == INVALID_FD)
    ProxySocketEntry* slot = nullptr;
    for (auto& entry : m_sockets) {
        if (entry.fd == INVALID_FD) {
            slot = &entry;
            break;
        }
    }
    if (slot == nullptr) {
        // Shouldn't happen if m_socket_count is consistent, but guard anyway
        return nullptr;
    }

    // Create new proxy socket
    auto socket = std::make_unique<ProxySocket>(type, protocol);
    ProxySocket* result = socket.get();

    // Add to registry
    slot->fd = fd;
    slot->socket = std::move(socket);
    m_socket_count++;

    return result;
}

ProxySocket* ProxySocketManager::GetProxySocket(s32 fd) {
    std::scoped_lock lock(m_mutex);
    if (ProxySocketEntry* entry = FindSocketEntry(fd)) {
        return entry->socket.get();
    }
    return nullptr;
}

ProxySocketManager::ProxySocketEntry* ProxySocketManager::FindSocketEntry(s32 fd) {
    // Caller must hold m_mutex
    for (auto& entry : m_sockets) {
        if (entry.fd != INVALID_FD && entry.fd == fd) {
            return &entry;
        }
    }
    return nullptr;
}

const ProxySocketManager::ProxySocketEntry* ProxySocketManager::FindSocketEntry(s32 fd) const {
    // Caller must hold m_mutex
    for (const auto& entry : m_sockets) {
        if (entry.fd != INVALID_FD && entry.fd == fd) {
            return &entry;
        }
    }
    return nullptr;
}

bool ProxySocketManager::IsProxySocket(s32 fd) const {
    std::scoped_lock lock(m_mutex);
    return FindSocketEntry(fd) != nullptr;
}

bool ProxySocketManager::CloseProxySocket(s32 fd) {
    std::scoped_lock lock(m_mutex);

    ProxySocketEntry* entry = FindSocketEntry(fd);
    if (entry == nullptr) {
        return false;
    }

    // Get socket info before closing
    ProxySocket* socket = entry->socket.get();
    if (socket != nullptr) {
        // Release the port
        const auto& local_addr = socket->GetLocalAddr();
        if (local_addr.GetPort() != 0) {
            m_port_pool.ReleasePort(local_addr.GetPort(), socket->GetProtocol());
        }

        // Close the socket
        Result close_result = socket->Close();
        if (R_FAILED(close_result)) {
            // Log but continue cleanup - socket will be destroyed regardless
            AMS_UNUSED(close_result);
        }
    }

    // Remove from registry
    entry->fd = INVALID_FD;
    entry->socket.reset();
    if (m_socket_count > 0) {
        m_socket_count--;
    }

    return true;
}

void ProxySocketManager::CloseAllProxySockets() {
    std::scoped_lock lock(m_mutex);

    // Close all sockets
    for (auto& entry : m_sockets) {
        if (entry.fd != INVALID_FD && entry.socket != nullptr) {
            Result close_result = entry.socket->Close();
            if (R_FAILED(close_result)) {
                // Log but continue cleanup - socket will be destroyed regardless
                AMS_UNUSED(close_result);
            }
            entry.fd = INVALID_FD;
            entry.socket.reset();
        }
    }
    m_socket_count = 0;

    // Release all ports
    m_port_pool.ReleaseAll();
}

void ProxySocketManager::Reset() {
    std::scoped_lock lock(m_mutex);

    // Close all proxy sockets
    for (auto& entry : m_sockets) {
        if (entry.fd != INVALID_FD && entry.socket != nullptr) {
            Result close_result = entry.socket->Close();
            AMS_UNUSED(close_result);
            entry.fd = INVALID_FD;
            entry.socket.reset();
        }
    }
    m_socket_count = 0;

    // Release all ports
    m_port_pool.ReleaseAll();

    // Clear pending packets ring (no dealloc — just reset indices)
    m_pending_head = 0;
    m_pending_tail = 0;
    m_pending_count = 0;

    // Reset local IP
    m_local_ip = 0;

    // Clear callbacks
    m_send_callback = nullptr;
    m_proxy_connect_callback = nullptr;
    m_proxy_connect_reply_callback = nullptr;
    m_proxy_disconnect_callback = nullptr;
}

// =============================================================================
// Port Management
// =============================================================================

uint16_t ProxySocketManager::AllocatePort(ryu_ldn::bsd::ProtocolType protocol) {
    return m_port_pool.AllocatePort(protocol);
}

bool ProxySocketManager::ReservePort(uint16_t port, ryu_ldn::bsd::ProtocolType protocol) {
    return m_port_pool.AllocateSpecificPort(port, protocol);
}

void ProxySocketManager::ReleasePort(uint16_t port, ryu_ldn::bsd::ProtocolType protocol) {
    m_port_pool.ReleasePort(port, protocol);
}

// =============================================================================
// Outgoing Data
// =============================================================================

void ProxySocketManager::SetSendCallback(SendProxyDataCallback callback) {
    std::scoped_lock lock(m_mutex);
    m_send_callback = callback;
}

bool ProxySocketManager::SendProxyData(uint32_t source_ip, uint16_t source_port,
                                        uint32_t dest_ip, uint16_t dest_port,
                                        ryu_ldn::bsd::ProtocolType protocol,
                                        const void* data, size_t data_len) {
    SendProxyDataCallback callback;
    {
        std::scoped_lock lock(m_mutex);
        callback = m_send_callback;
    }

    if (callback == nullptr) {
        LOG_WARN("ProxySocketManager::SendProxyData: m_send_callback=nullptr (src=0x%08X:%u dst=0x%08X:%u)",
                 source_ip, source_port, dest_ip, dest_port);
        return false;
    }

    bool ok = callback(source_ip, source_port, dest_ip, dest_port, protocol, data, data_len);
    if (!ok) {
        LOG_WARN("ProxySocketManager::SendProxyData: callback returned false (src=0x%08X:%u dst=0x%08X:%u len=%zu)",
                 source_ip, source_port, dest_ip, dest_port, data_len);
    }
    return ok;
}

void ProxySocketManager::SetProxyConnectCallback(SendProxyConnectCallback callback) {
    std::scoped_lock lock(m_mutex);
    m_proxy_connect_callback = callback;
}

bool ProxySocketManager::SendProxyConnect(uint32_t source_ip, uint16_t source_port,
                                           uint32_t dest_ip, uint16_t dest_port,
                                           ryu_ldn::bsd::ProtocolType protocol) {
    SendProxyConnectCallback callback;
    {
        std::scoped_lock lock(m_mutex);
        callback = m_proxy_connect_callback;
    }

    if (callback == nullptr) {
        return false;
    }

    return callback(source_ip, source_port, dest_ip, dest_port, protocol);
}

void ProxySocketManager::SetProxyConnectReplyCallback(SendProxyConnectReplyCallback callback) {
    std::scoped_lock lock(m_mutex);
    m_proxy_connect_reply_callback = callback;
}

bool ProxySocketManager::SendProxyConnectReply(uint32_t source_ip, uint16_t source_port,
                                                uint32_t dest_ip, uint16_t dest_port,
                                                ryu_ldn::bsd::ProtocolType protocol) {
    SendProxyConnectReplyCallback callback;
    {
        std::scoped_lock lock(m_mutex);
        callback = m_proxy_connect_reply_callback;
    }

    if (callback == nullptr) {
        return false;
    }

    return callback(source_ip, source_port, dest_ip, dest_port, protocol);
}

void ProxySocketManager::SetProxyDisconnectCallback(SendProxyDisconnectCallback callback) {
    std::scoped_lock lock(m_mutex);
    m_proxy_disconnect_callback = callback;
}

bool ProxySocketManager::SendProxyDisconnect(uint32_t source_ip, uint16_t source_port,
                                              uint32_t dest_ip, uint16_t dest_port,
                                              ryu_ldn::bsd::ProtocolType protocol) {
    SendProxyDisconnectCallback callback;
    {
        std::scoped_lock lock(m_mutex);
        callback = m_proxy_disconnect_callback;
    }

    if (callback == nullptr) {
        return false;
    }

    return callback(source_ip, source_port, dest_ip, dest_port, protocol);
}

bool ProxySocketManager::RouteConnectResponse(const ryu_ldn::protocol::ProxyConnectResponse& response) {
    std::scoped_lock lock(m_mutex);

    // Find socket in Connecting state that matches the destination
    uint32_t dest_ip = response.info.source_ipv4;  // Response comes back to our source
    uint16_t dest_port = response.info.source_port;

    for (auto& entry : m_sockets) {
        if (entry.fd == INVALID_FD || entry.socket == nullptr) {
            continue;
        }
        ProxySocket* socket = entry.socket.get();

        // Check if socket is connecting
        if (socket->GetState() != ProxySocketState::Connecting) {
            continue;
        }

        // Check local address matches
        const auto& local_addr = socket->GetLocalAddr();
        if (local_addr.GetAddr() != dest_ip || local_addr.GetPort() != dest_port) {
            continue;
        }

        // Found matching socket - deliver response
        socket->HandleConnectResponse(response);
        return true;
    }

    return false;
}

bool ProxySocketManager::RouteConnectRequest(const ryu_ldn::protocol::ProxyConnectRequest& request) {
    std::scoped_lock lock(m_mutex);

    // Find listening socket that matches the destination
    uint32_t dest_ip = request.info.dest_ipv4;
    uint16_t dest_port = request.info.dest_port;

    for (auto& entry : m_sockets) {
        if (entry.fd == INVALID_FD || entry.socket == nullptr) {
            continue;
        }
        ProxySocket* socket = entry.socket.get();

        // Check if socket is listening
        if (socket->GetState() != ProxySocketState::Listening) {
            continue;
        }

        // Check protocol matches (TCP)
        if (socket->GetProtocol() != ryu_ldn::bsd::ProtocolType::Tcp) {
            continue;
        }

        // Check local address matches destination
        const auto& local_addr = socket->GetLocalAddr();

        // Port must match
        if (local_addr.GetPort() != dest_port) {
            continue;
        }

        // IP can be exact match or INADDR_ANY
        uint32_t local_ip = local_addr.GetAddr();
        if (local_ip != 0 && local_ip != dest_ip) {
            continue;
        }

        // Found matching listener - queue the connection
        socket->IncomingConnection(request);
        return true;
    }

    return false;
}

// =============================================================================
// Data Routing
// =============================================================================

bool ProxySocketManager::RouteIncomingData(uint32_t source_ip, uint16_t source_port,
                                            uint32_t dest_ip, uint16_t dest_port,
                                            ryu_ldn::bsd::ProtocolType protocol,
                                            const void* data, size_t data_len) {
    std::scoped_lock lock(m_mutex);

    // For broadcast/multicast packets, deliver to ALL matching sockets.
    // PIA mesh discovery relies on broadcast UDP reaching every listener
    // on the same port. For unicast, only one socket matches.
    constexpr size_t kMaxSockets = 8;
    ProxySocket* targets[kMaxSockets];
    size_t match_count = FindAllSocketsByDestination(dest_ip, dest_port, protocol,
                                                      targets, kMaxSockets);

    if (match_count == 0) {
        // No matching socket — buffer into fixed-size ring for post-bind delivery.
        if (m_pending_count >= MaxPendingPackets) {
            // Drop oldest
            m_pending_head = (m_pending_head + 1) % MaxPendingPackets;
            m_pending_count--;
        }
        PendingPacket& slot = m_pending_ring[m_pending_tail];
        slot.source_ip = source_ip;
        slot.source_port = source_port;
        slot.dest_ip = dest_ip;
        slot.dest_port = dest_port;
        slot.protocol = protocol;
        size_t copy_len = std::min(data_len, static_cast<size_t>(PendingPayloadMax));
        slot.len = static_cast<uint16_t>(copy_len);
        if (copy_len > 0 && data != nullptr) {
            std::memcpy(slot.data, data, copy_len);
        }
        m_pending_tail = (m_pending_tail + 1) % MaxPendingPackets;
        m_pending_count++;
        return false;
    }

    // Build source address for RecvFrom
    // Note: Nintendo Switch uses BSD-style sockaddr with sin_len field
    // sin_len must be sizeof(SockAddrIn) = 16 for the game to accept the address
    //
    // CRITICAL: Ryujinx stores IPs as uint in "big-endian read" format:
    // For 10.114.0.1, Ryujinx stores 0x0A720001.
    // BUT in BsdSockAddr, sin_addr must be in NETWORK BYTE ORDER (standard BSD).
    // Ryujinx does Array.Reverse() when converting ProxyInfo.SourceIpV4 to IPEndPoint,
    // then copies bytes directly to sin_addr, resulting in network byte order.
    // So we must bswap32 to convert Ryujinx format -> network byte order.
    // For 10.114.0.1: 0x0A720001 -> bswap32 -> 0x0100720A (network order)
    ryu_ldn::bsd::SockAddrIn from_addr{};
    from_addr.sin_len = sizeof(ryu_ldn::bsd::SockAddrIn);
    from_addr.sin_family = static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet);
    from_addr.sin_port = __builtin_bswap16(source_port);
    from_addr.sin_addr = __builtin_bswap32(source_ip);  // Convert Ryujinx format to network byte order

    // Deliver to all matching sockets (critical for broadcast UDP)
    for (size_t i = 0; i < match_count; i++) {
        targets[i]->IncomingData(data, data_len, from_addr);
    }

    return true;
}

ProxySocket* ProxySocketManager::FindSocketByDestination(uint32_t dest_ip, uint16_t dest_port,
                                                          ryu_ldn::bsd::ProtocolType protocol) {
    // Caller must hold m_mutex

    // Check if dest_ip is a broadcast address (ends in .255 or .255.255)
    // LDN subnet is 10.114.x.x with mask 255.255.0.0, so broadcast is 10.114.255.255
    // CRITICAL: dest_ip is in Ryujinx format (Big Endian read as uint32)
    // For 10.114.255.255: first octet (10) in HIGH byte, last octet (255) in LOW byte
    // So 10.114.255.255 = 0x0A72FFFF where:
    //   - 0x0A = 10 (first octet, bits 24-31)
    //   - 0x72 = 114 (second octet, bits 16-23)
    //   - 0xFF = 255 (third octet, bits 8-15)
    //   - 0xFF = 255 (fourth octet, bits 0-7)
    // To check .255 (last octet): mask 0x000000FF
    // To check .255.255 (last two octets): mask 0x0000FFFF
    bool is_broadcast = ((dest_ip & 0x000000FF) == 0x000000FF) ||   // x.x.x.255 in Ryujinx format
                        ((dest_ip & 0x0000FFFF) == 0x0000FFFF);     // x.x.255.255 in Ryujinx format

    for (auto& entry : m_sockets) {
        if (entry.fd == INVALID_FD || entry.socket == nullptr) {
            continue;
        }
        ProxySocket* socket = entry.socket.get();

        // Check protocol matches
        if (socket->GetProtocol() != protocol) {
            continue;
        }

        // Check local address matches destination
        const auto& local_addr = socket->GetLocalAddr();

        // Port must match (GetPort does bswap16, but dest_port is in host order)
        if (local_addr.GetPort() != dest_port) {
            continue;
        }

        // IP matching:
        // CRITICAL: sin_addr is now in Ryujinx format (NO bswap was applied in Bind).
        // Use sin_addr directly instead of GetAddr() which does bswap32.
        // 1. INADDR_ANY (bound to 0.0.0.0 - accepts any destination)
        // 2. Exact match (bound to specific IP)
        // 3. Broadcast: any socket on the same port receives broadcast packets
        uint32_t local_ip = local_addr.sin_addr;  // Direct access - Ryujinx format
        if (local_ip == 0) {
            // Bound to INADDR_ANY - accepts all
            return socket;
        }
        if (local_ip == dest_ip) {
            // Exact match
            return socket;
        }
        if (is_broadcast) {
            // Broadcast packet - deliver to any socket bound on this port
            // Check if socket is in the same subnet (10.114.x.x)
            // In Ryujinx format: 0x0A72xxxx where 0x0A72 is the subnet (10.114)
            // Subnet is in HIGH 16 bits, so mask is 0xFFFF0000
            if ((local_ip & 0xFFFF0000) == (dest_ip & 0xFFFF0000)) {
                return socket;
            }
        }
    }

    return nullptr;
}

size_t ProxySocketManager::FindAllSocketsByDestination(uint32_t dest_ip, uint16_t dest_port,
                                                          ryu_ldn::bsd::ProtocolType protocol,
                                                          ProxySocket* out_sockets[], size_t max_sockets) {
    // Caller must hold m_mutex

    bool is_broadcast = ((dest_ip & 0x000000FF) == 0x000000FF) ||
                        ((dest_ip & 0x0000FFFF) == 0x0000FFFF);

    size_t count = 0;

    for (auto& entry : m_sockets) {
        if (entry.fd == INVALID_FD || entry.socket == nullptr) {
            continue;
        }
        ProxySocket* socket = entry.socket.get();
        if (count >= max_sockets) {
            break;
        }

        if (socket->GetProtocol() != protocol) {
            continue;
        }

        const auto& local_addr = socket->GetLocalAddr();

        if (local_addr.GetPort() != dest_port) {
            continue;
        }

        uint32_t local_ip = local_addr.sin_addr;

        if (local_ip == 0) {
            out_sockets[count++] = socket;
            continue;
        }
        if (local_ip == dest_ip) {
            out_sockets[count++] = socket;
            continue;
        }
        if (is_broadcast) {
            if ((local_ip & 0xFFFF0000) == (dest_ip & 0xFFFF0000)) {
                out_sockets[count++] = socket;
                continue;
            }
        }
    }

    return count;
}

// =============================================================================
// LDN Network Configuration
// =============================================================================

void ProxySocketManager::SetLocalIp(uint32_t ip) {
    std::scoped_lock lock(m_mutex);
    m_local_ip = ip;
}

uint32_t ProxySocketManager::GetLocalIp() const {
    std::scoped_lock lock(m_mutex);
    return m_local_ip;
}

// =============================================================================
// Statistics
// =============================================================================

size_t ProxySocketManager::GetActiveSocketCount() const {
    std::scoped_lock lock(m_mutex);
    return m_socket_count;
}

size_t ProxySocketManager::GetAvailablePortCount(ryu_ldn::bsd::ProtocolType protocol) const {
    return m_port_pool.GetAvailableCount(protocol);
}

// =============================================================================
// Pending Packet Delivery
// =============================================================================

void ProxySocketManager::DeliverPendingPackets(ProxySocket* socket, uint16_t port,
                                                ryu_ldn::bsd::ProtocolType protocol) {
    // Called after bind - deliver any buffered packets matching this socket
    // Caller must NOT hold m_mutex (we take it here)
    std::scoped_lock lock(m_mutex);

    if (socket == nullptr || m_pending_count == 0) {
        return;
    }

    // Walk the ring in arrival order, delivering matches and compacting in place.
    size_t read_idx = m_pending_head;
    size_t write_idx = m_pending_head;
    size_t remaining = m_pending_count;
    size_t kept = 0;

    while (remaining > 0) {
        const PendingPacket& pkt = m_pending_ring[read_idx];

        if (pkt.protocol == protocol && pkt.dest_port == port) {
            ryu_ldn::bsd::SockAddrIn from_addr{};
            from_addr.sin_len = sizeof(ryu_ldn::bsd::SockAddrIn);
            from_addr.sin_family = static_cast<uint8_t>(ryu_ldn::bsd::AddressFamily::Inet);
            from_addr.sin_port = __builtin_bswap16(pkt.source_port);
            from_addr.sin_addr = __builtin_bswap32(pkt.source_ip);  // Ryujinx -> network byte order

            socket->IncomingData(pkt.data, pkt.len, from_addr);
            // Drop this slot by not copying it forward
        } else {
            if (write_idx != read_idx) {
                m_pending_ring[write_idx] = pkt;
            }
            write_idx = (write_idx + 1) % MaxPendingPackets;
            kept++;
        }

        read_idx = (read_idx + 1) % MaxPendingPackets;
        remaining--;
    }

    m_pending_tail = write_idx;
    m_pending_count = kept;
}

} // namespace ams::mitm::bsd
