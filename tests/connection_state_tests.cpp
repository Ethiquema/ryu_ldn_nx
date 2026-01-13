/**
 * @file connection_state_tests.cpp
 * @brief Unit tests for ConnectionStateMachine
 *
 * This file contains comprehensive unit tests for the ConnectionStateMachine
 * class, which manages the lifecycle of network connections through a
 * finite state machine pattern.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 *
 * @section Test Categories
 *
 * The tests are organized into the following categories:
 *
 * ### Initial State Tests
 * Verify that a newly created state machine has the correct initial values:
 * - State is Disconnected
 * - Retry count is zero
 * - Not connected, not ready, not transitioning
 *
 * ### Valid Transition Tests (Happy Path)
 * Test the normal connection flow:
 * - Disconnected -> Connecting -> Connected -> Ready
 *
 * ### Failure and Recovery Tests
 * Test error handling and automatic retry behavior:
 * - Connection failures trigger Backoff state
 * - Backoff expires to trigger Retrying
 * - Recovery from various failure points
 *
 * ### Disconnect Tests
 * Test graceful disconnection from various states:
 * - Ready -> Disconnecting -> Disconnected
 * - Cancel from Backoff state
 *
 * ### Error State Tests
 * Test fatal error handling:
 * - Transition to Error state
 * - Recovery options from Error state
 *
 * ### Invalid Transition Tests
 * Verify that invalid state transitions are rejected:
 * - Returns InvalidTransition for impossible transitions
 * - Returns AlreadyInState for no-op transitions
 *
 * ### Callback Tests
 * Test the state change notification callback:
 * - Callback invoked on successful transitions
 * - Not invoked on invalid transitions
 * - Null callback is safe
 *
 * ### Retry Count Tests
 * Test the retry counter behavior:
 * - Increments on retry attempts
 * - Resets on successful Ready state
 * - Manual reset works
 *
 * ### Force State Tests
 * Test the force_state() method:
 * - Can force to any state
 * - Does not trigger callback
 *
 * ### String Conversion Tests
 * Test the *_to_string() helper methods:
 * - All states have valid string representations
 * - All events have valid string representations
 * - All transition results have valid string representations
 *
 * ### Helper Method Tests
 * Test utility methods:
 * - is_connected() returns true for connected states
 * - is_ready() returns true only in Ready state
 * - is_transitioning() returns true for intermediate states
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-3.0-or-later
 */

#include "network/connection_state.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>

using namespace ryu_ldn::network;

// ============================================================================
// Test Framework
// ============================================================================

/**
 * @brief Global counter for total tests executed
 */
static int g_tests_run = 0;

/**
 * @brief Global counter for tests that passed
 */
static int g_tests_passed = 0;

/**
 * @brief Macro to define a test function
 * @param name Test name (used to generate function name)
 */
#define TEST(name) static void test_##name()

/**
 * @brief Macro to run a test and track results
 *
 * Prints test name, executes the test function, catches exceptions,
 * and updates the global pass/fail counters.
 *
 * @param name Test name (must match a TEST(name) definition)
 */
#define RUN_TEST(name) do { \
    g_tests_run++; \
    printf("  [TEST] %s... ", #name); \
    try { \
        test_##name(); \
        g_tests_passed++; \
        printf("PASS\n"); \
    } catch (const std::exception& e) { \
        printf("FAIL: %s\n", e.what()); \
    } \
} while(0)

/**
 * @brief Assert that an expression evaluates to true
 * @param expr Expression to evaluate
 * @throws std::runtime_error if expression is false
 */
#define ASSERT_TRUE(expr) \
    do { if (!(expr)) { throw std::runtime_error(#expr " is false"); } } while(0)

/**
 * @brief Assert that an expression evaluates to false
 * @param expr Expression to evaluate
 * @throws std::runtime_error if expression is true
 */
#define ASSERT_FALSE(expr) \
    do { if (expr) { throw std::runtime_error(#expr " is true"); } } while(0)

/**
 * @brief Assert that two values are equal
 *
 * Values are cast to long long for comparison to handle
 * signed/unsigned comparison issues.
 *
 * @param a First value
 * @param b Second value
 * @throws std::runtime_error if values are not equal
 */
#define ASSERT_EQ(a, b) \
    do { \
        auto _a = static_cast<long long>(a); \
        auto _b = static_cast<long long>(b); \
        if (_a != _b) { \
            char buf[128]; \
            snprintf(buf, sizeof(buf), "%s != %s (%lld vs %lld)", #a, #b, _a, _b); \
            throw std::runtime_error(buf); \
        } \
    } while(0)

/**
 * @brief Assert that two C strings are equal
 * @param a First string
 * @param b Second string
 * @throws std::runtime_error if strings are not equal
 */
#define ASSERT_STREQ(a, b) \
    do { if (strcmp(a, b) != 0) { throw std::runtime_error(#a " != " #b); } } while(0)

// ============================================================================
// Callback Tracking for Tests
// ============================================================================

/**
 * @brief Last old state passed to callback
 */
static ConnectionState g_last_old_state;

/**
 * @brief Last new state passed to callback
 */
static ConnectionState g_last_new_state;

/**
 * @brief Last event passed to callback
 */
static ConnectionEvent g_last_event;

/**
 * @brief Count of callback invocations
 */
static int g_callback_count = 0;

/**
 * @brief Test callback that records state transitions
 *
 * This callback is used to verify that state transitions are properly
 * notified. It stores the transition parameters in global variables
 * and increments a counter.
 *
 * @param old_state State before transition
 * @param new_state State after transition
 * @param event Event that triggered transition
 */
static void test_callback(ConnectionState old_state, ConnectionState new_state,
                          ConnectionEvent event) {
    g_last_old_state = old_state;
    g_last_new_state = new_state;
    g_last_event = event;
    g_callback_count++;
}

/**
 * @brief Reset callback tracking state
 *
 * Call this at the start of each callback test to ensure clean state.
 */
static void reset_callback_tracking() {
    g_last_old_state = ConnectionState::Disconnected;
    g_last_new_state = ConnectionState::Disconnected;
    g_last_event = ConnectionEvent::Disconnect;
    g_callback_count = 0;
}

// ============================================================================
// Initial State Tests
// ============================================================================

/**
 * @brief Verify initial state is Disconnected
 */
TEST(initial_state_disconnected) {
    ConnectionStateMachine sm;
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Disconnected));
}

/**
 * @brief Verify initial retry count is zero
 */
TEST(initial_retry_count_zero) {
    ConnectionStateMachine sm;
    ASSERT_EQ(sm.get_retry_count(), 0);
}

/**
 * @brief Verify is_connected() returns false initially
 */
TEST(initial_not_connected) {
    ConnectionStateMachine sm;
    ASSERT_FALSE(sm.is_connected());
}

/**
 * @brief Verify is_ready() returns false initially
 */
TEST(initial_not_ready) {
    ConnectionStateMachine sm;
    ASSERT_FALSE(sm.is_ready());
}

/**
 * @brief Verify is_transitioning() returns false initially
 */
TEST(initial_not_transitioning) {
    ConnectionStateMachine sm;
    ASSERT_FALSE(sm.is_transitioning());
}

// ============================================================================
// Valid Transition Tests - Happy Path
// ============================================================================

/**
 * @brief Test Disconnected -> Connecting transition
 */
TEST(transition_disconnected_to_connecting) {
    ConnectionStateMachine sm;
    TransitionResult result = sm.process_event(ConnectionEvent::Connect);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Connecting));
}

/**
 * @brief Test Connecting -> Connected transition
 */
TEST(transition_connecting_to_connected) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    TransitionResult result = sm.process_event(ConnectionEvent::ConnectSuccess);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Connected));
}

/**
 * @brief Test Connected -> Ready transition
 */
TEST(transition_connected_to_ready) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectSuccess);
    TransitionResult result = sm.process_event(ConnectionEvent::HandshakeSuccess);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Ready));
}

/**
 * @brief Test complete happy path: Disconnected -> Ready
 */
TEST(full_happy_path) {
    ConnectionStateMachine sm;

    // Connect
    sm.process_event(ConnectionEvent::Connect);
    ASSERT_TRUE(sm.is_transitioning());

    // TCP established
    sm.process_event(ConnectionEvent::ConnectSuccess);
    ASSERT_TRUE(sm.is_connected());
    ASSERT_FALSE(sm.is_ready());

    // Handshake complete
    sm.process_event(ConnectionEvent::HandshakeSuccess);
    ASSERT_TRUE(sm.is_connected());
    ASSERT_TRUE(sm.is_ready());
    ASSERT_FALSE(sm.is_transitioning());
}

// ============================================================================
// Failure and Recovery Tests
// ============================================================================

/**
 * @brief Test Connecting -> Backoff on connection failure
 */
TEST(transition_connecting_to_backoff_on_failure) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    TransitionResult result = sm.process_event(ConnectionEvent::ConnectFailed);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Backoff));
}

/**
 * @brief Test Backoff -> Retrying when backoff timer expires
 */
TEST(transition_backoff_to_retrying) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectFailed);
    TransitionResult result = sm.process_event(ConnectionEvent::BackoffExpired);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Retrying));
}

/**
 * @brief Test Retrying -> Connected on retry success
 */
TEST(transition_retrying_to_connected) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectFailed);
    sm.process_event(ConnectionEvent::BackoffExpired);
    TransitionResult result = sm.process_event(ConnectionEvent::ConnectSuccess);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Connected));
}

/**
 * @brief Test Ready -> Backoff when connection is lost
 */
TEST(transition_ready_to_backoff_on_connection_lost) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectSuccess);
    sm.process_event(ConnectionEvent::HandshakeSuccess);
    TransitionResult result = sm.process_event(ConnectionEvent::ConnectionLost);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Backoff));
}

/**
 * @brief Test Connected -> Backoff when handshake fails
 */
TEST(transition_connected_to_backoff_on_handshake_failed) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectSuccess);
    TransitionResult result = sm.process_event(ConnectionEvent::HandshakeFailed);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Backoff));
}

/**
 * @brief Test Connected -> Handshaking on handshake started
 */
TEST(transition_connected_to_handshaking) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectSuccess);
    TransitionResult result = sm.process_event(ConnectionEvent::HandshakeStarted);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Handshaking));
}

/**
 * @brief Test Handshaking -> Ready on handshake success
 */
TEST(transition_handshaking_to_ready) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectSuccess);
    sm.process_event(ConnectionEvent::HandshakeStarted);
    TransitionResult result = sm.process_event(ConnectionEvent::HandshakeSuccess);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Ready));
}

/**
 * @brief Test Handshaking -> Backoff on handshake failure
 */
TEST(transition_handshaking_to_backoff_on_failure) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectSuccess);
    sm.process_event(ConnectionEvent::HandshakeStarted);
    TransitionResult result = sm.process_event(ConnectionEvent::HandshakeFailed);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Backoff));
}

/**
 * @brief Test Handshaking -> Backoff on connection lost
 */
TEST(transition_handshaking_to_backoff_on_connection_lost) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectSuccess);
    sm.process_event(ConnectionEvent::HandshakeStarted);
    TransitionResult result = sm.process_event(ConnectionEvent::ConnectionLost);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Backoff));
}

// ============================================================================
// Disconnect Tests
// ============================================================================

/**
 * @brief Test Ready -> Disconnecting on disconnect request
 */
TEST(transition_ready_to_disconnecting) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectSuccess);
    sm.process_event(ConnectionEvent::HandshakeSuccess);
    TransitionResult result = sm.process_event(ConnectionEvent::Disconnect);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Disconnecting));
}

/**
 * @brief Test Disconnecting -> Disconnected when connection closes
 */
TEST(transition_disconnecting_to_disconnected) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectSuccess);
    sm.process_event(ConnectionEvent::HandshakeSuccess);
    sm.process_event(ConnectionEvent::Disconnect);
    TransitionResult result = sm.process_event(ConnectionEvent::ConnectionLost);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Disconnected));
}

/**
 * @brief Test Backoff -> Disconnected on disconnect request
 */
TEST(transition_backoff_to_disconnected_on_disconnect) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectFailed);
    TransitionResult result = sm.process_event(ConnectionEvent::Disconnect);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Disconnected));
}

// ============================================================================
// Error State Tests
// ============================================================================

/**
 * @brief Test transition to Error on fatal error event
 */
TEST(transition_to_error_on_fatal) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectSuccess);
    TransitionResult result = sm.process_event(ConnectionEvent::FatalError);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Error));
}

/**
 * @brief Test recovery from Error state with retry request
 */
TEST(recover_from_error_with_retry) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::FatalError);
    TransitionResult result = sm.process_event(ConnectionEvent::RetryRequested);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Connecting));
}

/**
 * @brief Test recovery from Error state with disconnect request
 */
TEST(recover_from_error_with_disconnect) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::FatalError);
    TransitionResult result = sm.process_event(ConnectionEvent::Disconnect);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(TransitionResult::Success));
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Disconnected));
}

// ============================================================================
// Invalid Transition Tests
// ============================================================================

/**
 * @brief Test that Connect event is invalid when already Connecting
 */
TEST(invalid_connect_when_connecting) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    TransitionResult result = sm.process_event(ConnectionEvent::Connect);
    ASSERT_EQ(static_cast<int>(result),
              static_cast<int>(TransitionResult::InvalidTransition));
}

/**
 * @brief Test that HandshakeSuccess is invalid when Disconnected
 */
TEST(invalid_handshake_when_disconnected) {
    ConnectionStateMachine sm;
    TransitionResult result = sm.process_event(ConnectionEvent::HandshakeSuccess);
    ASSERT_EQ(static_cast<int>(result),
              static_cast<int>(TransitionResult::InvalidTransition));
}

/**
 * @brief Test that BackoffExpired is invalid when Connected
 */
TEST(invalid_backoff_expired_when_connected) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectSuccess);
    TransitionResult result = sm.process_event(ConnectionEvent::BackoffExpired);
    ASSERT_EQ(static_cast<int>(result),
              static_cast<int>(TransitionResult::InvalidTransition));
}

/**
 * @brief Test that Disconnect returns AlreadyInState when Disconnected
 */
TEST(already_in_state_disconnect_when_disconnected) {
    ConnectionStateMachine sm;
    TransitionResult result = sm.process_event(ConnectionEvent::Disconnect);
    ASSERT_EQ(static_cast<int>(result),
              static_cast<int>(TransitionResult::AlreadyInState));
}

// ============================================================================
// Callback Tests
// ============================================================================

/**
 * @brief Test that callback is invoked on successful transition
 */
TEST(callback_invoked_on_transition) {
    reset_callback_tracking();
    ConnectionStateMachine sm;
    sm.set_state_change_callback(test_callback);

    sm.process_event(ConnectionEvent::Connect);

    ASSERT_EQ(g_callback_count, 1);
    ASSERT_EQ(static_cast<int>(g_last_old_state),
              static_cast<int>(ConnectionState::Disconnected));
    ASSERT_EQ(static_cast<int>(g_last_new_state),
              static_cast<int>(ConnectionState::Connecting));
    ASSERT_EQ(static_cast<int>(g_last_event),
              static_cast<int>(ConnectionEvent::Connect));
}

/**
 * @brief Test that callback is NOT invoked on invalid transition
 */
TEST(callback_not_invoked_on_invalid_transition) {
    reset_callback_tracking();
    ConnectionStateMachine sm;
    sm.set_state_change_callback(test_callback);

    sm.process_event(ConnectionEvent::HandshakeSuccess);  // Invalid

    ASSERT_EQ(g_callback_count, 0);
}

/**
 * @brief Test that callback tracks all transitions
 */
TEST(callback_tracks_multiple_transitions) {
    reset_callback_tracking();
    ConnectionStateMachine sm;
    sm.set_state_change_callback(test_callback);

    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectSuccess);
    sm.process_event(ConnectionEvent::HandshakeSuccess);

    ASSERT_EQ(g_callback_count, 3);
    ASSERT_EQ(static_cast<int>(g_last_new_state),
              static_cast<int>(ConnectionState::Ready));
}

/**
 * @brief Test that null callback is safe (no crash)
 */
TEST(null_callback_safe) {
    ConnectionStateMachine sm;
    sm.set_state_change_callback(nullptr);

    // Should not crash
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectSuccess);
}

// ============================================================================
// Retry Count Tests
// ============================================================================

/**
 * @brief Test that retry count increments on first retry
 */
TEST(retry_count_increments_on_retry) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectFailed);
    sm.process_event(ConnectionEvent::BackoffExpired);

    ASSERT_EQ(sm.get_retry_count(), 1);
}

/**
 * @brief Test that retry count increments on multiple retries
 */
TEST(retry_count_increments_multiple) {
    ConnectionStateMachine sm;

    // First attempt
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectFailed);

    // Second attempt
    sm.process_event(ConnectionEvent::BackoffExpired);
    sm.process_event(ConnectionEvent::ConnectFailed);

    // Third attempt
    sm.process_event(ConnectionEvent::BackoffExpired);

    ASSERT_EQ(sm.get_retry_count(), 2);
}

/**
 * @brief Test that retry count resets when reaching Ready state
 */
TEST(retry_count_resets_on_ready) {
    ConnectionStateMachine sm;

    // Fail once
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectFailed);
    sm.process_event(ConnectionEvent::BackoffExpired);
    ASSERT_EQ(sm.get_retry_count(), 1);

    // Succeed
    sm.process_event(ConnectionEvent::ConnectSuccess);
    sm.process_event(ConnectionEvent::HandshakeSuccess);
    ASSERT_EQ(sm.get_retry_count(), 0);
}

/**
 * @brief Test manual retry count reset
 */
TEST(manual_retry_count_reset) {
    ConnectionStateMachine sm;
    sm.process_event(ConnectionEvent::Connect);
    sm.process_event(ConnectionEvent::ConnectFailed);
    sm.process_event(ConnectionEvent::BackoffExpired);

    sm.reset_retry_count();
    ASSERT_EQ(sm.get_retry_count(), 0);
}

// ============================================================================
// Force State Tests
// ============================================================================

/**
 * @brief Test that force_state() changes state directly
 */
TEST(force_state_works) {
    ConnectionStateMachine sm;
    sm.force_state(ConnectionState::Ready);
    ASSERT_EQ(static_cast<int>(sm.get_state()),
              static_cast<int>(ConnectionState::Ready));
}

/**
 * @brief Test that force_state() does NOT trigger callback
 */
TEST(force_state_does_not_trigger_callback) {
    reset_callback_tracking();
    ConnectionStateMachine sm;
    sm.set_state_change_callback(test_callback);

    sm.force_state(ConnectionState::Ready);

    ASSERT_EQ(g_callback_count, 0);
}

// ============================================================================
// String Conversion Tests
// ============================================================================

/**
 * @brief Test state_to_string() for all states
 */
TEST(state_to_string_all_states) {
    ASSERT_STREQ(ConnectionStateMachine::state_to_string(ConnectionState::Disconnected),
                 "Disconnected");
    ASSERT_STREQ(ConnectionStateMachine::state_to_string(ConnectionState::Connecting),
                 "Connecting");
    ASSERT_STREQ(ConnectionStateMachine::state_to_string(ConnectionState::Connected),
                 "Connected");
    ASSERT_STREQ(ConnectionStateMachine::state_to_string(ConnectionState::Handshaking),
                 "Handshaking");
    ASSERT_STREQ(ConnectionStateMachine::state_to_string(ConnectionState::Ready),
                 "Ready");
    ASSERT_STREQ(ConnectionStateMachine::state_to_string(ConnectionState::Backoff),
                 "Backoff");
    ASSERT_STREQ(ConnectionStateMachine::state_to_string(ConnectionState::Retrying),
                 "Retrying");
    ASSERT_STREQ(ConnectionStateMachine::state_to_string(ConnectionState::Disconnecting),
                 "Disconnecting");
    ASSERT_STREQ(ConnectionStateMachine::state_to_string(ConnectionState::Error),
                 "Error");
}

/**
 * @brief Test event_to_string() for all events
 */
TEST(event_to_string_all_events) {
    ASSERT_STREQ(ConnectionStateMachine::event_to_string(ConnectionEvent::Connect),
                 "Connect");
    ASSERT_STREQ(ConnectionStateMachine::event_to_string(ConnectionEvent::ConnectSuccess),
                 "ConnectSuccess");
    ASSERT_STREQ(ConnectionStateMachine::event_to_string(ConnectionEvent::ConnectFailed),
                 "ConnectFailed");
    ASSERT_STREQ(ConnectionStateMachine::event_to_string(ConnectionEvent::HandshakeStarted),
                 "HandshakeStarted");
    ASSERT_STREQ(ConnectionStateMachine::event_to_string(ConnectionEvent::HandshakeSuccess),
                 "HandshakeSuccess");
    ASSERT_STREQ(ConnectionStateMachine::event_to_string(ConnectionEvent::HandshakeFailed),
                 "HandshakeFailed");
    ASSERT_STREQ(ConnectionStateMachine::event_to_string(ConnectionEvent::Disconnect),
                 "Disconnect");
    ASSERT_STREQ(ConnectionStateMachine::event_to_string(ConnectionEvent::ConnectionLost),
                 "ConnectionLost");
    ASSERT_STREQ(ConnectionStateMachine::event_to_string(ConnectionEvent::BackoffExpired),
                 "BackoffExpired");
    ASSERT_STREQ(ConnectionStateMachine::event_to_string(ConnectionEvent::RetryRequested),
                 "RetryRequested");
    ASSERT_STREQ(ConnectionStateMachine::event_to_string(ConnectionEvent::FatalError),
                 "FatalError");
}

/**
 * @brief Test transition_result_to_string() for all results
 */
TEST(transition_result_to_string_all) {
    ASSERT_STREQ(transition_result_to_string(TransitionResult::Success),
                 "Success");
    ASSERT_STREQ(transition_result_to_string(TransitionResult::InvalidTransition),
                 "InvalidTransition");
    ASSERT_STREQ(transition_result_to_string(TransitionResult::AlreadyInState),
                 "AlreadyInState");
}

// ============================================================================
// Helper Method Tests
// ============================================================================

/**
 * @brief Test is_connected() returns true in Connected state
 */
TEST(is_connected_in_connected_state) {
    ConnectionStateMachine sm;
    sm.force_state(ConnectionState::Connected);
    ASSERT_TRUE(sm.is_connected());
}

/**
 * @brief Test is_connected() returns true in Handshaking state
 */
TEST(is_connected_in_handshaking_state) {
    ConnectionStateMachine sm;
    sm.force_state(ConnectionState::Handshaking);
    ASSERT_TRUE(sm.is_connected());
}

/**
 * @brief Test is_connected() returns true in Ready state
 */
TEST(is_connected_in_ready_state) {
    ConnectionStateMachine sm;
    sm.force_state(ConnectionState::Ready);
    ASSERT_TRUE(sm.is_connected());
}

/**
 * @brief Test is_connected() returns false in Backoff state
 */
TEST(is_not_connected_in_backoff_state) {
    ConnectionStateMachine sm;
    sm.force_state(ConnectionState::Backoff);
    ASSERT_FALSE(sm.is_connected());
}

/**
 * @brief Test is_transitioning() returns true in Connecting state
 */
TEST(is_transitioning_in_connecting) {
    ConnectionStateMachine sm;
    sm.force_state(ConnectionState::Connecting);
    ASSERT_TRUE(sm.is_transitioning());
}

/**
 * @brief Test is_transitioning() returns true in Backoff state
 */
TEST(is_transitioning_in_backoff) {
    ConnectionStateMachine sm;
    sm.force_state(ConnectionState::Backoff);
    ASSERT_TRUE(sm.is_transitioning());
}

/**
 * @brief Test is_transitioning() returns false in Ready state
 */
TEST(is_not_transitioning_in_ready) {
    ConnectionStateMachine sm;
    sm.force_state(ConnectionState::Ready);
    ASSERT_FALSE(sm.is_transitioning());
}

// ============================================================================
// Main Entry Point
// ============================================================================

/**
 * @brief Main function that runs all tests
 *
 * Executes all test categories in order and prints a summary of results.
 *
 * @return 0 if all tests pass, 1 if any test fails
 */
int main() {
    printf("\n========================================\n");
    printf("  ConnectionStateMachine Tests - ryu_ldn_nx\n");
    printf("========================================\n\n");

    // Initial State Tests
    RUN_TEST(initial_state_disconnected);
    RUN_TEST(initial_retry_count_zero);
    RUN_TEST(initial_not_connected);
    RUN_TEST(initial_not_ready);
    RUN_TEST(initial_not_transitioning);

    // Valid Transition Tests
    RUN_TEST(transition_disconnected_to_connecting);
    RUN_TEST(transition_connecting_to_connected);
    RUN_TEST(transition_connected_to_ready);
    RUN_TEST(full_happy_path);

    // Failure and Recovery Tests
    RUN_TEST(transition_connecting_to_backoff_on_failure);
    RUN_TEST(transition_backoff_to_retrying);
    RUN_TEST(transition_retrying_to_connected);
    RUN_TEST(transition_ready_to_backoff_on_connection_lost);
    RUN_TEST(transition_connected_to_backoff_on_handshake_failed);

    // Handshaking State Tests
    RUN_TEST(transition_connected_to_handshaking);
    RUN_TEST(transition_handshaking_to_ready);
    RUN_TEST(transition_handshaking_to_backoff_on_failure);
    RUN_TEST(transition_handshaking_to_backoff_on_connection_lost);

    // Disconnect Tests
    RUN_TEST(transition_ready_to_disconnecting);
    RUN_TEST(transition_disconnecting_to_disconnected);
    RUN_TEST(transition_backoff_to_disconnected_on_disconnect);

    // Error State Tests
    RUN_TEST(transition_to_error_on_fatal);
    RUN_TEST(recover_from_error_with_retry);
    RUN_TEST(recover_from_error_with_disconnect);

    // Invalid Transition Tests
    RUN_TEST(invalid_connect_when_connecting);
    RUN_TEST(invalid_handshake_when_disconnected);
    RUN_TEST(invalid_backoff_expired_when_connected);
    RUN_TEST(already_in_state_disconnect_when_disconnected);

    // Callback Tests
    RUN_TEST(callback_invoked_on_transition);
    RUN_TEST(callback_not_invoked_on_invalid_transition);
    RUN_TEST(callback_tracks_multiple_transitions);
    RUN_TEST(null_callback_safe);

    // Retry Count Tests
    RUN_TEST(retry_count_increments_on_retry);
    RUN_TEST(retry_count_increments_multiple);
    RUN_TEST(retry_count_resets_on_ready);
    RUN_TEST(manual_retry_count_reset);

    // Force State Tests
    RUN_TEST(force_state_works);
    RUN_TEST(force_state_does_not_trigger_callback);

    // String Conversion Tests
    RUN_TEST(state_to_string_all_states);
    RUN_TEST(event_to_string_all_events);
    RUN_TEST(transition_result_to_string_all);

    // Helper Method Tests
    RUN_TEST(is_connected_in_connected_state);
    RUN_TEST(is_connected_in_handshaking_state);
    RUN_TEST(is_connected_in_ready_state);
    RUN_TEST(is_not_connected_in_backoff_state);
    RUN_TEST(is_transitioning_in_connecting);
    RUN_TEST(is_transitioning_in_backoff);
    RUN_TEST(is_not_transitioning_in_ready);

    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n", g_tests_passed, g_tests_run);
    printf("========================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
