/**
 * @file ephemeral_port_pool.cpp
 * @brief Implementation of the Ephemeral Port Pool
 *
 * This file implements the thread-safe ephemeral port allocator.
 * See ephemeral_port_pool.hpp for design documentation.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ephemeral_port_pool.hpp"

namespace ams::mitm::bsd {

// =============================================================================
// Bitset Access Helpers
// =============================================================================

std::bitset<EPHEMERAL_PORT_COUNT>& EphemeralPortPool::GetBitset(ryu_ldn::bsd::ProtocolType protocol) {
    switch (protocol) {
        case ryu_ldn::bsd::ProtocolType::Udp:
            return m_udp_ports;
        case ryu_ldn::bsd::ProtocolType::Tcp:
            return m_tcp_ports;
        default:
            // For unspecified or other protocols, use UDP pool
            return m_udp_ports;
    }
}

const std::bitset<EPHEMERAL_PORT_COUNT>& EphemeralPortPool::GetBitset(ryu_ldn::bsd::ProtocolType protocol) const {
    switch (protocol) {
        case ryu_ldn::bsd::ProtocolType::Udp:
            return m_udp_ports;
        case ryu_ldn::bsd::ProtocolType::Tcp:
            return m_tcp_ports;
        default:
            return m_udp_ports;
    }
}

// =============================================================================
// Port Allocation
// =============================================================================

uint16_t EphemeralPortPool::AllocatePort(ryu_ldn::bsd::ProtocolType protocol) {
    std::scoped_lock lock(m_mutex);

    auto& bitset = GetBitset(protocol);
    size_t& hint = (protocol == ryu_ldn::bsd::ProtocolType::Tcp) ? m_tcp_hint : m_udp_hint;

    // Search for an available port starting from the hint (round-robin)
    for (size_t i = 0; i < EPHEMERAL_PORT_COUNT; ++i) {
        size_t index = (hint + i) % EPHEMERAL_PORT_COUNT;

        // Check if port is available (bit is 0)
        if (!bitset.test(index)) {
            // Mark as allocated
            bitset.set(index);

            // Update hint for next allocation (skip to next for round-robin)
            hint = (index + 1) % EPHEMERAL_PORT_COUNT;

            return IndexToPort(index);
        }
    }

    // No ports available
    return 0;
}

bool EphemeralPortPool::AllocateSpecificPort(uint16_t port, ryu_ldn::bsd::ProtocolType protocol) {
    // Validate port is in ephemeral range
    const size_t index = PortToIndex(port);
    if (index >= EPHEMERAL_PORT_COUNT) {
        // Port not in ephemeral range - this is OK for well-known ports
        // We just don't track them in the pool
        return true;
    }

    std::scoped_lock lock(m_mutex);

    auto& bitset = GetBitset(protocol);

    // Check if already allocated
    if (bitset.test(index)) {
        return false; // Port in use
    }

    // Mark as allocated
    bitset.set(index);
    return true;
}

// =============================================================================
// Port Release
// =============================================================================

void EphemeralPortPool::ReleasePort(uint16_t port, ryu_ldn::bsd::ProtocolType protocol) {
    const size_t index = PortToIndex(port);
    if (index >= EPHEMERAL_PORT_COUNT) {
        return; // Port not in ephemeral range, nothing to release
    }

    std::scoped_lock lock(m_mutex);

    auto& bitset = GetBitset(protocol);

    // Clear the bit (mark as available)
    bitset.reset(index);
}

// =============================================================================
// Query Methods
// =============================================================================

bool EphemeralPortPool::IsPortAllocated(uint16_t port, ryu_ldn::bsd::ProtocolType protocol) const {
    const size_t index = PortToIndex(port);
    if (index >= EPHEMERAL_PORT_COUNT) {
        return false; // Port not in ephemeral range
    }

    std::scoped_lock lock(m_mutex);

    const auto& bitset = GetBitset(protocol);
    return bitset.test(index);
}

size_t EphemeralPortPool::GetAvailableCount(ryu_ldn::bsd::ProtocolType protocol) const {
    std::scoped_lock lock(m_mutex);

    const auto& bitset = GetBitset(protocol);

    // Count is total minus allocated
    return EPHEMERAL_PORT_COUNT - bitset.count();
}

void EphemeralPortPool::ReleaseAll() {
    std::scoped_lock lock(m_mutex);

    m_udp_ports.reset();
    m_tcp_ports.reset();
    m_udp_hint = 0;
    m_tcp_hint = 0;
}

} // namespace ams::mitm::bsd
