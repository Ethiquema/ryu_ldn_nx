/**
 * @file shared_state_tests.cpp
 * @brief Unit tests for SharedState singleton (runtime LDN state)
 *
 * Tests for the SharedState class that shares runtime LDN state between
 * the MITM service and the standalone ryu:cfg configuration service.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include <cstdio>
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
// Types (mirroring sysmodule definitions)
// ============================================================================

using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

/**
 * @brief LDN communication state (mirrors ams::mitm::ldn::CommState)
 */
enum class CommState : u32 {
    None = 0,
    Initialized = 1,
    AccessPoint = 2,
    AccessPointCreated = 3,
    Station = 4,
    StationConnected = 5,
    Error = 6
};

// ============================================================================
// Test Implementation of SharedState (mirrors the real implementation)
// ============================================================================

#include <mutex>

/**
 * @brief Shared runtime state singleton
 *
 * This class provides a thread-safe singleton for sharing runtime LDN state
 * between the MITM service (which updates the state) and the ryu:cfg service
 * (which exposes it to the overlay).
 */
class SharedState {
public:
    static SharedState& GetInstance() {
        static SharedState instance;
        return instance;
    }

    /**
     * @brief Reset all state (for testing)
     */
    void Reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_game_active = false;
        m_process_id = 0;
        m_ldn_state = CommState::None;
        m_node_count = 0;
        m_max_nodes = 0;
        m_local_node_id = 0;
        m_is_host = false;
        m_last_rtt_ms = 0;
        m_reconnect_requested = false;
    }

    // =========================================================================
    // Game Active State
    // =========================================================================

    void SetGameActive(bool active, u64 process_id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_game_active = active;
        m_process_id = active ? process_id : 0;
        if (!active) {
            // Reset runtime state when game exits
            m_ldn_state = CommState::None;
            m_node_count = 0;
            m_max_nodes = 0;
            m_local_node_id = 0;
            m_is_host = false;
            m_last_rtt_ms = 0;
        }
    }

    bool IsGameActive() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_game_active;
    }

    u64 GetActiveProcessId() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_process_id;
    }

    // =========================================================================
    // LDN State
    // =========================================================================

    void SetLdnState(CommState state) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ldn_state = state;
    }

    CommState GetLdnState() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_ldn_state;
    }

    // =========================================================================
    // Session Info
    // =========================================================================

    void SetSessionInfo(u8 node_count, u8 max_nodes, u8 local_node_id, bool is_host) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_node_count = node_count;
        m_max_nodes = max_nodes;
        m_local_node_id = local_node_id;
        m_is_host = is_host;
    }

    void GetSessionInfo(u8& node_count, u8& max_nodes, u8& local_node_id, bool& is_host) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        node_count = m_node_count;
        max_nodes = m_max_nodes;
        local_node_id = m_local_node_id;
        is_host = m_is_host;
    }

    // =========================================================================
    // RTT (Round-Trip Time)
    // =========================================================================

    void SetLastRtt(u32 rtt_ms) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_last_rtt_ms = rtt_ms;
    }

    u32 GetLastRtt() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_last_rtt_ms;
    }

    // =========================================================================
    // Reconnect Request
    // =========================================================================

    void RequestReconnect() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_reconnect_requested = true;
    }

    bool ConsumeReconnectRequest() {
        std::lock_guard<std::mutex> lock(m_mutex);
        bool was_requested = m_reconnect_requested;
        m_reconnect_requested = false;
        return was_requested;
    }

private:
    SharedState() = default;
    SharedState(const SharedState&) = delete;
    SharedState& operator=(const SharedState&) = delete;

    mutable std::mutex m_mutex;
    bool m_game_active = false;
    u64 m_process_id = 0;
    CommState m_ldn_state = CommState::None;
    u8 m_node_count = 0;
    u8 m_max_nodes = 0;
    u8 m_local_node_id = 0;
    bool m_is_host = false;
    u32 m_last_rtt_ms = 0;
    bool m_reconnect_requested = false;
};

// ============================================================================
// Singleton Tests
// ============================================================================

TEST(singleton_returns_same_instance) {
    auto& s1 = SharedState::GetInstance();
    auto& s2 = SharedState::GetInstance();
    ASSERT_EQ(&s1, &s2);
}

// ============================================================================
// Game Active State Tests
// ============================================================================

TEST(initially_game_not_active) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    ASSERT_FALSE(state.IsGameActive());
}

TEST(initially_process_id_zero) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    ASSERT_EQ(state.GetActiveProcessId(), (u64)0);
}

TEST(set_game_active_true) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    state.SetGameActive(true, 0x12345678);
    ASSERT_TRUE(state.IsGameActive());
    ASSERT_EQ(state.GetActiveProcessId(), (u64)0x12345678);
}

TEST(set_game_active_false_resets_pid) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    state.SetGameActive(true, 0x12345678);
    state.SetGameActive(false, 0);
    ASSERT_FALSE(state.IsGameActive());
    ASSERT_EQ(state.GetActiveProcessId(), (u64)0);
}

TEST(set_game_active_false_resets_ldn_state) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    state.SetGameActive(true, 0x1234);
    state.SetLdnState(CommState::StationConnected);
    state.SetGameActive(false, 0);
    ASSERT_EQ(state.GetLdnState(), CommState::None);
}

TEST(set_game_active_false_resets_session_info) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    state.SetGameActive(true, 0x1234);
    state.SetSessionInfo(4, 8, 2, true);
    state.SetGameActive(false, 0);

    u8 count, max, local;
    bool is_host;
    state.GetSessionInfo(count, max, local, is_host);
    ASSERT_EQ(count, 0);
    ASSERT_EQ(max, 0);
    ASSERT_EQ(local, 0);
    ASSERT_FALSE(is_host);
}

// ============================================================================
// LDN State Tests
// ============================================================================

TEST(initially_ldn_state_none) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    ASSERT_EQ(state.GetLdnState(), CommState::None);
}

TEST(set_ldn_state_initialized) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    state.SetLdnState(CommState::Initialized);
    ASSERT_EQ(state.GetLdnState(), CommState::Initialized);
}

TEST(set_ldn_state_station_connected) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    state.SetLdnState(CommState::StationConnected);
    ASSERT_EQ(state.GetLdnState(), CommState::StationConnected);
}

TEST(set_ldn_state_access_point_created) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    state.SetLdnState(CommState::AccessPointCreated);
    ASSERT_EQ(state.GetLdnState(), CommState::AccessPointCreated);
}

TEST(set_ldn_state_error) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    state.SetLdnState(CommState::Error);
    ASSERT_EQ(state.GetLdnState(), CommState::Error);
}

TEST(ldn_state_transitions) {
    auto& state = SharedState::GetInstance();
    state.Reset();

    state.SetLdnState(CommState::Initialized);
    ASSERT_EQ(state.GetLdnState(), CommState::Initialized);

    state.SetLdnState(CommState::Station);
    ASSERT_EQ(state.GetLdnState(), CommState::Station);

    state.SetLdnState(CommState::StationConnected);
    ASSERT_EQ(state.GetLdnState(), CommState::StationConnected);

    state.SetLdnState(CommState::None);
    ASSERT_EQ(state.GetLdnState(), CommState::None);
}

// ============================================================================
// Session Info Tests
// ============================================================================

TEST(initially_session_info_empty) {
    auto& state = SharedState::GetInstance();
    state.Reset();

    u8 count, max, local;
    bool is_host;
    state.GetSessionInfo(count, max, local, is_host);

    ASSERT_EQ(count, 0);
    ASSERT_EQ(max, 0);
    ASSERT_EQ(local, 0);
    ASSERT_FALSE(is_host);
}

TEST(set_session_info_as_host) {
    auto& state = SharedState::GetInstance();
    state.Reset();

    state.SetSessionInfo(4, 8, 0, true);

    u8 count, max, local;
    bool is_host;
    state.GetSessionInfo(count, max, local, is_host);

    ASSERT_EQ(count, 4);
    ASSERT_EQ(max, 8);
    ASSERT_EQ(local, 0);
    ASSERT_TRUE(is_host);
}

TEST(set_session_info_as_client) {
    auto& state = SharedState::GetInstance();
    state.Reset();

    state.SetSessionInfo(3, 8, 2, false);

    u8 count, max, local;
    bool is_host;
    state.GetSessionInfo(count, max, local, is_host);

    ASSERT_EQ(count, 3);
    ASSERT_EQ(max, 8);
    ASSERT_EQ(local, 2);
    ASSERT_FALSE(is_host);
}

TEST(update_session_info_node_count) {
    auto& state = SharedState::GetInstance();
    state.Reset();

    state.SetSessionInfo(2, 8, 0, true);
    state.SetSessionInfo(5, 8, 0, true); // More players joined

    u8 count, max, local;
    bool is_host;
    state.GetSessionInfo(count, max, local, is_host);

    ASSERT_EQ(count, 5);
}

// ============================================================================
// RTT Tests
// ============================================================================

TEST(initially_rtt_zero) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    ASSERT_EQ(state.GetLastRtt(), (u32)0);
}

TEST(set_last_rtt) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    state.SetLastRtt(42);
    ASSERT_EQ(state.GetLastRtt(), (u32)42);
}

TEST(update_last_rtt) {
    auto& state = SharedState::GetInstance();
    state.Reset();

    state.SetLastRtt(100);
    ASSERT_EQ(state.GetLastRtt(), (u32)100);

    state.SetLastRtt(150);
    ASSERT_EQ(state.GetLastRtt(), (u32)150);

    state.SetLastRtt(80);
    ASSERT_EQ(state.GetLastRtt(), (u32)80);
}

TEST(rtt_typical_values) {
    auto& state = SharedState::GetInstance();
    state.Reset();

    // Low latency (LAN)
    state.SetLastRtt(5);
    ASSERT_EQ(state.GetLastRtt(), (u32)5);

    // Medium latency
    state.SetLastRtt(50);
    ASSERT_EQ(state.GetLastRtt(), (u32)50);

    // High latency
    state.SetLastRtt(200);
    ASSERT_EQ(state.GetLastRtt(), (u32)200);
}

// ============================================================================
// Reconnect Request Tests
// ============================================================================

TEST(initially_no_reconnect_request) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    ASSERT_FALSE(state.ConsumeReconnectRequest());
}

TEST(request_reconnect_sets_flag) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    state.RequestReconnect();
    ASSERT_TRUE(state.ConsumeReconnectRequest());
}

TEST(consume_reconnect_clears_flag) {
    auto& state = SharedState::GetInstance();
    state.Reset();
    state.RequestReconnect();
    ASSERT_TRUE(state.ConsumeReconnectRequest()); // First consume
    ASSERT_FALSE(state.ConsumeReconnectRequest()); // Second consume should be false
}

TEST(multiple_reconnect_requests) {
    auto& state = SharedState::GetInstance();
    state.Reset();

    state.RequestReconnect();
    state.RequestReconnect();
    state.RequestReconnect();

    // Should only consume once
    ASSERT_TRUE(state.ConsumeReconnectRequest());
    ASSERT_FALSE(state.ConsumeReconnectRequest());
}

TEST(reconnect_after_consume) {
    auto& state = SharedState::GetInstance();
    state.Reset();

    state.RequestReconnect();
    ASSERT_TRUE(state.ConsumeReconnectRequest());

    // New request after consumption
    state.RequestReconnect();
    ASSERT_TRUE(state.ConsumeReconnectRequest());
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(full_game_session_lifecycle) {
    auto& state = SharedState::GetInstance();
    state.Reset();

    // Game starts
    state.SetGameActive(true, 0xABCD1234);
    ASSERT_TRUE(state.IsGameActive());
    ASSERT_EQ(state.GetActiveProcessId(), (u64)0xABCD1234);

    // LDN initializes
    state.SetLdnState(CommState::Initialized);
    ASSERT_EQ(state.GetLdnState(), CommState::Initialized);

    // Opens station mode
    state.SetLdnState(CommState::Station);
    ASSERT_EQ(state.GetLdnState(), CommState::Station);

    // Connects to network
    state.SetLdnState(CommState::StationConnected);
    state.SetSessionInfo(4, 8, 2, false);

    u8 count, max, local;
    bool is_host;
    state.GetSessionInfo(count, max, local, is_host);
    ASSERT_EQ(count, 4);
    ASSERT_FALSE(is_host);

    // RTT updates
    state.SetLastRtt(45);
    ASSERT_EQ(state.GetLastRtt(), (u32)45);

    // Game exits
    state.SetGameActive(false, 0);
    ASSERT_FALSE(state.IsGameActive());
    ASSERT_EQ(state.GetLdnState(), CommState::None);
}

TEST(host_session_lifecycle) {
    auto& state = SharedState::GetInstance();
    state.Reset();

    // Game starts as host
    state.SetGameActive(true, 0x5678);
    state.SetLdnState(CommState::Initialized);
    state.SetLdnState(CommState::AccessPoint);
    state.SetLdnState(CommState::AccessPointCreated);

    state.SetSessionInfo(1, 8, 0, true);

    u8 count, max, local;
    bool is_host;
    state.GetSessionInfo(count, max, local, is_host);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(local, 0);
    ASSERT_TRUE(is_host);

    // Players join
    state.SetSessionInfo(3, 8, 0, true);
    state.GetSessionInfo(count, max, local, is_host);
    ASSERT_EQ(count, 3);

    // Force reconnect requested
    state.RequestReconnect();
    ASSERT_TRUE(state.ConsumeReconnectRequest());

    // Game ends
    state.SetGameActive(false, 0);
    ASSERT_FALSE(state.IsGameActive());
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("\n========================================\n");
    printf("  SharedState Tests - ryu_ldn_nx\n");
    printf("========================================\n\n");

    // Tests run automatically via static initializers

    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n", g_tests_passed, g_tests_passed + g_tests_failed);
    printf("========================================\n\n");

    return g_tests_failed > 0 ? 1 : 0;
}
