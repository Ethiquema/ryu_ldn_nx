/**
 * @file reconnect_tests.cpp
 * @brief Unit tests for ReconnectManager
 *
 * This file contains comprehensive unit tests for the ReconnectManager
 * class, which implements exponential backoff for network reconnection.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 *
 * @section Test Categories
 *
 * The tests are organized into the following categories:
 *
 * ### Default Configuration Tests
 * Verify that default configuration values are correct:
 * - 1000ms initial delay
 * - 30000ms max delay
 * - 200 (2.0x) multiplier
 * - 10% jitter
 * - 0 (infinite) max retries
 *
 * ### Exponential Backoff Tests
 * Test the core backoff algorithm:
 * - First retry uses initial delay
 * - Each subsequent retry doubles the delay
 * - Delay is capped at max_delay
 *
 * ### Reset Tests
 * Test that reset() properly resets state:
 * - Retry count goes to zero
 * - Delay returns to initial value
 *
 * ### Jitter Tests
 * Test the jitter functionality:
 * - Different seeds produce different delays
 * - Delays stay within jitter bounds
 * - Zero jitter returns base delay
 *
 * ### Max Retries Tests
 * Test maximum retry limiting:
 * - Infinite retries when max_retries = 0
 * - MaxRetriesReached when limit exceeded
 *
 * ### Custom Configuration Tests
 * Test non-default configuration values
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "network/reconnect.hpp"

using namespace ryu_ldn::network;

// ============================================================================
// Test Framework (Minimal)
// ============================================================================

/**
 * @brief Global test counters
 */
static int g_tests_passed = 0;
static int g_tests_failed = 0;

/**
 * @brief Assert macro with detailed failure message
 */
#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            printf("    FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return false; \
        } \
    } while(0)

/**
 * @brief Assert equality macro with value display
 */
#define ASSERT_EQ(a, b) \
    do { \
        auto _a = static_cast<long long>(a); \
        auto _b = static_cast<long long>(b); \
        if (_a != _b) { \
            printf("    FAIL: %s:%d: %s == %s (%lld != %lld)\n", \
                   __FILE__, __LINE__, #a, #b, _a, _b); \
            return false; \
        } \
    } while(0)

/**
 * @brief Assert value within range
 */
#define ASSERT_IN_RANGE(val, min, max) \
    do { \
        auto _v = static_cast<long long>(val); \
        auto _min = static_cast<long long>(min); \
        auto _max = static_cast<long long>(max); \
        if (_v < _min || _v > _max) { \
            printf("    FAIL: %s:%d: %s in [%lld, %lld] (got %lld)\n", \
                   __FILE__, __LINE__, #val, _min, _max, _v); \
            return false; \
        } \
    } while(0)

/**
 * @brief Assert string equality
 */
#define ASSERT_STREQ(a, b) \
    do { \
        if (std::strcmp((a), (b)) != 0) { \
            printf("    FAIL: %s:%d: \"%s\" != \"%s\"\n", \
                   __FILE__, __LINE__, (a), (b)); \
            return false; \
        } \
    } while(0)

/**
 * @brief Test runner macro
 */
#define RUN_TEST(test_func) \
    do { \
        printf("  [TEST] %s... ", #test_func); \
        if (test_func()) { \
            printf("PASS\n"); \
            g_tests_passed++; \
        } else { \
            g_tests_failed++; \
        } \
    } while(0)

// ============================================================================
// Default Configuration Tests
// ============================================================================

/**
 * @brief Test default initial delay is 1000ms
 */
bool test_default_initial_delay() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_config().initial_delay_ms, 1000);
    return true;
}

/**
 * @brief Test default max delay is 30000ms
 */
bool test_default_max_delay() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_config().max_delay_ms, 30000);
    return true;
}

/**
 * @brief Test default multiplier is 200 (2.0x)
 */
bool test_default_multiplier() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_config().multiplier_percent, 200);
    return true;
}

/**
 * @brief Test default jitter is 10%
 */
bool test_default_jitter() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_config().jitter_percent, 10);
    return true;
}

/**
 * @brief Test default max_retries is 0 (infinite)
 */
bool test_default_max_retries() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_config().max_retries, 0);
    return true;
}

/**
 * @brief Test initial retry count is 0
 */
bool test_initial_retry_count() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_retry_count(), 0);
    return true;
}

/**
 * @brief Test initial delay is initial_delay_ms
 */
bool test_initial_delay_value() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);
    return true;
}

// ============================================================================
// Exponential Backoff Tests
// ============================================================================

/**
 * @brief Test first retry delay is initial delay
 *
 * After first failure, delay should be initial_delay_ms
 */
bool test_first_retry_delay() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 2000);  // initial * multiplier
    return true;
}

/**
 * @brief Test exponential growth: 1s -> 2s -> 4s -> 8s
 */
bool test_exponential_growth() {
    ReconnectManager mgr;

    // Initial delay (retry 0)
    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);

    // After 1st failure
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 2000);

    // After 2nd failure
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 4000);

    // After 3rd failure
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 8000);

    // After 4th failure
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 16000);

    return true;
}

/**
 * @brief Test delay is capped at max_delay
 */
bool test_delay_capped_at_max() {
    ReconnectManager mgr;

    // Simulate many failures to exceed max
    for (int i = 0; i < 10; i++) {
        mgr.record_failure();
    }

    // Should be capped at 30000ms
    ASSERT_EQ(mgr.get_next_delay_ms(), 30000);

    return true;
}

/**
 * @brief Test delay stays at max after reaching it
 */
bool test_delay_stays_at_max() {
    ReconnectManager mgr;

    // Get to max
    for (int i = 0; i < 10; i++) {
        mgr.record_failure();
    }
    ASSERT_EQ(mgr.get_next_delay_ms(), 30000);

    // More failures should keep it at max
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 30000);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 30000);

    return true;
}

/**
 * @brief Test retry count increments correctly
 */
bool test_retry_count_increments() {
    ReconnectManager mgr;

    ASSERT_EQ(mgr.get_retry_count(), 0);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_retry_count(), 1);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_retry_count(), 2);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_retry_count(), 3);

    return true;
}

// ============================================================================
// Reset Tests
// ============================================================================

/**
 * @brief Test reset clears retry count
 */
bool test_reset_clears_retry_count() {
    ReconnectManager mgr;

    // Accumulate some failures
    mgr.record_failure();
    mgr.record_failure();
    mgr.record_failure();
    ASSERT_EQ(mgr.get_retry_count(), 3);

    // Reset
    mgr.reset();
    ASSERT_EQ(mgr.get_retry_count(), 0);

    return true;
}

/**
 * @brief Test reset restores initial delay
 */
bool test_reset_restores_initial_delay() {
    ReconnectManager mgr;

    // Accumulate failures
    mgr.record_failure();
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 4000);

    // Reset
    mgr.reset();
    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);

    return true;
}

/**
 * @brief Test multiple reset calls are safe
 */
bool test_multiple_resets_safe() {
    ReconnectManager mgr;

    mgr.record_failure();
    mgr.reset();
    mgr.reset();
    mgr.reset();

    ASSERT_EQ(mgr.get_retry_count(), 0);
    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);

    return true;
}

/**
 * @brief Test backoff continues normally after reset
 */
bool test_backoff_after_reset() {
    ReconnectManager mgr;

    // Build up some backoff
    mgr.record_failure();
    mgr.record_failure();
    mgr.record_failure();

    // Reset
    mgr.reset();

    // Backoff should start fresh
    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 2000);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 4000);

    return true;
}

// ============================================================================
// Jitter Tests
// ============================================================================

/**
 * @brief Test different seeds produce different delays
 */
bool test_jitter_varies_with_seed() {
    ReconnectManager mgr;

    uint32_t delay1 = mgr.get_next_delay_ms_with_jitter(12345);
    uint32_t delay2 = mgr.get_next_delay_ms_with_jitter(54321);
    uint32_t delay3 = mgr.get_next_delay_ms_with_jitter(99999);

    // Not all should be the same (statistically very unlikely)
    bool all_same = (delay1 == delay2) && (delay2 == delay3);
    ASSERT_TRUE(!all_same);

    return true;
}

/**
 * @brief Test jitter stays within configured bounds
 */
bool test_jitter_within_bounds() {
    ReconnectManager mgr;

    // Base delay is 1000ms, jitter is 10%
    // So range is [900, 1100]
    uint32_t min_expected = 900;
    uint32_t max_expected = 1100;

    // Test many seeds
    for (uint32_t seed = 0; seed < 1000; seed++) {
        uint32_t delay = mgr.get_next_delay_ms_with_jitter(seed);
        ASSERT_IN_RANGE(delay, min_expected, max_expected);
    }

    return true;
}

/**
 * @brief Test zero jitter returns base delay
 */
bool test_zero_jitter_no_variation() {
    ReconnectConfig cfg;
    cfg.jitter_percent = 0;
    ReconnectManager mgr(cfg);

    uint32_t base = mgr.get_next_delay_ms();

    // All seeds should return base delay
    ASSERT_EQ(mgr.get_next_delay_ms_with_jitter(12345), base);
    ASSERT_EQ(mgr.get_next_delay_ms_with_jitter(54321), base);
    ASSERT_EQ(mgr.get_next_delay_ms_with_jitter(99999), base);

    return true;
}

/**
 * @brief Test jitter at higher delays
 */
bool test_jitter_at_high_delay() {
    ReconnectManager mgr;

    // Get to higher delay
    mgr.record_failure();
    mgr.record_failure();
    mgr.record_failure();  // Now at 8000ms

    // Range should be [7200, 8800] (8000 +/- 10%)
    uint32_t min_expected = 7200;
    uint32_t max_expected = 8800;

    for (uint32_t seed = 0; seed < 100; seed++) {
        uint32_t delay = mgr.get_next_delay_ms_with_jitter(seed);
        ASSERT_IN_RANGE(delay, min_expected, max_expected);
    }

    return true;
}

// ============================================================================
// Max Retries Tests
// ============================================================================

/**
 * @brief Test infinite retries when max_retries = 0
 */
bool test_infinite_retries_by_default() {
    ReconnectManager mgr;

    // Many retries should all be allowed
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(mgr.should_retry(), RetryResult::ShouldRetry);
        mgr.record_failure();
    }

    // Still allowed
    ASSERT_EQ(mgr.should_retry(), RetryResult::ShouldRetry);

    return true;
}

/**
 * @brief Test max retries limit is enforced
 */
bool test_max_retries_limit() {
    ReconnectConfig cfg;
    cfg.max_retries = 3;
    ReconnectManager mgr(cfg);

    // First 3 should be allowed
    ASSERT_EQ(mgr.should_retry(), RetryResult::ShouldRetry);
    mgr.record_failure();

    ASSERT_EQ(mgr.should_retry(), RetryResult::ShouldRetry);
    mgr.record_failure();

    ASSERT_EQ(mgr.should_retry(), RetryResult::ShouldRetry);
    mgr.record_failure();

    // 4th should be denied
    ASSERT_EQ(mgr.should_retry(), RetryResult::MaxRetriesReached);

    return true;
}

/**
 * @brief Test reset allows retries again after max reached
 */
bool test_reset_allows_retries_again() {
    ReconnectConfig cfg;
    cfg.max_retries = 2;
    ReconnectManager mgr(cfg);

    // Use up retries
    mgr.record_failure();
    mgr.record_failure();
    ASSERT_EQ(mgr.should_retry(), RetryResult::MaxRetriesReached);

    // Reset should allow retries again
    mgr.reset();
    ASSERT_EQ(mgr.should_retry(), RetryResult::ShouldRetry);

    return true;
}

// ============================================================================
// Custom Configuration Tests
// ============================================================================

/**
 * @brief Test custom initial delay
 */
bool test_custom_initial_delay() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 500;
    ReconnectManager mgr(cfg);

    ASSERT_EQ(mgr.get_next_delay_ms(), 500);

    return true;
}

/**
 * @brief Test custom max delay
 */
bool test_custom_max_delay() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 1000;
    cfg.max_delay_ms = 5000;
    cfg.multiplier_percent = 200;
    ReconnectManager mgr(cfg);

    // 1000 -> 2000 -> 4000 -> 5000 (capped)
    mgr.record_failure();
    mgr.record_failure();
    mgr.record_failure();

    ASSERT_EQ(mgr.get_next_delay_ms(), 5000);

    return true;
}

/**
 * @brief Test custom multiplier (1.5x)
 */
bool test_custom_multiplier() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 1000;
    cfg.multiplier_percent = 150;  // 1.5x
    cfg.max_delay_ms = 100000;
    ReconnectManager mgr(cfg);

    // 1000 -> 1500 -> 2250 -> 3375
    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 1500);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 2250);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 3375);

    return true;
}

/**
 * @brief Test custom jitter percentage
 */
bool test_custom_jitter() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 1000;
    cfg.jitter_percent = 50;  // 50%
    ReconnectManager mgr(cfg);

    // Range should be [500, 1500] (1000 +/- 50%)
    uint32_t min_expected = 500;
    uint32_t max_expected = 1500;

    for (uint32_t seed = 0; seed < 100; seed++) {
        uint32_t delay = mgr.get_next_delay_ms_with_jitter(seed);
        ASSERT_IN_RANGE(delay, min_expected, max_expected);
    }

    return true;
}

/**
 * @brief Test set_config updates configuration
 */
bool test_set_config() {
    ReconnectManager mgr;

    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);

    ReconnectConfig new_cfg;
    new_cfg.initial_delay_ms = 2000;
    mgr.set_config(new_cfg);

    // Should recalculate with new config
    ASSERT_EQ(mgr.get_config().initial_delay_ms, 2000);

    return true;
}

/**
 * @brief Test set_config preserves retry count
 */
bool test_set_config_preserves_retry_count() {
    ReconnectManager mgr;

    mgr.record_failure();
    mgr.record_failure();
    ASSERT_EQ(mgr.get_retry_count(), 2);

    ReconnectConfig new_cfg;
    new_cfg.initial_delay_ms = 500;
    mgr.set_config(new_cfg);

    // Retry count should be preserved
    ASSERT_EQ(mgr.get_retry_count(), 2);
    // But delay is recalculated (500 * 2^2 = 2000)
    ASSERT_EQ(mgr.get_next_delay_ms(), 2000);

    return true;
}

// ============================================================================
// Edge Cases
// ============================================================================

/**
 * @brief Test very small initial delay
 */
bool test_small_initial_delay() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 1;
    cfg.max_delay_ms = 100;
    ReconnectManager mgr(cfg);

    ASSERT_EQ(mgr.get_next_delay_ms(), 1);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 2);

    return true;
}

/**
 * @brief Test initial delay equals max delay
 */
bool test_initial_equals_max() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 5000;
    cfg.max_delay_ms = 5000;
    ReconnectManager mgr(cfg);

    ASSERT_EQ(mgr.get_next_delay_ms(), 5000);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 5000);

    return true;
}

/**
 * @brief Test multiplier of 100 (1.0x = no growth)
 */
bool test_no_growth_multiplier() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 1000;
    cfg.multiplier_percent = 100;  // 1.0x
    ReconnectManager mgr(cfg);

    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);

    return true;
}

// ============================================================================
// String Conversion Tests
// ============================================================================

/**
 * @brief Test retry_result_to_string for all values
 */
bool test_retry_result_to_string() {
    ASSERT_STREQ(retry_result_to_string(RetryResult::ShouldRetry), "ShouldRetry");
    ASSERT_STREQ(retry_result_to_string(RetryResult::MaxRetriesReached), "MaxRetriesReached");
    ASSERT_STREQ(retry_result_to_string(static_cast<RetryResult>(99)), "Unknown");
    return true;
}

// ============================================================================
// Main
// ============================================================================

/**
 * @brief Run all tests
 */
int main() {
    printf("\n========================================\n");
    printf("  ReconnectManager Tests - ryu_ldn_nx\n");
    printf("========================================\n\n");

    // Default Configuration Tests
    printf("Default Configuration:\n");
    RUN_TEST(test_default_initial_delay);
    RUN_TEST(test_default_max_delay);
    RUN_TEST(test_default_multiplier);
    RUN_TEST(test_default_jitter);
    RUN_TEST(test_default_max_retries);
    RUN_TEST(test_initial_retry_count);
    RUN_TEST(test_initial_delay_value);

    // Exponential Backoff Tests
    printf("\nExponential Backoff:\n");
    RUN_TEST(test_first_retry_delay);
    RUN_TEST(test_exponential_growth);
    RUN_TEST(test_delay_capped_at_max);
    RUN_TEST(test_delay_stays_at_max);
    RUN_TEST(test_retry_count_increments);

    // Reset Tests
    printf("\nReset Behavior:\n");
    RUN_TEST(test_reset_clears_retry_count);
    RUN_TEST(test_reset_restores_initial_delay);
    RUN_TEST(test_multiple_resets_safe);
    RUN_TEST(test_backoff_after_reset);

    // Jitter Tests
    printf("\nJitter:\n");
    RUN_TEST(test_jitter_varies_with_seed);
    RUN_TEST(test_jitter_within_bounds);
    RUN_TEST(test_zero_jitter_no_variation);
    RUN_TEST(test_jitter_at_high_delay);

    // Max Retries Tests
    printf("\nMax Retries:\n");
    RUN_TEST(test_infinite_retries_by_default);
    RUN_TEST(test_max_retries_limit);
    RUN_TEST(test_reset_allows_retries_again);

    // Custom Configuration Tests
    printf("\nCustom Configuration:\n");
    RUN_TEST(test_custom_initial_delay);
    RUN_TEST(test_custom_max_delay);
    RUN_TEST(test_custom_multiplier);
    RUN_TEST(test_custom_jitter);
    RUN_TEST(test_set_config);
    RUN_TEST(test_set_config_preserves_retry_count);

    // Edge Cases
    printf("\nEdge Cases:\n");
    RUN_TEST(test_small_initial_delay);
    RUN_TEST(test_initial_equals_max);
    RUN_TEST(test_no_growth_multiplier);

    // String Conversion
    printf("\nString Conversion:\n");
    RUN_TEST(test_retry_result_to_string);

    // Summary
    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n",
           g_tests_passed, g_tests_passed + g_tests_failed);
    printf("========================================\n\n");

    return g_tests_failed > 0 ? 1 : 0;
}
