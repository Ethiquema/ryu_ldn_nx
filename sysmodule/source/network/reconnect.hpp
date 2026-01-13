/**
 * @file reconnect.hpp
 * @brief Reconnection Manager with Exponential Backoff
 *
 * This module implements automatic reconnection logic with exponential
 * backoff for the ryu_ldn_nx network client. It handles retry timing
 * and provides a clean interface for managing reconnection attempts.
 *
 * ## Backoff Algorithm
 *
 * The backoff algorithm uses exponential growth with the following formula:
 *
 * ```
 * delay = min(initial_delay * (multiplier ^ attempt), max_delay)
 * ```
 *
 * With optional jitter to prevent thundering herd:
 *
 * ```
 * jittered_delay = delay * (1.0 + random(-jitter, +jitter))
 * ```
 *
 * ## Default Configuration
 *
 * - Initial delay: 1 second
 * - Multiplier: 2x (doubles each retry)
 * - Maximum delay: 30 seconds (cap)
 * - Jitter: 10% (optional randomization)
 *
 * ## Usage Example
 *
 * ```cpp
 * ReconnectManager reconnect;
 *
 * while (!connected) {
 *     // Wait for backoff period
 *     uint32_t delay_ms = reconnect.get_next_delay_ms();
 *     sleep_ms(delay_ms);
 *
 *     // Attempt connection
 *     if (try_connect()) {
 *         reconnect.reset();  // Reset on success
 *         connected = true;
 *     } else {
 *         reconnect.record_failure();  // Increment backoff
 *     }
 * }
 * ```
 *
 * ## Thread Safety
 *
 * This class is NOT thread-safe. External synchronization is required
 * if accessed from multiple threads.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <cstdint>

namespace ryu_ldn {
namespace network {

/**
 * @brief Configuration for the reconnection manager
 *
 * This structure holds all configurable parameters for the
 * exponential backoff algorithm.
 */
struct ReconnectConfig {
    /**
     * @brief Initial delay before first retry (milliseconds)
     *
     * This is the delay used after the first failure.
     * Default: 1000ms (1 second)
     */
    uint32_t initial_delay_ms;

    /**
     * @brief Maximum delay cap (milliseconds)
     *
     * The delay will never exceed this value, regardless of
     * how many retries have occurred.
     * Default: 30000ms (30 seconds)
     */
    uint32_t max_delay_ms;

    /**
     * @brief Multiplier for exponential growth
     *
     * The delay is multiplied by this factor after each failure.
     * Common values: 2 (doubles), 1.5 (grows by 50%)
     * Stored as fixed-point: 200 = 2.0x, 150 = 1.5x
     * Default: 200 (2.0x multiplier)
     */
    uint16_t multiplier_percent;

    /**
     * @brief Jitter percentage for randomization
     *
     * Random variation added to prevent thundering herd.
     * A value of 10 means +/- 10% variation.
     * Set to 0 to disable jitter.
     * Default: 10 (10% jitter)
     */
    uint8_t jitter_percent;

    /**
     * @brief Maximum number of retry attempts (0 = infinite)
     *
     * If set, the manager will stop allowing retries after
     * this many attempts. Use 0 for infinite retries.
     * Default: 0 (infinite)
     */
    uint16_t max_retries;

    /**
     * @brief Default constructor with sensible defaults
     *
     * Initializes with:
     * - 1 second initial delay
     * - 30 second max delay
     * - 2x multiplier
     * - 10% jitter
     * - Infinite retries
     */
    ReconnectConfig()
        : initial_delay_ms(1000)
        , max_delay_ms(30000)
        , multiplier_percent(200)
        , jitter_percent(10)
        , max_retries(0)
    {}
};

/**
 * @brief Result of a retry check
 */
enum class RetryResult : uint8_t {
    ShouldRetry,      ///< OK to retry, delay calculated
    MaxRetriesReached ///< Maximum retry count exceeded
};

/**
 * @brief Reconnection manager with exponential backoff
 *
 * Manages retry timing for network reconnection attempts. Provides
 * exponential backoff with optional jitter to spread out retry
 * attempts and avoid overwhelming the server.
 *
 * ## State Diagram
 *
 * ```
 *   [Initial]
 *       |
 *       | get_next_delay_ms()
 *       v
 *   [Waiting] <--+
 *       |       |
 *       | record_failure()
 *       v       |
 *   [Backoff]---+
 *       |
 *       | reset()
 *       v
 *   [Initial]
 * ```
 */
class ReconnectManager {
public:
    /**
     * @brief Constructor with default configuration
     *
     * Creates a reconnect manager with default settings:
     * - 1s initial delay
     * - 30s max delay
     * - 2x multiplier
     * - 10% jitter
     */
    ReconnectManager();

    /**
     * @brief Constructor with custom configuration
     *
     * @param config Custom configuration parameters
     */
    explicit ReconnectManager(const ReconnectConfig& config);

    /**
     * @brief Get the delay for the next retry attempt
     *
     * Calculates the delay based on the current retry count and
     * configuration. Does NOT increment the retry counter - call
     * record_failure() after a failed attempt.
     *
     * @return Delay in milliseconds before next retry
     */
    uint32_t get_next_delay_ms() const;

    /**
     * @brief Get delay with jitter applied
     *
     * Same as get_next_delay_ms() but applies random jitter
     * based on the configured jitter percentage.
     *
     * @param seed Random seed for jitter calculation (e.g., tick count)
     * @return Delay in milliseconds with jitter applied
     */
    uint32_t get_next_delay_ms_with_jitter(uint32_t seed) const;

    /**
     * @brief Check if retry should be attempted
     *
     * Checks whether a retry should be attempted based on the
     * maximum retry configuration.
     *
     * @return RetryResult::ShouldRetry if OK to retry
     * @return RetryResult::MaxRetriesReached if limit exceeded
     */
    RetryResult should_retry() const;

    /**
     * @brief Record a connection failure
     *
     * Increments the retry counter, which increases the backoff
     * delay for the next attempt.
     */
    void record_failure();

    /**
     * @brief Reset the manager after successful connection
     *
     * Resets the retry counter to zero, so the next failure
     * will start with the initial delay again.
     */
    void reset();

    /**
     * @brief Get current retry attempt count
     *
     * @return Number of retries since last reset
     */
    uint32_t get_retry_count() const { return m_retry_count; }

    /**
     * @brief Get current calculated delay (without jitter)
     *
     * @return Current delay in milliseconds
     */
    uint32_t get_current_delay_ms() const { return m_current_delay_ms; }

    /**
     * @brief Get the configuration
     *
     * @return Reference to current configuration
     */
    const ReconnectConfig& get_config() const { return m_config; }

    /**
     * @brief Update configuration
     *
     * Allows changing configuration at runtime. Does NOT reset
     * the retry counter.
     *
     * @param config New configuration to use
     */
    void set_config(const ReconnectConfig& config);

private:
    ReconnectConfig m_config;       ///< Configuration parameters
    uint32_t m_retry_count;         ///< Number of retries since reset
    uint32_t m_current_delay_ms;    ///< Current calculated delay

    /**
     * @brief Calculate delay for current retry count
     *
     * Internal helper that computes the delay based on the
     * exponential backoff formula.
     */
    void calculate_delay();
};

/**
 * @brief Convert RetryResult to string for logging
 *
 * @param result Result to convert
 * @return Human-readable string representation
 */
inline const char* retry_result_to_string(RetryResult result) {
    switch (result) {
        case RetryResult::ShouldRetry:      return "ShouldRetry";
        case RetryResult::MaxRetriesReached: return "MaxRetriesReached";
        default:                             return "Unknown";
    }
}

} // namespace network
} // namespace ryu_ldn
