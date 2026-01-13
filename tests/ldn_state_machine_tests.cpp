/**
 * @file ldn_state_machine_tests.cpp
 * @brief Unit tests for LDN State Machine logic
 *
 * Since the actual LdnStateMachine class depends on stratosphere (Switch-only),
 * we test the state transition logic using a standalone test implementation
 * that mirrors the same state machine behavior.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include <cstdio>
#include <cstring>
#include <cstdint>

// ============================================================================
// Test Framework (minimal)
// ============================================================================

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct test_##name##_register { \
        test_##name##_register() { \
            printf("  [TEST] %s... ", #name); \
            fflush(stdout); \
            test_##name(); \
            printf("PASS\n"); \
            g_tests_passed++; \
        } \
    } test_##name##_instance; \
    static void test_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL\n    Assertion failed: %s\n    at %s:%d\n", #cond, __FILE__, __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            printf("FAIL\n    Expected: %s == %s\n    at %s:%d\n", #a, #b, __FILE__, __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

// ============================================================================
// Standalone State Machine Implementation (mirrors LdnStateMachine)
// ============================================================================

enum class CommState : uint32_t {
    None = 0,
    Initialized = 1,
    AccessPoint = 2,
    AccessPointCreated = 3,
    Station = 4,
    StationConnected = 5,
    Error = 6
};

enum class StateTransitionResult {
    Success,
    InvalidTransition,
    AlreadyInState
};

/**
 * @brief Test version of LDN State Machine
 *
 * Implements the same state transition logic as the real LdnStateMachine
 * but without stratosphere dependencies.
 */
class TestLdnStateMachine {
public:
    TestLdnStateMachine() : m_state(CommState::None), m_event_signaled(false) {}

    CommState GetState() const { return m_state; }
    bool WasEventSignaled() const { return m_event_signaled; }
    void ClearEventFlag() { m_event_signaled = false; }

    // State queries
    bool IsInState(CommState state) const { return m_state == state; }
    bool IsInitialized() const {
        return m_state != CommState::None && m_state != CommState::Error;
    }
    bool IsNetworkActive() const {
        return m_state == CommState::AccessPointCreated ||
               m_state == CommState::StationConnected;
    }

    // Transitions
    StateTransitionResult Initialize() {
        if (m_state != CommState::None) {
            return StateTransitionResult::InvalidTransition;
        }
        return TransitionTo(CommState::Initialized);
    }

    StateTransitionResult Finalize() {
        // Can finalize from any state
        return TransitionTo(CommState::None);
    }

    StateTransitionResult OpenAccessPoint() {
        if (m_state != CommState::Initialized) {
            return StateTransitionResult::InvalidTransition;
        }
        return TransitionTo(CommState::AccessPoint);
    }

    StateTransitionResult CloseAccessPoint() {
        if (m_state != CommState::AccessPoint &&
            m_state != CommState::AccessPointCreated) {
            return StateTransitionResult::InvalidTransition;
        }
        return TransitionTo(CommState::Initialized);
    }

    StateTransitionResult CreateNetwork() {
        if (m_state != CommState::AccessPoint) {
            return StateTransitionResult::InvalidTransition;
        }
        return TransitionTo(CommState::AccessPointCreated);
    }

    StateTransitionResult DestroyNetwork() {
        if (m_state != CommState::AccessPointCreated) {
            return StateTransitionResult::InvalidTransition;
        }
        return TransitionTo(CommState::AccessPoint);
    }

    StateTransitionResult OpenStation() {
        if (m_state != CommState::Initialized) {
            return StateTransitionResult::InvalidTransition;
        }
        return TransitionTo(CommState::Station);
    }

    StateTransitionResult CloseStation() {
        if (m_state != CommState::Station &&
            m_state != CommState::StationConnected) {
            return StateTransitionResult::InvalidTransition;
        }
        return TransitionTo(CommState::Initialized);
    }

    StateTransitionResult Connect() {
        if (m_state != CommState::Station) {
            return StateTransitionResult::InvalidTransition;
        }
        return TransitionTo(CommState::StationConnected);
    }

    StateTransitionResult Disconnect() {
        if (m_state != CommState::StationConnected) {
            return StateTransitionResult::InvalidTransition;
        }
        return TransitionTo(CommState::Station);
    }

    StateTransitionResult SetError() {
        if (m_state == CommState::Error) {
            return StateTransitionResult::AlreadyInState;
        }
        return TransitionTo(CommState::Error);
    }

private:
    StateTransitionResult TransitionTo(CommState new_state) {
        if (m_state == new_state) {
            return StateTransitionResult::AlreadyInState;
        }
        m_state = new_state;
        m_event_signaled = true;
        return StateTransitionResult::Success;
    }

    CommState m_state;
    bool m_event_signaled;
};

// ============================================================================
// Initial State Tests
// ============================================================================

TEST(initial_state_is_none) {
    TestLdnStateMachine sm;
    ASSERT_EQ(sm.GetState(), CommState::None);
}

TEST(initial_not_initialized) {
    TestLdnStateMachine sm;
    ASSERT_FALSE(sm.IsInitialized());
}

TEST(initial_not_network_active) {
    TestLdnStateMachine sm;
    ASSERT_FALSE(sm.IsNetworkActive());
}

TEST(initial_event_not_signaled) {
    TestLdnStateMachine sm;
    ASSERT_FALSE(sm.WasEventSignaled());
}

// ============================================================================
// Initialize Tests
// ============================================================================

TEST(initialize_from_none_succeeds) {
    TestLdnStateMachine sm;
    auto result = sm.Initialize();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::Initialized);
}

TEST(initialize_signals_event) {
    TestLdnStateMachine sm;
    sm.Initialize();
    ASSERT_TRUE(sm.WasEventSignaled());
}

TEST(initialize_from_initialized_fails) {
    TestLdnStateMachine sm;
    sm.Initialize();
    auto result = sm.Initialize();
    ASSERT_EQ(result, StateTransitionResult::InvalidTransition);
}

TEST(is_initialized_after_init) {
    TestLdnStateMachine sm;
    sm.Initialize();
    ASSERT_TRUE(sm.IsInitialized());
}

// ============================================================================
// Finalize Tests
// ============================================================================

TEST(finalize_from_initialized_succeeds) {
    TestLdnStateMachine sm;
    sm.Initialize();
    auto result = sm.Finalize();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::None);
}

TEST(finalize_from_access_point_succeeds) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenAccessPoint();
    auto result = sm.Finalize();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::None);
}

TEST(finalize_from_station_connected_succeeds) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenStation();
    sm.Connect();
    auto result = sm.Finalize();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::None);
}

TEST(finalize_from_none_already_in_state) {
    TestLdnStateMachine sm;
    auto result = sm.Finalize();
    ASSERT_EQ(result, StateTransitionResult::AlreadyInState);
}

// ============================================================================
// Access Point Flow Tests
// ============================================================================

TEST(open_access_point_from_initialized) {
    TestLdnStateMachine sm;
    sm.Initialize();
    auto result = sm.OpenAccessPoint();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::AccessPoint);
}

TEST(open_access_point_from_none_fails) {
    TestLdnStateMachine sm;
    auto result = sm.OpenAccessPoint();
    ASSERT_EQ(result, StateTransitionResult::InvalidTransition);
}

TEST(create_network_from_access_point) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenAccessPoint();
    auto result = sm.CreateNetwork();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::AccessPointCreated);
}

TEST(create_network_from_initialized_fails) {
    TestLdnStateMachine sm;
    sm.Initialize();
    auto result = sm.CreateNetwork();
    ASSERT_EQ(result, StateTransitionResult::InvalidTransition);
}

TEST(is_network_active_when_ap_created) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenAccessPoint();
    sm.CreateNetwork();
    ASSERT_TRUE(sm.IsNetworkActive());
}

TEST(destroy_network_from_ap_created) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenAccessPoint();
    sm.CreateNetwork();
    auto result = sm.DestroyNetwork();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::AccessPoint);
}

TEST(destroy_network_from_access_point_fails) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenAccessPoint();
    auto result = sm.DestroyNetwork();
    ASSERT_EQ(result, StateTransitionResult::InvalidTransition);
}

TEST(close_access_point_from_ap) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenAccessPoint();
    auto result = sm.CloseAccessPoint();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::Initialized);
}

TEST(close_access_point_from_ap_created) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenAccessPoint();
    sm.CreateNetwork();
    auto result = sm.CloseAccessPoint();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::Initialized);
}

TEST(close_access_point_from_station_fails) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenStation();
    auto result = sm.CloseAccessPoint();
    ASSERT_EQ(result, StateTransitionResult::InvalidTransition);
}

// ============================================================================
// Station Flow Tests
// ============================================================================

TEST(open_station_from_initialized) {
    TestLdnStateMachine sm;
    sm.Initialize();
    auto result = sm.OpenStation();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::Station);
}

TEST(open_station_from_none_fails) {
    TestLdnStateMachine sm;
    auto result = sm.OpenStation();
    ASSERT_EQ(result, StateTransitionResult::InvalidTransition);
}

TEST(connect_from_station) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenStation();
    auto result = sm.Connect();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::StationConnected);
}

TEST(connect_from_initialized_fails) {
    TestLdnStateMachine sm;
    sm.Initialize();
    auto result = sm.Connect();
    ASSERT_EQ(result, StateTransitionResult::InvalidTransition);
}

TEST(is_network_active_when_connected) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenStation();
    sm.Connect();
    ASSERT_TRUE(sm.IsNetworkActive());
}

TEST(disconnect_from_connected) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenStation();
    sm.Connect();
    auto result = sm.Disconnect();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::Station);
}

TEST(disconnect_from_station_fails) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenStation();
    auto result = sm.Disconnect();
    ASSERT_EQ(result, StateTransitionResult::InvalidTransition);
}

TEST(close_station_from_station) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenStation();
    auto result = sm.CloseStation();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::Initialized);
}

TEST(close_station_from_connected) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenStation();
    sm.Connect();
    auto result = sm.CloseStation();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::Initialized);
}

TEST(close_station_from_ap_fails) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenAccessPoint();
    auto result = sm.CloseStation();
    ASSERT_EQ(result, StateTransitionResult::InvalidTransition);
}

// ============================================================================
// Cannot Mix AP and Station Modes Tests
// ============================================================================

TEST(cannot_open_station_from_access_point) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenAccessPoint();
    auto result = sm.OpenStation();
    ASSERT_EQ(result, StateTransitionResult::InvalidTransition);
}

TEST(cannot_open_access_point_from_station) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenStation();
    auto result = sm.OpenAccessPoint();
    ASSERT_EQ(result, StateTransitionResult::InvalidTransition);
}

// ============================================================================
// Error State Tests
// ============================================================================

TEST(set_error_from_any_state) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.OpenAccessPoint();
    auto result = sm.SetError();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::Error);
}

TEST(set_error_already_in_error) {
    TestLdnStateMachine sm;
    sm.SetError();
    auto result = sm.SetError();
    ASSERT_EQ(result, StateTransitionResult::AlreadyInState);
}

TEST(not_initialized_when_error) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.SetError();
    ASSERT_FALSE(sm.IsInitialized());
}

TEST(finalize_from_error_succeeds) {
    TestLdnStateMachine sm;
    sm.Initialize();
    sm.SetError();
    auto result = sm.Finalize();
    ASSERT_EQ(result, StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::None);
}

// ============================================================================
// Full Flow Tests
// ============================================================================

TEST(full_host_flow) {
    TestLdnStateMachine sm;

    // Initialize
    ASSERT_EQ(sm.Initialize(), StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::Initialized);

    // Open AP
    ASSERT_EQ(sm.OpenAccessPoint(), StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::AccessPoint);

    // Create network
    ASSERT_EQ(sm.CreateNetwork(), StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::AccessPointCreated);
    ASSERT_TRUE(sm.IsNetworkActive());

    // Destroy network
    ASSERT_EQ(sm.DestroyNetwork(), StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::AccessPoint);

    // Close AP
    ASSERT_EQ(sm.CloseAccessPoint(), StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::Initialized);

    // Finalize
    ASSERT_EQ(sm.Finalize(), StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::None);
}

TEST(full_client_flow) {
    TestLdnStateMachine sm;

    // Initialize
    ASSERT_EQ(sm.Initialize(), StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::Initialized);

    // Open Station
    ASSERT_EQ(sm.OpenStation(), StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::Station);

    // Connect
    ASSERT_EQ(sm.Connect(), StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::StationConnected);
    ASSERT_TRUE(sm.IsNetworkActive());

    // Disconnect
    ASSERT_EQ(sm.Disconnect(), StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::Station);

    // Close Station
    ASSERT_EQ(sm.CloseStation(), StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::Initialized);

    // Finalize
    ASSERT_EQ(sm.Finalize(), StateTransitionResult::Success);
    ASSERT_EQ(sm.GetState(), CommState::None);
}

TEST(event_cleared_and_resignaled) {
    TestLdnStateMachine sm;

    sm.Initialize();
    ASSERT_TRUE(sm.WasEventSignaled());

    sm.ClearEventFlag();
    ASSERT_FALSE(sm.WasEventSignaled());

    sm.OpenAccessPoint();
    ASSERT_TRUE(sm.WasEventSignaled());
}

// ============================================================================
// IsInState Tests
// ============================================================================

TEST(is_in_state_correct) {
    TestLdnStateMachine sm;
    ASSERT_TRUE(sm.IsInState(CommState::None));
    ASSERT_FALSE(sm.IsInState(CommState::Initialized));

    sm.Initialize();
    ASSERT_FALSE(sm.IsInState(CommState::None));
    ASSERT_TRUE(sm.IsInState(CommState::Initialized));
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("\n========================================\n");
    printf("  LDN State Machine Tests - ryu_ldn_nx\n");
    printf("========================================\n\n");

    // Tests run automatically via static initializers

    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n", g_tests_passed, g_tests_passed + g_tests_failed);
    printf("========================================\n\n");

    return g_tests_failed > 0 ? 1 : 0;
}
