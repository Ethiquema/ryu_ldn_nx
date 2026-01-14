/**
 * @file ldn_session_handler_tests.cpp
 * @brief Unit tests for LDN Session Handler
 *
 * Tests the session management logic that processes incoming packets
 * and maintains LDN session state (network info, node list, etc.).
 *
 * Test-First: These tests are written BEFORE the implementation.
 */

#include <cstdio>
#include <cstring>
#include <cstdint>

// Include protocol types
#include "protocol/types.hpp"

// Include the session handler (will be created)
#include "ldn/ldn_session_handler.hpp"

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
// Test Helpers
// ============================================================================

using namespace ryu_ldn;
using namespace ryu_ldn::protocol;

/**
 * @brief Create a test NetworkInfo with specified parameters
 */
static NetworkInfo make_test_network_info(uint8_t node_count, uint8_t max_nodes,
                                           uint64_t game_id = 0x0100000000001234ULL) {
    NetworkInfo info{};
    info.network_id.intent_id.local_communication_id = game_id;
    info.network_id.intent_id.scene_id = 1;
    info.ldn.node_count_max = max_nodes;
    info.ldn.node_count = node_count;

    // Set up nodes
    for (uint8_t i = 0; i < node_count && i < MAX_NODES; i++) {
        info.ldn.nodes[i].node_id = i;
        info.ldn.nodes[i].is_connected = 1;
        info.ldn.nodes[i].ipv4_address = 0x0A720000 + i + 1;  // 10.114.0.X
        snprintf(info.ldn.nodes[i].user_name, sizeof(info.ldn.nodes[i].user_name),
                 "Player%d", i + 1);
    }

    return info;
}

/**
 * @brief Create a test LdnHeader
 */
static LdnHeader make_test_header(PacketId type, int32_t data_size) {
    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(type);
    header.data_size = data_size;
    return header;
}

// Track callback invocations
static struct {
    bool state_changed;
    ldn::LdnSessionState last_old_state;
    ldn::LdnSessionState last_new_state;

    bool network_updated;
    NetworkInfo last_network_info;

    bool scan_result_received;
    int scan_result_count;

    bool scan_completed;

    bool disconnected;
    uint32_t disconnect_reason;

    bool error_received;
    NetworkErrorCode error_code;

    bool rejected;
    uint32_t rejected_node_id;
    uint32_t rejected_reason;

    bool accept_policy_changed;
    AcceptPolicy last_accept_policy;

    void reset() {
        state_changed = false;
        network_updated = false;
        scan_result_received = false;
        scan_result_count = 0;
        scan_completed = false;
        disconnected = false;
        disconnect_reason = 0;
        error_received = false;
        error_code = NetworkErrorCode::None;
        rejected = false;
        rejected_node_id = 0;
        rejected_reason = 0;
        accept_policy_changed = false;
        last_accept_policy = AcceptPolicy::AcceptAll;
    }
} g_callback_state;

// Callback functions
static void on_state_changed(ldn::LdnSessionState old_state, ldn::LdnSessionState new_state) {
    g_callback_state.state_changed = true;
    g_callback_state.last_old_state = old_state;
    g_callback_state.last_new_state = new_state;
}

static void on_network_updated(const NetworkInfo& info) {
    g_callback_state.network_updated = true;
    g_callback_state.last_network_info = info;
}

static void on_scan_result(const NetworkInfo& info) {
    (void)info;
    g_callback_state.scan_result_received = true;
    g_callback_state.scan_result_count++;
}

static void on_scan_completed() {
    g_callback_state.scan_completed = true;
}

static void on_disconnected(uint32_t reason) {
    g_callback_state.disconnected = true;
    g_callback_state.disconnect_reason = reason;
}

static void on_error(NetworkErrorCode code) {
    g_callback_state.error_received = true;
    g_callback_state.error_code = code;
}

static void on_rejected(uint32_t node_id, uint32_t reason) {
    g_callback_state.rejected = true;
    g_callback_state.rejected_node_id = node_id;
    g_callback_state.rejected_reason = reason;
}

static void on_accept_policy_changed(AcceptPolicy policy) {
    g_callback_state.accept_policy_changed = true;
    g_callback_state.last_accept_policy = policy;
}

// ============================================================================
// Tests - Session Handler Construction
// ============================================================================

TEST(session_handler_default_construction) {
    ldn::LdnSessionHandler handler;
    ASSERT_EQ(handler.get_state(), ldn::LdnSessionState::None);
}

TEST(session_handler_initial_state_none) {
    ldn::LdnSessionHandler handler;
    ASSERT_EQ(handler.get_state(), ldn::LdnSessionState::None);
    ASSERT_FALSE(handler.is_in_session());
    ASSERT_FALSE(handler.is_host());
}

// ============================================================================
// Tests - Initialize Response
// ============================================================================

TEST(session_handler_process_initialize) {
    g_callback_state.reset();

    ldn::LdnSessionHandler handler;
    handler.set_state_callback(on_state_changed);

    InitializeMessage msg{};
    msg.id.data[0] = 0x12;
    msg.mac_address.data[0] = 0xAA;

    LdnHeader header = make_test_header(PacketId::Initialize, sizeof(msg));
    handler.handle_initialize(header, msg);

    // Initialize response should transition to Initialized state
    ASSERT_EQ(handler.get_state(), ldn::LdnSessionState::Initialized);
    ASSERT_TRUE(g_callback_state.state_changed);
    ASSERT_EQ(g_callback_state.last_new_state, ldn::LdnSessionState::Initialized);
}

// ============================================================================
// Tests - Connected (Join Success)
// ============================================================================

TEST(session_handler_process_connected) {
    g_callback_state.reset();

    ldn::LdnSessionHandler handler;
    handler.set_state_callback(on_state_changed);
    handler.set_network_updated_callback(on_network_updated);

    // First initialize
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    // Then receive Connected
    NetworkInfo info = make_test_network_info(2, 8);
    LdnHeader header = make_test_header(PacketId::Connected, sizeof(info));
    handler.handle_connected(header, info);

    ASSERT_EQ(handler.get_state(), ldn::LdnSessionState::Station);
    ASSERT_TRUE(handler.is_in_session());
    ASSERT_FALSE(handler.is_host());  // We joined, not created
    ASSERT_TRUE(g_callback_state.network_updated);
    ASSERT_EQ(handler.get_node_count(), 2);
}

TEST(session_handler_connected_stores_network_info) {
    ldn::LdnSessionHandler handler;

    // Initialize first
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    // Then receive Connected
    NetworkInfo info = make_test_network_info(3, 4, 0x0100000000005678ULL);
    info.common.ssid.length = 8;
    memcpy(info.common.ssid.name, "TestRoom", 8);

    LdnHeader header = make_test_header(PacketId::Connected, sizeof(info));
    handler.handle_connected(header, info);

    const NetworkInfo& stored = handler.get_network_info();
    ASSERT_EQ(stored.network_id.intent_id.local_communication_id, 0x0100000000005678ULL);
    ASSERT_EQ(stored.ldn.node_count, 3);
    ASSERT_EQ(stored.ldn.node_count_max, 4);
}

// ============================================================================
// Tests - SyncNetwork
// ============================================================================

TEST(session_handler_process_sync_network) {
    g_callback_state.reset();

    ldn::LdnSessionHandler handler;
    handler.set_network_updated_callback(on_network_updated);

    // Initialize
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    // Join session
    NetworkInfo join_info = make_test_network_info(2, 8);
    LdnHeader join_header = make_test_header(PacketId::Connected, sizeof(join_info));
    handler.handle_connected(join_header, join_info);

    g_callback_state.reset();

    // Receive SyncNetwork with updated node count
    NetworkInfo sync_info = make_test_network_info(3, 8);  // New player joined
    LdnHeader sync_header = make_test_header(PacketId::SyncNetwork, sizeof(sync_info));
    handler.handle_sync_network(sync_header, sync_info);

    ASSERT_TRUE(g_callback_state.network_updated);
    ASSERT_EQ(handler.get_node_count(), 3);
}

TEST(session_handler_sync_network_updates_node_list) {
    ldn::LdnSessionHandler handler;

    // Initialize and join
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    NetworkInfo join_info = make_test_network_info(1, 8);
    LdnHeader join_header = make_test_header(PacketId::Connected, sizeof(join_info));
    handler.handle_connected(join_header, join_info);

    // Sync with more players
    NetworkInfo sync_info = make_test_network_info(4, 8);
    LdnHeader sync_header = make_test_header(PacketId::SyncNetwork, sizeof(sync_info));
    handler.handle_sync_network(sync_header, sync_info);

    ASSERT_EQ(handler.get_node_count(), 4);

    // Verify nodes
    const NetworkInfo& stored = handler.get_network_info();
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(stored.ldn.nodes[i].is_connected, 1);
    }
}

// ============================================================================
// Tests - Scan Results
// ============================================================================

TEST(session_handler_process_scan_reply) {
    g_callback_state.reset();

    ldn::LdnSessionHandler handler;
    handler.set_scan_result_callback(on_scan_result);

    // Initialize
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    // Receive scan result
    NetworkInfo info = make_test_network_info(2, 8);
    LdnHeader header = make_test_header(PacketId::ScanReply, sizeof(info));
    handler.handle_scan_reply(header, info);

    ASSERT_TRUE(g_callback_state.scan_result_received);
    ASSERT_EQ(g_callback_state.scan_result_count, 1);
}

TEST(session_handler_process_multiple_scan_replies) {
    g_callback_state.reset();

    ldn::LdnSessionHandler handler;
    handler.set_scan_result_callback(on_scan_result);

    // Initialize
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    // Receive multiple scan results
    for (int i = 0; i < 5; i++) {
        NetworkInfo info = make_test_network_info(i + 1, 8);
        LdnHeader header = make_test_header(PacketId::ScanReply, sizeof(info));
        handler.handle_scan_reply(header, info);
    }

    ASSERT_EQ(g_callback_state.scan_result_count, 5);
}

TEST(session_handler_process_scan_reply_end) {
    g_callback_state.reset();

    ldn::LdnSessionHandler handler;
    handler.set_scan_completed_callback(on_scan_completed);

    // Initialize
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    // Receive scan end
    LdnHeader header = make_test_header(PacketId::ScanReplyEnd, 0);
    handler.handle_scan_reply_end(header);

    ASSERT_TRUE(g_callback_state.scan_completed);
}

// ============================================================================
// Tests - Ping Handling
// ============================================================================

TEST(session_handler_process_ping_from_server) {
    ldn::LdnSessionHandler handler;

    // Initialize
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    PingMessage msg{};
    msg.requester = 0;  // Server requested
    msg.id = 42;

    LdnHeader header = make_test_header(PacketId::Ping, sizeof(msg));

    // Should return true indicating echo needed
    bool needs_echo = handler.handle_ping(header, msg);
    ASSERT_TRUE(needs_echo);
    ASSERT_EQ(handler.get_last_ping_id(), 42);
}

TEST(session_handler_process_ping_response) {
    ldn::LdnSessionHandler handler;

    // Initialize
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    PingMessage msg{};
    msg.requester = 1;  // Response to our ping
    msg.id = 10;

    LdnHeader header = make_test_header(PacketId::Ping, sizeof(msg));

    // Should return false - no echo needed for response
    bool needs_echo = handler.handle_ping(header, msg);
    ASSERT_FALSE(needs_echo);
}

// ============================================================================
// Tests - Disconnect Handling
// ============================================================================

TEST(session_handler_process_disconnect) {
    g_callback_state.reset();

    ldn::LdnSessionHandler handler;
    handler.set_state_callback(on_state_changed);
    handler.set_disconnected_callback(on_disconnected);

    // Initialize and join
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    NetworkInfo join_info = make_test_network_info(2, 8);
    LdnHeader join_header = make_test_header(PacketId::Connected, sizeof(join_info));
    handler.handle_connected(join_header, join_info);

    g_callback_state.reset();

    // Receive disconnect
    DisconnectMessage msg{};
    msg.disconnect_ip = 0x0A720002;  // Another player's IP

    LdnHeader header = make_test_header(PacketId::Disconnect, sizeof(msg));
    handler.handle_disconnect(header, msg);

    ASSERT_TRUE(g_callback_state.disconnected);
}

TEST(session_handler_disconnect_self_leaves_session) {
    ldn::LdnSessionHandler handler;
    handler.set_state_callback(on_state_changed);

    // Initialize and join
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    NetworkInfo join_info = make_test_network_info(2, 8);
    // Set our local node ID to 1
    handler.set_local_node_id(1);

    LdnHeader join_header = make_test_header(PacketId::Connected, sizeof(join_info));
    handler.handle_connected(join_header, join_info);

    // Simulate leaving
    handler.leave_session();

    ASSERT_EQ(handler.get_state(), ldn::LdnSessionState::Initialized);
    ASSERT_FALSE(handler.is_in_session());
}

// ============================================================================
// Tests - NetworkError Handling
// ============================================================================

TEST(session_handler_process_network_error) {
    g_callback_state.reset();

    ldn::LdnSessionHandler handler;
    handler.set_error_callback(on_error);

    NetworkErrorMessage msg{};
    msg.error_code = static_cast<uint32_t>(NetworkErrorCode::SessionFull);

    LdnHeader header = make_test_header(PacketId::NetworkError, sizeof(msg));
    handler.handle_network_error(header, msg);

    ASSERT_TRUE(g_callback_state.error_received);
    ASSERT_EQ(g_callback_state.error_code, NetworkErrorCode::SessionFull);
}

// ============================================================================
// Tests - Access Point (Host) Mode
// ============================================================================

TEST(session_handler_create_access_point_success) {
    g_callback_state.reset();

    ldn::LdnSessionHandler handler;
    handler.set_state_callback(on_state_changed);
    handler.set_network_updated_callback(on_network_updated);

    // Initialize
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    // Simulate access point creation success (SyncNetwork with us as host)
    NetworkInfo info = make_test_network_info(1, 8);
    info.ldn.nodes[0].node_id = 0;  // We are node 0 (host)
    handler.set_local_node_id(0);

    LdnHeader header = make_test_header(PacketId::SyncNetwork, sizeof(info));
    handler.handle_sync_network(header, info);

    // Should be in AccessPoint state as host
    ASSERT_EQ(handler.get_state(), ldn::LdnSessionState::AccessPoint);
    ASSERT_TRUE(handler.is_host());
    ASSERT_TRUE(handler.is_in_session());
}

// ============================================================================
// Tests - State Queries
// ============================================================================

TEST(session_handler_get_local_node_id) {
    ldn::LdnSessionHandler handler;

    // Initialize and join
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    handler.set_local_node_id(3);

    NetworkInfo join_info = make_test_network_info(4, 8);
    LdnHeader join_header = make_test_header(PacketId::Connected, sizeof(join_info));
    handler.handle_connected(join_header, join_info);

    ASSERT_EQ(handler.get_local_node_id(), 3);
}

TEST(session_handler_get_max_nodes) {
    ldn::LdnSessionHandler handler;

    // Initialize and join
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    NetworkInfo join_info = make_test_network_info(2, 4);
    LdnHeader join_header = make_test_header(PacketId::Connected, sizeof(join_info));
    handler.handle_connected(join_header, join_info);

    ASSERT_EQ(handler.get_max_nodes(), 4);
}

TEST(session_handler_reset) {
    ldn::LdnSessionHandler handler;

    // Initialize and join
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    NetworkInfo join_info = make_test_network_info(2, 8);
    LdnHeader join_header = make_test_header(PacketId::Connected, sizeof(join_info));
    handler.handle_connected(join_header, join_info);

    ASSERT_TRUE(handler.is_in_session());

    // Reset
    handler.reset();

    ASSERT_EQ(handler.get_state(), ldn::LdnSessionState::None);
    ASSERT_FALSE(handler.is_in_session());
    ASSERT_EQ(handler.get_node_count(), 0);
}

// ============================================================================
// Tests - Reject Handling (Control Handlers)
// ============================================================================

TEST(session_handler_process_reject_invokes_callback) {
    g_callback_state.reset();

    ldn::LdnSessionHandler handler;
    handler.set_rejected_callback(on_rejected);

    // Initialize and join
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    NetworkInfo join_info = make_test_network_info(3, 8);
    handler.set_local_node_id(2);  // We are node 2
    LdnHeader join_header = make_test_header(PacketId::Connected, sizeof(join_info));
    handler.handle_connected(join_header, join_info);

    g_callback_state.reset();

    // Another player (node 1) gets rejected
    RejectRequest req{};
    req.node_id = 1;
    req.disconnect_reason = static_cast<uint32_t>(DisconnectReason::Rejected);

    LdnHeader header = make_test_header(PacketId::Reject, sizeof(req));
    handler.handle_reject(header, req);

    // Callback should be invoked
    ASSERT_TRUE(g_callback_state.rejected);
    ASSERT_EQ(g_callback_state.rejected_node_id, 1);
    ASSERT_EQ(g_callback_state.rejected_reason, static_cast<uint32_t>(DisconnectReason::Rejected));

    // We should still be in session (we weren't rejected)
    ASSERT_TRUE(handler.is_in_session());
}

TEST(session_handler_process_reject_self_leaves_session) {
    g_callback_state.reset();

    ldn::LdnSessionHandler handler;
    handler.set_state_callback(on_state_changed);
    handler.set_rejected_callback(on_rejected);

    // Initialize and join
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    NetworkInfo join_info = make_test_network_info(3, 8);
    handler.set_local_node_id(2);  // We are node 2
    LdnHeader join_header = make_test_header(PacketId::Connected, sizeof(join_info));
    handler.handle_connected(join_header, join_info);

    ASSERT_TRUE(handler.is_in_session());
    g_callback_state.reset();

    // We get rejected (node 2)
    RejectRequest req{};
    req.node_id = 2;  // Our node ID
    req.disconnect_reason = static_cast<uint32_t>(DisconnectReason::SystemRequest);

    LdnHeader header = make_test_header(PacketId::Reject, sizeof(req));
    handler.handle_reject(header, req);

    // Callback should be invoked
    ASSERT_TRUE(g_callback_state.rejected);
    ASSERT_EQ(g_callback_state.rejected_node_id, 2);

    // We should leave the session
    ASSERT_FALSE(handler.is_in_session());
    ASSERT_EQ(handler.get_state(), ldn::LdnSessionState::Initialized);
}

TEST(session_handler_process_reject_reply_is_noop) {
    ldn::LdnSessionHandler handler;

    // Initialize
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    // Join session
    NetworkInfo join_info = make_test_network_info(2, 8);
    handler.set_local_node_id(0);  // We are host
    LdnHeader join_header = make_test_header(PacketId::SyncNetwork, sizeof(join_info));
    handler.handle_sync_network(join_header, join_info);

    // RejectReply should not affect state
    LdnHeader header = make_test_header(PacketId::RejectReply, 0);
    handler.handle_reject_reply(header);

    // Should still be in session
    ASSERT_TRUE(handler.is_in_session());
}

// ============================================================================
// Tests - SetAcceptPolicy Handling (Control Handlers)
// ============================================================================

TEST(session_handler_process_set_accept_policy) {
    g_callback_state.reset();

    ldn::LdnSessionHandler handler;
    handler.set_accept_policy_changed_callback(on_accept_policy_changed);

    // Initialize
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    // Default policy should be AcceptAll
    ASSERT_EQ(handler.get_accept_policy(), AcceptPolicy::AcceptAll);

    // Receive SetAcceptPolicy with RejectAll
    SetAcceptPolicyRequest req{};
    req.accept_policy = static_cast<uint8_t>(AcceptPolicy::RejectAll);

    LdnHeader header = make_test_header(PacketId::SetAcceptPolicy, sizeof(req));
    handler.handle_set_accept_policy(header, req);

    // Policy should be updated
    ASSERT_EQ(handler.get_accept_policy(), AcceptPolicy::RejectAll);

    // Callback should be invoked
    ASSERT_TRUE(g_callback_state.accept_policy_changed);
    ASSERT_EQ(g_callback_state.last_accept_policy, AcceptPolicy::RejectAll);
}

TEST(session_handler_set_accept_policy_no_callback) {
    ldn::LdnSessionHandler handler;

    // Initialize
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    // Receive SetAcceptPolicy without callback registered
    SetAcceptPolicyRequest req{};
    req.accept_policy = static_cast<uint8_t>(AcceptPolicy::BlackList);

    LdnHeader header = make_test_header(PacketId::SetAcceptPolicy, sizeof(req));
    handler.handle_set_accept_policy(header, req);

    // Policy should still be updated
    ASSERT_EQ(handler.get_accept_policy(), AcceptPolicy::BlackList);
}

TEST(session_handler_accept_policy_persists_after_reset) {
    ldn::LdnSessionHandler handler;

    // Initialize
    InitializeMessage init{};
    LdnHeader init_header = make_test_header(PacketId::Initialize, sizeof(init));
    handler.handle_initialize(init_header, init);

    // Change policy
    SetAcceptPolicyRequest req{};
    req.accept_policy = static_cast<uint8_t>(AcceptPolicy::RejectAll);

    LdnHeader header = make_test_header(PacketId::SetAcceptPolicy, sizeof(req));
    handler.handle_set_accept_policy(header, req);

    ASSERT_EQ(handler.get_accept_policy(), AcceptPolicy::RejectAll);

    // Reset clears accept policy to default
    handler.reset();

    // After reset, policy goes back to AcceptAll
    ASSERT_EQ(handler.get_accept_policy(), AcceptPolicy::AcceptAll);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("\n========================================\n");
    printf("  LDN Session Handler Tests\n");
    printf("========================================\n\n");

    // Tests run automatically via static initializers

    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n", g_tests_passed, g_tests_passed + g_tests_failed);
    printf("========================================\n\n");

    return g_tests_failed > 0 ? 1 : 0;
}
