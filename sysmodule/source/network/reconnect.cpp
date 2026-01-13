/**
 * @file reconnect.cpp
 * @brief Reconnection Manager Implementation
 *
 * This module implements the exponential backoff algorithm for automatic
 * network reconnection. The algorithm ensures that retry attempts are
 * spaced out appropriately to avoid overwhelming the server while still
 * attempting to reconnect in a timely manner.
 *
 * ## Algorithm Details
 *
 * The exponential backoff works as follows:
 *
 * 1. **First failure**: Wait `initial_delay_ms` (default 1 second)
 * 2. **Second failure**: Wait `initial_delay_ms * multiplier` (default 2 seconds)
 * 3. **Third failure**: Wait `initial_delay_ms * multiplier^2` (default 4 seconds)
 * 4. **Continue**: Until reaching `max_delay_ms` (default 30 seconds)
 *
 * The delay is capped at `max_delay_ms` to ensure reconnection attempts
 * continue at a reasonable rate even after many failures.
 *
 * ## Jitter Implementation
 *
 * Jitter is implemented using a simple linear congruential generator (LCG)
 * to avoid depending on rand() or other PRNG libraries. The jitter adds
 * random variation to prevent multiple clients from synchronizing their
 * reconnection attempts (thundering herd problem).
 *
 * The jitter formula:
 * ```
 * jittered = base_delay * (1.0 + variation)
 * where: variation = [-jitter_percent, +jitter_percent] / 100.0
 * ```
 *
 * ## Overflow Protection
 *
 * The implementation uses careful arithmetic to prevent integer overflow
 * when calculating delays. The maximum safe retry count before overflow
 * depends on the initial delay and multiplier. Beyond this point, the
 * delay is clamped to max_delay_ms.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "reconnect.hpp"

namespace ryu_ldn {
namespace network {

/**
 * @brief Constructor with default configuration
 *
 * Initializes the manager with default settings suitable for
 * most reconnection scenarios:
 * - 1 second initial delay
 * - 30 second maximum delay
 * - 2x multiplier (doubling)
 * - 10% jitter
 * - Infinite retries
 *
 * The retry count starts at 0 and the initial delay is pre-calculated.
 */
ReconnectManager::ReconnectManager()
    : m_config()
    , m_retry_count(0)
    , m_current_delay_ms(0)
{
    // Calculate initial delay (will be initial_delay_ms for retry_count=0)
    calculate_delay();
}

/**
 * @brief Constructor with custom configuration
 *
 * Allows customizing all backoff parameters for specific use cases.
 * For example, a more aggressive retry strategy might use:
 * - 500ms initial delay
 * - 10s max delay
 * - 1.5x multiplier
 *
 * @param config Custom configuration parameters
 */
ReconnectManager::ReconnectManager(const ReconnectConfig& config)
    : m_config(config)
    , m_retry_count(0)
    , m_current_delay_ms(0)
{
    calculate_delay();
}

/**
 * @brief Calculate delay based on current retry count
 *
 * Implements the exponential backoff formula:
 * ```
 * delay = initial_delay * (multiplier ^ retry_count)
 * ```
 *
 * The calculation is done using integer arithmetic to avoid
 * floating-point operations on embedded systems. Overflow is
 * prevented by checking against max_delay before multiplying.
 *
 * Special case: When retry_count is 0 (first attempt), the
 * delay is set to initial_delay_ms.
 */
void ReconnectManager::calculate_delay() {
    // Start with initial delay
    m_current_delay_ms = m_config.initial_delay_ms;

    // Apply exponential growth for each retry
    for (uint32_t i = 0; i < m_retry_count; ++i) {
        // Check for potential overflow before multiplying
        // max_delay / (multiplier/100) gives us the safe threshold
        uint32_t safe_threshold = (m_config.max_delay_ms * 100) /
                                   m_config.multiplier_percent;

        if (m_current_delay_ms >= safe_threshold) {
            // Would overflow or exceed max, clamp to max
            m_current_delay_ms = m_config.max_delay_ms;
            return;
        }

        // Apply multiplier: delay = delay * (multiplier_percent / 100)
        m_current_delay_ms = (m_current_delay_ms * m_config.multiplier_percent) / 100;

        // Clamp to max delay
        if (m_current_delay_ms > m_config.max_delay_ms) {
            m_current_delay_ms = m_config.max_delay_ms;
            return;
        }
    }
}

/**
 * @brief Get the delay for the next retry attempt
 *
 * Returns the pre-calculated delay based on the current retry count.
 * This method is const and does not modify state - call record_failure()
 * after a failed connection attempt to increment the counter.
 *
 * @return Delay in milliseconds before the next retry should be attempted
 */
uint32_t ReconnectManager::get_next_delay_ms() const {
    return m_current_delay_ms;
}

/**
 * @brief Get delay with random jitter applied
 *
 * Adds random variation to the base delay to prevent thundering herd.
 * The jitter is calculated using a simple hash of the provided seed
 * to generate pseudo-random variation within the configured range.
 *
 * For example, with 10% jitter and 1000ms base delay:
 * - Minimum: 900ms (1000 * 0.9)
 * - Maximum: 1100ms (1000 * 1.1)
 *
 * @param seed Random seed (e.g., system tick count, time)
 * @return Delay in milliseconds with jitter applied
 */
uint32_t ReconnectManager::get_next_delay_ms_with_jitter(uint32_t seed) const {
    // If jitter is disabled, return base delay
    if (m_config.jitter_percent == 0) {
        return m_current_delay_ms;
    }

    // Simple hash function to generate pseudo-random value
    // Uses a variation of xorshift for decent distribution
    uint32_t hash = seed;
    hash ^= hash << 13;
    hash ^= hash >> 17;
    hash ^= hash << 5;

    // Map hash to range [-jitter_percent, +jitter_percent]
    // hash % (2 * jitter + 1) gives [0, 2*jitter]
    // Subtract jitter to get [-jitter, +jitter]
    uint32_t jitter_range = 2 * m_config.jitter_percent + 1;
    int32_t jitter_offset = static_cast<int32_t>(hash % jitter_range) -
                            static_cast<int32_t>(m_config.jitter_percent);

    // Apply jitter: delay * (100 + jitter_offset) / 100
    int64_t adjusted = static_cast<int64_t>(m_current_delay_ms) *
                       (100 + jitter_offset) / 100;

    // Ensure result is positive and within bounds
    if (adjusted < 1) {
        adjusted = 1;
    }
    if (adjusted > m_config.max_delay_ms) {
        adjusted = m_config.max_delay_ms;
    }

    return static_cast<uint32_t>(adjusted);
}

/**
 * @brief Check if retry should be attempted
 *
 * Evaluates whether a retry attempt is permitted based on the
 * maximum retry configuration. If max_retries is 0 (default),
 * infinite retries are allowed.
 *
 * @return RetryResult::ShouldRetry if retry is permitted
 * @return RetryResult::MaxRetriesReached if limit exceeded
 */
RetryResult ReconnectManager::should_retry() const {
    // If max_retries is 0, infinite retries are allowed
    if (m_config.max_retries == 0) {
        return RetryResult::ShouldRetry;
    }

    // Check if we've exceeded the maximum
    if (m_retry_count >= m_config.max_retries) {
        return RetryResult::MaxRetriesReached;
    }

    return RetryResult::ShouldRetry;
}

/**
 * @brief Record a connection failure
 *
 * Increments the retry counter and recalculates the delay for
 * the next attempt. Call this method after each failed connection
 * attempt to increase the backoff delay.
 *
 * The delay will grow exponentially until reaching max_delay_ms,
 * after which it stays constant.
 */
void ReconnectManager::record_failure() {
    // Increment retry count (with overflow protection)
    if (m_retry_count < UINT32_MAX) {
        m_retry_count++;
    }

    // Recalculate delay for next attempt
    calculate_delay();
}

/**
 * @brief Reset the manager after successful connection
 *
 * Resets the retry counter to zero, so the next failure will
 * start the backoff sequence from the beginning. Call this
 * method after a successful connection is established.
 */
void ReconnectManager::reset() {
    m_retry_count = 0;
    calculate_delay();
}

/**
 * @brief Update configuration at runtime
 *
 * Allows changing the backoff parameters without creating a new
 * manager instance. The retry count is preserved, but the delay
 * is recalculated with the new parameters.
 *
 * @param config New configuration to use
 */
void ReconnectManager::set_config(const ReconnectConfig& config) {
    m_config = config;
    calculate_delay();
}

} // namespace network
} // namespace ryu_ldn
