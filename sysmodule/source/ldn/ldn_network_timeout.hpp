/**
 * @file ldn_network_timeout.hpp
 * @brief Network inactivity timeout manager
 *
 * This class manages automatic disconnection from the server after a period
 * of inactivity to conserve server resources. Mirrors Ryujinx's NetworkTimeout.
 *
 * ## Usage Pattern
 *
 * - RefreshTimeout(): Called after Scan or network disconnection
 * - DisableTimeout(): Called when entering a network (CreateNetwork, Connect)
 * - After timeout expires, the callback disconnects from server
 *
 * ## Ryujinx Compatibility
 *
 * - InactiveTimeout: 6000ms (6 seconds)
 * - Same behavior: disconnect from server when idle
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>

namespace ams::mitm::ldn {

/**
 * @brief Callback type for timeout expiration
 */
using TimeoutCallback = void(*)();

/**
 * @brief Network inactivity timeout manager
 *
 * Manages automatic disconnection from the RyuLdn server after a period
 * of inactivity (no active network session).
 *
 * ## Thread Safety
 *
 * All methods are thread-safe via mutex protection.
 */
class NetworkTimeout {
public:
    /**
     * @brief Default inactive timeout (matches Ryujinx)
     */
    static constexpr uint64_t DEFAULT_IDLE_TIMEOUT_MS = 6000;

    /**
     * @brief Constructor
     * @param idle_timeout_ms Timeout duration in milliseconds
     * @param callback Function to call when timeout expires
     */
    NetworkTimeout(uint64_t idle_timeout_ms, TimeoutCallback callback);

    /**
     * @brief Destructor - cancels any pending timeout
     */
    ~NetworkTimeout();

    // Non-copyable
    NetworkTimeout(const NetworkTimeout&) = delete;
    NetworkTimeout& operator=(const NetworkTimeout&) = delete;

    /**
     * @brief Refresh the timeout (restart the timer)
     *
     * Called after operations that keep the connection alive but don't
     * require being in a network (e.g., Scan, DisconnectNetwork).
     *
     * @return true always (for compatibility)
     */
    bool RefreshTimeout();

    /**
     * @brief Disable the timeout (cancel any pending timer)
     *
     * Called when entering a network session (CreateNetwork, Connect).
     * The connection should stay alive while in a network.
     */
    void DisableTimeout();

    /**
     * @brief Check if timeout has expired
     *
     * Called periodically from update loop to check if timeout
     * has elapsed and the callback should be invoked.
     *
     * @param current_time_ms Current time in milliseconds
     * @return true if timeout expired and callback was invoked
     */
    bool CheckTimeout(uint64_t current_time_ms);

    /**
     * @brief Check if timeout is currently active
     */
    bool IsActive() const;

private:
    uint64_t m_idle_timeout_ms;      ///< Timeout duration
    TimeoutCallback m_callback;       ///< Callback to invoke on timeout
    uint64_t m_timeout_start_ms;     ///< When timeout was started
    bool m_active;                    ///< True if timeout is active
    mutable os::Mutex m_mutex;        ///< Thread safety
};

} // namespace ams::mitm::ldn
