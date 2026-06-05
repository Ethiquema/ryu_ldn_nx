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
 * - 0 (disabled) max retries
 * - 1 fast retry at 200ms
 *
 * ### Fast Retry Tests
 * Test the fast retry mechanism:
 * - First retry uses fast_delay_ms (200ms)
 * - After fast_retries are exhausted, exponential backoff begins
 * - Exponential growth starts from initial_delay_ms at that point
 *
 * ### Exponential Backoff Tests
 * Test the core backoff algorithm:
 * - First non-fast retry uses initial delay
 * - Each subsequent retry doubles the delay (with 2x multiplier)
 * - Delay is capped at max_delay
 *
 * ### Reset Tests
 * Test that reset() properly resets state:
 * - Retry count goes to zero
 * - Delay returns to fast_delay_ms (first fast retry)
 *
 * ### Jitter Tests
 * Test the jitter functionality:
 * - Different seeds produce different delays
 * - Delays stay within jitter bounds
 * - Zero jitter returns base delay
 *
 * ### Max Retries Tests
 * Test maximum retry limiting:
 * - Disabled auto-reconnect when max_retries = 0
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

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            printf("    FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return false; \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            printf("    FAIL: %s:%d: %s == %s (%lld != %lld)\n", \
                   __FILE__, __LINE__, #a, #b, \
                   static_cast<long long>(a), static_cast<long long>(b)); \
            return false; \
        } \
    } while(0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            printf("    FAIL: %s:%d: %s != %s (both %lld)\n", \
                   __FILE__, __LINE__, #a, #b, \
                   static_cast<long long>(a)); \
            return false; \
        } \
    } while(0)

#define ASSERT_IN_RANGE(val, min_val, max_val) \
    do { \
        auto _v = static_cast<long long>(val); \
        auto _min = static_cast<long long>(min_val); \
        auto _max = static_cast<long long>(max_val); \
        if (_v < _min || _v > _max) { \
            printf("    FAIL: %s:%d: %s in [%lld, %lld] (got %lld)\n", \
                   __FILE__, __LINE__, #val, _min, _max, _v); \
            return false; \
        } \
    } while(0)

#define ASSERT_STREQ(a, b) \
    do { \
        if (std::strcmp((a), (b)) != 0) { \
            printf("    FAIL: %s:%d: \"%s\" != \"%s\"\n", \
                   __FILE__, __LINE__, (a), (b)); \
            return false; \
        } \
    } while(0)

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

bool test_default_initial_delay() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_config().initial_delay_ms, 1000);
    return true;
}

bool test_default_max_delay() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_config().max_delay_ms, 30000);
    return true;
}

bool test_default_multiplier() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_config().multiplier_percent, 200);
    return true;
}

bool test_default_jitter() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_config().jitter_percent, 10);
    return true;
}

bool test_default_max_retries() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_config().max_retries, 0);
    return true;
}

bool test_default_fast_retries() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_config().fast_retries, 1);
    return true;
}

bool test_default_fast_delay() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_config().fast_delay_ms, 200);
    return true;
}

bool test_initial_retry_count() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_retry_count(), 0);
    return true;
}

// ============================================================================
// Fast Retry Tests
// ============================================================================

/**
 * @brief Test that initial delay is the fast delay (200ms)
 *
 * With fast_retries=1, retry_count=0 uses fast_delay_ms.
 */
bool test_initial_delay_is_fast_delay() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_next_delay_ms(), 200);
    return true;
}

/**
 * @brief Test fast retry followed by exponential backoff
 *
 * retry 0: 200ms (fast)
 * retry 1: 1000ms (initial_delay, first exponential step)
 * retry 2: 2000ms (initial * 2)
 */
bool test_fast_then_exponential() {
    ReconnectManager mgr;

    ASSERT_EQ(mgr.get_next_delay_ms(), 200);   // fast retry (retry 0)
    mgr.record_failure();

    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);  // first exponential step (retry 1)
    mgr.record_failure();

    ASSERT_EQ(mgr.get_next_delay_ms(), 2000);  // second exponential step (retry 2)
    mgr.record_failure();

    ASSERT_EQ(mgr.get_next_delay_ms(), 4000);  // third exponential step (retry 3)
    return true;
}

/**
 * @brief Test multiple fast retries
 *
 * With fast_retries=2 and fast_delay_ms=150:
 * retry 0: 150ms (fast)
 * retry 1: 150ms (fast)
 * retry 2: 1000ms (initial)
 * retry 3: 2000ms (initial * 2)
 */
bool test_multiple_fast_retries() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 1000;
    cfg.fast_retries = 2;
    cfg.fast_delay_ms = 150;
    ReconnectManager mgr(cfg);

    ASSERT_EQ(mgr.get_next_delay_ms(), 150);   // fast retry 0
    mgr.record_failure();

    ASSERT_EQ(mgr.get_next_delay_ms(), 150);   // fast retry 1
    mgr.record_failure();

    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);  // first exponential step
    mgr.record_failure();

    ASSERT_EQ(mgr.get_next_delay_ms(), 2000);  // second exponential step
    return true;
}

/**
 * @brief Test zero fast retries (no fast retry phase)
 *
 * With fast_retries=0, delays start directly at initial_delay_ms.
 */
bool test_zero_fast_retries() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 1000;
    cfg.fast_retries = 0;
    ReconnectManager mgr(cfg);

    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);  // no fast phase
    mgr.record_failure();

    ASSERT_EQ(mgr.get_next_delay_ms(), 2000);  // initial * multiplier
    return true;
}

// ============================================================================
// Exponential Backoff Tests
// ============================================================================

/**
 * @brief Test full exponential growth after fast phase
 *
 * After fast_retries=1, exponential growth proceeds:
 * retry 1: 1000ms
 * retry 2: 2000ms
 * retry 3: 4000ms
 * retry 4: 8000ms
 */
bool test_exponential_growth() {
    ReconnectManager mgr;

    // skip fast retry
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 2000);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 4000);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 8000);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 16000);

    return true;
}

/**
 * @brief Test delay is capped at max_delay
 */
bool test_delay_capped_at_max() {
    ReconnectManager mgr;

    for (int i = 0; i < 10; i++) {
        mgr.record_failure();
    }

    ASSERT_EQ(mgr.get_next_delay_ms(), 30000);

    return true;
}

/**
 * @brief Test delay stays at max after reaching it
 */
bool test_delay_stays_at_max() {
    ReconnectManager mgr;

    for (int i = 0; i < 10; i++) {
        mgr.record_failure();
    }
    ASSERT_EQ(mgr.get_next_delay_ms(), 30000);

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

bool test_reset_clears_retry_count() {
    ReconnectManager mgr;

    mgr.record_failure();
    mgr.record_failure();
    mgr.record_failure();
    ASSERT_EQ(mgr.get_retry_count(), 3);

    mgr.reset();
    ASSERT_EQ(mgr.get_retry_count(), 0);

    return true;
}

/**
 * @brief Test reset restores initial delay (fast delay phase)
 */
bool test_reset_restores_initial_delay() {
    ReconnectManager mgr;

    mgr.record_failure();
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 2000);

    mgr.reset();
    ASSERT_EQ(mgr.get_next_delay_ms(), 200);  // Back to fast delay

    return true;
}

bool test_multiple_resets_safe() {
    ReconnectManager mgr;

    mgr.record_failure();
    mgr.reset();
    mgr.reset();
    mgr.reset();

    ASSERT_EQ(mgr.get_retry_count(), 0);
    ASSERT_EQ(mgr.get_next_delay_ms(), 200);  // fast delay

    return true;
}

/**
 * @brief Test backoff restarts from fast delay after reset
 */
bool test_backoff_after_reset() {
    ReconnectManager mgr;

    mgr.record_failure();
    mgr.record_failure();
    mgr.record_failure();

    mgr.reset();

    ASSERT_EQ(mgr.get_next_delay_ms(), 200);   // fast delay
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);  // first exponential step
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 2000);   // second exponential step

    return true;
}

// ============================================================================
// Jitter Tests
// ============================================================================

bool test_jitter_varies_with_seed() {
    ReconnectManager mgr;

    uint32_t delay1 = mgr.get_next_delay_ms_with_jitter(12345);
    uint32_t delay2 = mgr.get_next_delay_ms_with_jitter(54321);
    uint32_t delay3 = mgr.get_next_delay_ms_with_jitter(99999);

    bool all_same = (delay1 == delay2) && (delay2 == delay3);
    ASSERT_TRUE(!all_same);

    return true;
}

/**
 * @brief Test jitter stays within bounds for fast retry (200ms +/- 10%)
 */
bool test_jitter_within_bounds() {
    ReconnectManager mgr;

    // After reset, base delay is fast_delay_ms=200, jitter is 10%
    // Range: [180, 220]
    uint32_t min_expected = 180;
    uint32_t max_expected = 220;

    for (uint32_t seed = 0; seed < 1000; seed++) {
        uint32_t delay = mgr.get_next_delay_ms_with_jitter(seed);
        ASSERT_IN_RANGE(delay, min_expected, max_expected);
    }

    return true;
}

bool test_zero_jitter_no_variation() {
    ReconnectConfig cfg;
    cfg.jitter_percent = 0;
    ReconnectManager mgr(cfg);

    uint32_t base = mgr.get_next_delay_ms();

    ASSERT_EQ(mgr.get_next_delay_ms_with_jitter(12345), base);
    ASSERT_EQ(mgr.get_next_delay_ms_with_jitter(54321), base);
    ASSERT_EQ(mgr.get_next_delay_ms_with_jitter(99999), base);

    return true;
}

/**
 * @brief Test jitter at higher delays (after fast phase)
 */
bool test_jitter_at_high_delay() {
    ReconnectManager mgr;

    // Skip fast retry, get to exponential delay
    mgr.record_failure();  // now at 1000ms
    mgr.record_failure();  // now at 2000ms

    // Range should be [1800, 2200] (2000 +/- 10%)
    uint32_t min_expected = 1800;
    uint32_t max_expected = 2200;

    for (uint32_t seed = 0; seed < 100; seed++) {
        uint32_t delay = mgr.get_next_delay_ms_with_jitter(seed);
        ASSERT_IN_RANGE(delay, min_expected, max_expected);
    }

    return true;
}

// ============================================================================
// Max Retries Tests
// ============================================================================

bool test_max_retries_zero_disables_retry() {
    ReconnectManager mgr;

    // max_retries=0 disables auto-reconnect entirely (not infinite retries)
    ASSERT_EQ(mgr.should_retry(), RetryResult::MaxRetriesReached);

    return true;
}

bool test_max_retries_limit() {
    ReconnectConfig cfg;
    cfg.max_retries = 3;
    ReconnectManager mgr(cfg);

    ASSERT_EQ(mgr.should_retry(), RetryResult::ShouldRetry);
    mgr.record_failure();

    ASSERT_EQ(mgr.should_retry(), RetryResult::ShouldRetry);
    mgr.record_failure();

    ASSERT_EQ(mgr.should_retry(), RetryResult::ShouldRetry);
    mgr.record_failure();

    ASSERT_EQ(mgr.should_retry(), RetryResult::MaxRetriesReached);

    return true;
}

bool test_reset_allows_retries_again() {
    ReconnectConfig cfg;
    cfg.max_retries = 2;
    ReconnectManager mgr(cfg);

    mgr.record_failure();
    mgr.record_failure();
    ASSERT_EQ(mgr.should_retry(), RetryResult::MaxRetriesReached);

    mgr.reset();
    ASSERT_EQ(mgr.should_retry(), RetryResult::ShouldRetry);

    return true;
}

// ============================================================================
// Custom Configuration Tests
// ============================================================================

/**
 * @brief Test custom initial delay with fast retries skipped
 */
bool test_custom_initial_delay() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 500;
    cfg.fast_retries = 0;  // skip fast phase
    ReconnectManager mgr(cfg);

    ASSERT_EQ(mgr.get_next_delay_ms(), 500);

    return true;
}

/**
 * @brief Test custom max delay with fast phase
 *
 * fast(200) → 1000 → 2000 → 4000 → 5000 (capped)
 */
bool test_custom_max_delay() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 1000;
    cfg.max_delay_ms = 5000;
    cfg.multiplier_percent = 200;
    // fast_retries=1, fast_delay=200 by default
    ReconnectManager mgr(cfg);

    mgr.record_failure();  // skip fast delay, now at 1000
    mgr.record_failure();  // 2000
    mgr.record_failure();  // 4000
    mgr.record_failure();  // would be 8000, capped at 5000

    ASSERT_EQ(mgr.get_next_delay_ms(), 5000);

    return true;
}

/**
 * @brief Test custom multiplier (1.5x) with fast phase
 *
 * fast(200) → 1000 → 1500 → 2250 → 3375
 */
bool test_custom_multiplier() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 1000;
    cfg.multiplier_percent = 150;  // 1.5x
    cfg.max_delay_ms = 100000;
    // fast_retries=1 by default
    ReconnectManager mgr(cfg);

    ASSERT_EQ(mgr.get_next_delay_ms(), 200);   // fast delay
    mgr.record_failure();

    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);  // first exponential step
    mgr.record_failure();

    ASSERT_EQ(mgr.get_next_delay_ms(), 1500);  // 1000 * 1.5
    mgr.record_failure();

    ASSERT_EQ(mgr.get_next_delay_ms(), 2250);  // 1500 * 1.5

    return true;
}

/**
 * @brief Test custom jitter on fast delay (200ms +/- 50%)
 */
bool test_custom_jitter() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 1000;
    cfg.jitter_percent = 50;  // 50%
    // fast_retries=1, fast_delay=200 by default
    ReconnectManager mgr(cfg);

    // Base is fast_delay_ms=200, +/- 50% = [100, 300]
    uint32_t min_expected = 100;
    uint32_t max_expected = 300;

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

    ASSERT_EQ(mgr.get_next_delay_ms(), 200);  // fast delay

    ReconnectConfig new_cfg;
    new_cfg.initial_delay_ms = 2000;
    // new_cfg keeps default fast_retries=1, fast_delay_ms=200
    mgr.set_config(new_cfg);

    ASSERT_EQ(mgr.get_config().initial_delay_ms, 2000);
    // After set_config, still at retry_count=0, so still fast delay
    ASSERT_EQ(mgr.get_next_delay_ms(), 200);

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
    // With fast_retries=1, retry_count=2 means we're past fast phase
    // exponential step = retry_count - fast_retries = 1
    // delay = initial_delay * multiplier^1 = 500 * 2 = 1000
    mgr.set_config(new_cfg);

    ASSERT_EQ(mgr.get_retry_count(), 2);
    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);

    return true;
}

// ============================================================================
// Edge Cases
// ============================================================================

/**
 * @brief Test very small initial delay (with fast phase disabled)
 */
bool test_small_initial_delay() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 1;
    cfg.max_delay_ms = 100;
    cfg.fast_retries = 0;  // disable fast phase
    ReconnectManager mgr(cfg);

    ASSERT_EQ(mgr.get_next_delay_ms(), 1);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 2);

    return true;
}

/**
 * @brief Test initial delay equals max delay (with fast phase disabled)
 */
bool test_initial_equals_max() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 5000;
    cfg.max_delay_ms = 5000;
    cfg.fast_retries = 0;  // disable fast phase
    ReconnectManager mgr(cfg);

    ASSERT_EQ(mgr.get_next_delay_ms(), 5000);

    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 5000);

    return true;
}

/**
 * @brief Test multiplier of 100 (1.0x = no growth) with fast phase
 */
bool test_no_growth_multiplier() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 1000;
    cfg.multiplier_percent = 100;  // 1.0x
    cfg.fast_retries = 0;  // disable fast phase for simpler test
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

bool test_retry_result_to_string() {
    ASSERT_STREQ(retry_result_to_string(RetryResult::ShouldRetry), "ShouldRetry");
    ASSERT_STREQ(retry_result_to_string(RetryResult::MaxRetriesReached), "MaxRetriesReached");
    ASSERT_STREQ(retry_result_to_string(static_cast<RetryResult>(99)), "Unknown");
    return true;
}

// ============================================================================
// Default Fast Retry Delay Tests (new)
// ============================================================================

/**
 * @brief Test that default fast delay is 200ms
 */
bool test_default_fast_delay_value() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_config().fast_delay_ms, 200);
    return true;
}

/**
 * @brief Test that default fast_retries is 1
 */
bool test_default_fast_retries_count() {
    ReconnectManager mgr;
    ASSERT_EQ(mgr.get_config().fast_retries, 1);
    return true;
}

/**
 * @brief Test fast delay applies on first retry attempt
 *
 * The fast delay phase uses fast_delay_ms for retry_count < fast_retries.
 */
bool test_fast_delay_applies_first() {
    ReconnectManager mgr;

    // retry_count=0: should be fast_delay_ms (200)
    ASSERT_EQ(mgr.get_next_delay_ms(), 200);

    // After one failure, retry_count=1 > fast_retries=0 (past fast phase)
    // Wait: fast_retries=1, so retry_count=1 means retry_count >= fast_retries
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);  // initial_delay_ms

    return true;
}

/**
 * @brief Test custom fast delay configuration
 */
bool test_custom_fast_delay() {
    ReconnectConfig cfg;
    cfg.fast_delay_ms = 100;
    cfg.fast_retries = 2;
    ReconnectManager mgr(cfg);

    ASSERT_EQ(mgr.get_next_delay_ms(), 100);   // fast retry 0
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 100);   // fast retry 1
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);  // exponential begins

    return true;
}

/**
 * @brief Test fast delay with jitter
 */
bool test_fast_delay_with_jitter() {
    ReconnectManager mgr;

    // Fast delay = 200ms, jitter = 10%, range = [180, 220]
    for (uint32_t seed = 0; seed < 100; seed++) {
        uint32_t delay = mgr.get_next_delay_ms_with_jitter(seed);
        ASSERT_IN_RANGE(delay, 180, 220);
    }

    return true;
}

/**
 * @brief Test transition from fast to exponential is smooth
 */
bool test_fast_to_exponential_transition() {
    ReconnectConfig cfg;
    cfg.initial_delay_ms = 500;
    cfg.fast_delay_ms = 100;
    cfg.fast_retries = 3;
    cfg.multiplier_percent = 200;
    ReconnectManager mgr(cfg);

    ASSERT_EQ(mgr.get_next_delay_ms(), 100);   // fast 0
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 100);   // fast 1
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 100);   // fast 2
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 500);   // initial (exponential step 0)
    mgr.record_failure();
    ASSERT_EQ(mgr.get_next_delay_ms(), 1000);  // 500 * 2

    return true;
}

// ============================================================================
// Main
// ============================================================================

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
    RUN_TEST(test_default_fast_retries);
    RUN_TEST(test_default_fast_delay);
    RUN_TEST(test_initial_retry_count);

    // Fast Retry Tests
    printf("\nFast Retries:\n");
    RUN_TEST(test_initial_delay_is_fast_delay);
    RUN_TEST(test_fast_then_exponential);
    RUN_TEST(test_multiple_fast_retries);
    RUN_TEST(test_zero_fast_retries);
    RUN_TEST(test_fast_delay_applies_first);
    RUN_TEST(test_custom_fast_delay);
    RUN_TEST(test_fast_delay_with_jitter);
    RUN_TEST(test_fast_to_exponential_transition);

    // Exponential Backoff Tests
    printf("\nExponential Backoff:\n");
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
    RUN_TEST(test_max_retries_zero_disables_retry);
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
    printf("  Results: %d/%d passed\n", g_tests_passed,
           g_tests_passed + g_tests_failed);
    printf("========================================\n");

    return g_tests_failed > 0 ? 1 : 0;
}