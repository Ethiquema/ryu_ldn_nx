/**
 * @file ephemeral_port_pool.hpp
 * @brief Ephemeral Port Pool for BSD Proxy Sockets
 *
 * This file implements a thread-safe ephemeral port allocator for proxy sockets.
 * When games create sockets and bind to port 0 (requesting system-assigned port),
 * we need to assign ports from the ephemeral range (49152-65535).
 *
 * ## Design
 *
 * The pool maintains separate port allocations per protocol (UDP/TCP) since
 * the same port can be used by both protocols simultaneously. Each allocation
 * is tracked in a bitset for O(1) allocation checking and O(n) allocation
 * where n is the range size in worst case.
 *
 * ## Usage
 *
 * ```cpp
 * EphemeralPortPool pool;
 *
 * // Allocate a port for UDP
 * uint16_t port = pool.AllocatePort(ProtocolType::Udp);
 * if (port != 0) {
 *     // Use the port
 * }
 *
 * // Release when done
 * pool.ReleasePort(port, ProtocolType::Udp);
 * ```
 *
 * ## Thread Safety
 *
 * All methods are thread-safe and use a mutex for synchronization.
 * This is necessary because multiple BSD IPC calls may run concurrently.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include <array>
#include <bitset>
#include "bsd_types.hpp"

namespace ams::mitm::bsd {

/**
 * @brief Start of ephemeral port range
 *
 * Standard ephemeral port range starts at 49152 (0xC000).
 * This matches the IANA recommendation and Linux default.
 */
constexpr uint16_t EPHEMERAL_PORT_MIN = 49152;

/**
 * @brief End of ephemeral port range (inclusive)
 *
 * Ephemeral ports go up to 65535 (0xFFFF).
 */
constexpr uint16_t EPHEMERAL_PORT_MAX = 65535;

/**
 * @brief Total number of ephemeral ports available
 *
 * 65535 - 49152 + 1 = 16384 ports
 */
constexpr size_t EPHEMERAL_PORT_COUNT = EPHEMERAL_PORT_MAX - EPHEMERAL_PORT_MIN + 1;

/**
 * @brief Ephemeral Port Pool for Proxy Sockets
 *
 * This class manages allocation of ephemeral ports for proxy sockets.
 * It maintains separate pools for UDP and TCP since ports can be
 * shared between protocols.
 *
 * ## Allocation Strategy
 *
 * Ports are allocated using a simple linear search starting from
 * the last allocated port (round-robin). This provides good distribution
 * and avoids rapid reuse of recently freed ports.
 *
 * ## Memory Usage
 *
 * Uses two bitsets (one per protocol) of 16384 bits each = ~4KB total.
 * This is acceptable for the sysmodule's memory budget.
 */
class EphemeralPortPool {
public:
    /**
     * @brief Default constructor
     *
     * Initializes the port pool with all ports available.
     * Sets the allocation hint to the start of the range.
     */
    EphemeralPortPool() = default;

    /**
     * @brief Destructor
     *
     * No special cleanup needed - bitsets are value types.
     */
    ~EphemeralPortPool() = default;

    /**
     * @brief Non-copyable
     */
    EphemeralPortPool(const EphemeralPortPool&) = delete;
    EphemeralPortPool& operator=(const EphemeralPortPool&) = delete;

    /**
     * @brief Allocate an ephemeral port for the given protocol
     *
     * Searches for an available port starting from the last allocation
     * point (round-robin strategy). This prevents rapid port reuse
     * which can cause issues with TIME_WAIT states.
     *
     * @param protocol The protocol type (TCP or UDP)
     * @return The allocated port number in host byte order, or 0 if none available
     *
     * @note Thread-safe. Uses internal mutex.
     * @note O(n) worst case where n = EPHEMERAL_PORT_COUNT
     */
    uint16_t AllocatePort(ryu_ldn::bsd::ProtocolType protocol);

    /**
     * @brief Allocate a specific port for the given protocol
     *
     * Attempts to allocate a specific port. If the port is already
     * allocated for this protocol, returns false.
     *
     * @param port The port number to allocate (must be in ephemeral range)
     * @param protocol The protocol type (TCP or UDP)
     * @return true if allocation succeeded, false if already in use or invalid
     *
     * @note Thread-safe. Uses internal mutex.
     * @note O(1) operation
     */
    bool AllocateSpecificPort(uint16_t port, ryu_ldn::bsd::ProtocolType protocol);

    /**
     * @brief Release a previously allocated port
     *
     * Returns the port to the pool for future allocation.
     * Releasing an unallocated port is a no-op (not an error).
     *
     * @param port The port number to release (must be in ephemeral range)
     * @param protocol The protocol type (TCP or UDP)
     *
     * @note Thread-safe. Uses internal mutex.
     * @note O(1) operation
     */
    void ReleasePort(uint16_t port, ryu_ldn::bsd::ProtocolType protocol);

    /**
     * @brief Check if a port is currently allocated
     *
     * @param port The port number to check
     * @param protocol The protocol type (TCP or UDP)
     * @return true if the port is allocated, false otherwise
     *
     * @note Thread-safe. Uses internal mutex.
     * @note O(1) operation
     */
    bool IsPortAllocated(uint16_t port, ryu_ldn::bsd::ProtocolType protocol) const;

    /**
     * @brief Get the number of available ports for a protocol
     *
     * @param protocol The protocol type (TCP or UDP)
     * @return Number of ports still available for allocation
     *
     * @note Thread-safe. Uses internal mutex.
     */
    size_t GetAvailableCount(ryu_ldn::bsd::ProtocolType protocol) const;

    /**
     * @brief Release all allocated ports
     *
     * Useful for cleanup when the LDN session ends.
     *
     * @note Thread-safe. Uses internal mutex.
     */
    void ReleaseAll();

private:
    /**
     * @brief Convert port number to bitset index
     *
     * @param port Port number in host byte order
     * @return Index into the bitset, or EPHEMERAL_PORT_COUNT if invalid
     */
    static constexpr size_t PortToIndex(uint16_t port) {
        if (port < EPHEMERAL_PORT_MIN || port > EPHEMERAL_PORT_MAX) {
            return EPHEMERAL_PORT_COUNT; // Invalid index
        }
        return port - EPHEMERAL_PORT_MIN;
    }

    /**
     * @brief Convert bitset index to port number
     *
     * @param index Index into the bitset
     * @return Port number in host byte order
     */
    static constexpr uint16_t IndexToPort(size_t index) {
        return static_cast<uint16_t>(EPHEMERAL_PORT_MIN + index);
    }

    /**
     * @brief Get the bitset for the given protocol
     *
     * @param protocol The protocol type
     * @return Reference to the protocol's bitset
     */
    std::bitset<EPHEMERAL_PORT_COUNT>& GetBitset(ryu_ldn::bsd::ProtocolType protocol);

    /**
     * @brief Get the bitset for the given protocol (const)
     *
     * @param protocol The protocol type
     * @return Const reference to the protocol's bitset
     */
    const std::bitset<EPHEMERAL_PORT_COUNT>& GetBitset(ryu_ldn::bsd::ProtocolType protocol) const;

    /**
     * @brief Mutex for thread safety
     */
    mutable os::Mutex m_mutex{false};

    /**
     * @brief Port allocation bitset for UDP
     *
     * Bit is set (1) if port is allocated, clear (0) if available.
     */
    std::bitset<EPHEMERAL_PORT_COUNT> m_udp_ports{};

    /**
     * @brief Port allocation bitset for TCP
     *
     * Bit is set (1) if port is allocated, clear (0) if available.
     */
    std::bitset<EPHEMERAL_PORT_COUNT> m_tcp_ports{};

    /**
     * @brief Next allocation hint for UDP (round-robin)
     */
    size_t m_udp_hint{0};

    /**
     * @brief Next allocation hint for TCP (round-robin)
     */
    size_t m_tcp_hint{0};
};

} // namespace ams::mitm::bsd
