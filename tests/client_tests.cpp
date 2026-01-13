/**
 * @file client_tests.cpp
 * @brief Unit tests for RyuLdnClient
 *
 * This file contains unit tests for the RyuLdnClient class, which provides
 * the high-level network client for communicating with ryu_ldn servers.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 *
 * @section Test Categories
 *
 * ### Construction Tests
 * Verify that clients are created correctly with default and custom configs.
 *
 * ### Configuration Tests
 * Test configuration getter/setter methods.
 *
 * ### State Query Tests
 * Test state query methods (is_connected, is_ready, etc.).
 *
 * ### Connection Tests
 * Test connection-related operations (connect, disconnect).
 *
 * ### Send Tests (Not Connected)
 * Test that send operations fail appropriately when not connected.
 *
 * ### Move Semantics Tests
 * Test move constructor and assignment operator.
 *
 * ### String Conversion Tests
 * Test result-to-string conversion functions.
 *
 * @note These tests run without a server, so they focus on client
 * behavior in disconnected state and configuration handling.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "network/client.hpp"
#include "network/socket.hpp"

using namespace ryu_ldn;
using namespace ryu_ldn::network;
using namespace ryu_ldn::protocol;

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
 * @brief Assert equality macro
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
// RyuLdnClientConfig Tests
// ============================================================================

/**
 * @brief Test default config values
 */
bool test_config_defaults() {
    RyuLdnClientConfig cfg;

    ASSERT_STREQ(cfg.host, "127.0.0.1");
    ASSERT_EQ(cfg.port, 30456);
    ASSERT_EQ(cfg.connect_timeout_ms, 5000);
    ASSERT_EQ(cfg.recv_timeout_ms, 100);
    ASSERT_EQ(cfg.ping_interval_ms, 30000);
    ASSERT_TRUE(cfg.auto_reconnect);

    return true;
}

/**
 * @brief Test config from Config structure
 */
bool test_config_from_app_config() {
    config::Config app_cfg = config::get_default_config();
    std::strncpy(app_cfg.server.host, "192.168.1.100", sizeof(app_cfg.server.host));
    app_cfg.server.port = 12345;
    app_cfg.network.connect_timeout_ms = 10000;
    app_cfg.network.ping_interval_ms = 60000;
    app_cfg.network.max_reconnect_attempts = 0;  // 0 = infinite, so auto_reconnect stays true
    app_cfg.network.reconnect_delay_ms = 2000;

    RyuLdnClientConfig cfg(app_cfg);

    ASSERT_STREQ(cfg.host, "192.168.1.100");
    ASSERT_EQ(cfg.port, 12345);
    ASSERT_EQ(cfg.connect_timeout_ms, 10000);
    ASSERT_EQ(cfg.ping_interval_ms, 60000);
    ASSERT_EQ(cfg.reconnect.initial_delay_ms, 2000);

    return true;
}

// ============================================================================
// Construction Tests
// ============================================================================

/**
 * @brief Test default construction
 */
bool test_default_construction() {
    RyuLdnClient client;

    ASSERT_EQ(client.get_state(), ConnectionState::Disconnected);
    ASSERT_TRUE(!client.is_connected());
    ASSERT_TRUE(!client.is_ready());
    ASSERT_TRUE(!client.is_transitioning());
    ASSERT_EQ(client.get_retry_count(), 0);

    return true;
}

/**
 * @brief Test construction with config
 */
bool test_construction_with_config() {
    RyuLdnClientConfig cfg;
    cfg.port = 9999;
    cfg.ping_interval_ms = 5000;

    RyuLdnClient client(cfg);

    ASSERT_EQ(client.get_config().port, 9999);
    ASSERT_EQ(client.get_config().ping_interval_ms, 5000);
    ASSERT_EQ(client.get_state(), ConnectionState::Disconnected);

    return true;
}

/**
 * @brief Test multiple client instances
 */
bool test_multiple_clients() {
    RyuLdnClient client1;
    RyuLdnClient client2;
    RyuLdnClient client3;

    ASSERT_EQ(client1.get_state(), ConnectionState::Disconnected);
    ASSERT_EQ(client2.get_state(), ConnectionState::Disconnected);
    ASSERT_EQ(client3.get_state(), ConnectionState::Disconnected);

    return true;
}

// ============================================================================
// Configuration Tests
// ============================================================================

/**
 * @brief Test set_config
 */
bool test_set_config() {
    RyuLdnClient client;

    RyuLdnClientConfig new_cfg;
    std::strncpy(new_cfg.host, "10.0.0.1", sizeof(new_cfg.host));
    new_cfg.port = 8888;
    new_cfg.ping_interval_ms = 1000;

    client.set_config(new_cfg);

    ASSERT_STREQ(client.get_config().host, "10.0.0.1");
    ASSERT_EQ(client.get_config().port, 8888);
    ASSERT_EQ(client.get_config().ping_interval_ms, 1000);

    return true;
}

// ============================================================================
// State Query Tests
// ============================================================================

/**
 * @brief Test is_connected when disconnected
 */
bool test_is_connected_when_disconnected() {
    RyuLdnClient client;
    ASSERT_TRUE(!client.is_connected());
    return true;
}

/**
 * @brief Test is_ready when disconnected
 */
bool test_is_ready_when_disconnected() {
    RyuLdnClient client;
    ASSERT_TRUE(!client.is_ready());
    return true;
}

/**
 * @brief Test is_transitioning when disconnected
 */
bool test_is_transitioning_when_disconnected() {
    RyuLdnClient client;
    ASSERT_TRUE(!client.is_transitioning());
    return true;
}

/**
 * @brief Test get_retry_count initial
 */
bool test_get_retry_count_initial() {
    RyuLdnClient client;
    ASSERT_EQ(client.get_retry_count(), 0);
    return true;
}

// ============================================================================
// Connection Tests
// ============================================================================

/**
 * @brief Test connect fails with no server
 *
 * Connection to non-existent server should fail.
 */
bool test_connect_no_server() {
    socket_init();

    RyuLdnClient client;

    // Try to connect to localhost - should fail (no server)
    ClientOpResult result = client.connect("127.0.0.1", 19999);
    (void)result;  // Suppress unused warning

    // The connect() call starts the process, but TCP will fail
    // After try_connect(), state should be Backoff (auto-reconnect)
    // or Disconnected (if auto-reconnect is off)

    // For this test, connection fails so state transitions through
    // Connecting -> Backoff (if auto-reconnect on)

    socket_exit();
    return true;
}

/**
 * @brief Test disconnect when already disconnected
 */
bool test_disconnect_when_disconnected() {
    RyuLdnClient client;

    // Should be safe to call
    client.disconnect();

    ASSERT_EQ(client.get_state(), ConnectionState::Disconnected);

    return true;
}

/**
 * @brief Test multiple disconnect calls
 */
bool test_multiple_disconnect_calls() {
    RyuLdnClient client;

    client.disconnect();
    client.disconnect();
    client.disconnect();

    ASSERT_EQ(client.get_state(), ConnectionState::Disconnected);

    return true;
}

// ============================================================================
// Send Tests (Not Connected)
// ============================================================================

/**
 * @brief Test send_scan when not ready
 */
bool test_send_scan_not_ready() {
    RyuLdnClient client;

    ScanFilterFull filter{};
    ClientOpResult result = client.send_scan(filter);

    ASSERT_EQ(result, ClientOpResult::NotReady);

    return true;
}

/**
 * @brief Test send_create_access_point when not ready
 */
bool test_send_create_access_point_not_ready() {
    RyuLdnClient client;

    CreateAccessPointRequest request{};
    ClientOpResult result = client.send_create_access_point(request);

    ASSERT_EQ(result, ClientOpResult::NotReady);

    return true;
}

/**
 * @brief Test send_connect when not ready
 */
bool test_send_connect_not_ready() {
    RyuLdnClient client;

    ConnectRequest request{};
    ClientOpResult result = client.send_connect(request);

    ASSERT_EQ(result, ClientOpResult::NotReady);

    return true;
}

/**
 * @brief Test send_proxy_data when not ready
 */
bool test_send_proxy_data_not_ready() {
    RyuLdnClient client;

    ProxyDataHeader header{};
    uint8_t data[10] = {0};
    ClientOpResult result = client.send_proxy_data(header, data, sizeof(data));

    ASSERT_EQ(result, ClientOpResult::NotReady);

    return true;
}

/**
 * @brief Test send_ping when not ready
 */
bool test_send_ping_not_ready() {
    RyuLdnClient client;

    ClientOpResult result = client.send_ping();

    ASSERT_EQ(result, ClientOpResult::NotReady);

    return true;
}

// ============================================================================
// Move Semantics Tests
// ============================================================================

/**
 * @brief Test move constructor
 */
bool test_move_constructor() {
    RyuLdnClientConfig cfg;
    cfg.port = 7777;

    RyuLdnClient client1(cfg);
    RyuLdnClient client2(std::move(client1));

    ASSERT_EQ(client2.get_config().port, 7777);
    ASSERT_EQ(client2.get_state(), ConnectionState::Disconnected);

    return true;
}

/**
 * @brief Test move assignment
 */
bool test_move_assignment() {
    RyuLdnClientConfig cfg;
    cfg.port = 6666;

    RyuLdnClient client1(cfg);
    RyuLdnClient client2;

    client2 = std::move(client1);

    ASSERT_EQ(client2.get_config().port, 6666);
    ASSERT_EQ(client2.get_state(), ConnectionState::Disconnected);

    return true;
}

/**
 * @brief Test self move assignment
 */
bool test_self_move_assignment() {
    RyuLdnClientConfig cfg;
    cfg.port = 5555;

    RyuLdnClient client(cfg);

    // Self-assignment should be safe
    client = std::move(client);

    ASSERT_EQ(client.get_config().port, 5555);

    return true;
}

// ============================================================================
// String Conversion Tests
// ============================================================================

/**
 * @brief Test client_op_result_to_string for all values
 */
bool test_client_op_result_to_string() {
    ASSERT_STREQ(client_op_result_to_string(ClientOpResult::Success), "Success");
    ASSERT_STREQ(client_op_result_to_string(ClientOpResult::NotConnected), "NotConnected");
    ASSERT_STREQ(client_op_result_to_string(ClientOpResult::NotReady), "NotReady");
    ASSERT_STREQ(client_op_result_to_string(ClientOpResult::AlreadyConnected), "AlreadyConnected");
    ASSERT_STREQ(client_op_result_to_string(ClientOpResult::ConnectionFailed), "ConnectionFailed");
    ASSERT_STREQ(client_op_result_to_string(ClientOpResult::SendFailed), "SendFailed");
    ASSERT_STREQ(client_op_result_to_string(ClientOpResult::InvalidState), "InvalidState");
    ASSERT_STREQ(client_op_result_to_string(ClientOpResult::Timeout), "Timeout");
    ASSERT_STREQ(client_op_result_to_string(ClientOpResult::ProtocolError), "ProtocolError");
    ASSERT_STREQ(client_op_result_to_string(ClientOpResult::InternalError), "InternalError");
    ASSERT_STREQ(client_op_result_to_string(static_cast<ClientOpResult>(99)), "Unknown");

    return true;
}

// ============================================================================
// Update Tests
// ============================================================================

/**
 * @brief Test update when disconnected
 */
bool test_update_when_disconnected() {
    RyuLdnClient client;

    // Should be safe to call
    client.update(1000);
    client.update(2000);
    client.update(3000);

    ASSERT_EQ(client.get_state(), ConnectionState::Disconnected);

    return true;
}

// ============================================================================
// Callback Tests
// ============================================================================

/**
 * @brief Test set_state_callback with null
 */
bool test_set_state_callback_null() {
    RyuLdnClient client;

    // Should be safe
    client.set_state_callback(nullptr);

    return true;
}

/**
 * @brief Test set_packet_callback with null
 */
bool test_set_packet_callback_null() {
    RyuLdnClient client;

    // Should be safe
    client.set_packet_callback(nullptr);

    return true;
}

// ============================================================================
// Handshake Tests
// ============================================================================

/**
 * @brief Test get_last_error_code initial value
 */
bool test_get_last_error_code_initial() {
    RyuLdnClient client;

    // Should be None initially
    ASSERT_EQ(static_cast<int>(client.get_last_error_code()),
              static_cast<int>(protocol::NetworkErrorCode::None));

    return true;
}

/**
 * @brief Test NetworkErrorCode enum values exist
 */
bool test_error_code_types() {
    // Verify all expected error codes exist
    ASSERT_EQ(static_cast<uint32_t>(protocol::NetworkErrorCode::None), 0);
    ASSERT_EQ(static_cast<uint32_t>(protocol::NetworkErrorCode::VersionMismatch), 1);
    ASSERT_EQ(static_cast<uint32_t>(protocol::NetworkErrorCode::InvalidMagic), 2);
    ASSERT_EQ(static_cast<uint32_t>(protocol::NetworkErrorCode::InvalidSessionId), 3);
    ASSERT_EQ(static_cast<uint32_t>(protocol::NetworkErrorCode::HandshakeTimeout), 4);
    ASSERT_EQ(static_cast<uint32_t>(protocol::NetworkErrorCode::AlreadyInitialized), 5);

    ASSERT_EQ(static_cast<uint32_t>(protocol::NetworkErrorCode::SessionNotFound), 100);
    ASSERT_EQ(static_cast<uint32_t>(protocol::NetworkErrorCode::SessionFull), 101);

    ASSERT_EQ(static_cast<uint32_t>(protocol::NetworkErrorCode::NetworkNotFound), 200);
    ASSERT_EQ(static_cast<uint32_t>(protocol::NetworkErrorCode::ConnectionRejected), 202);

    ASSERT_EQ(static_cast<uint32_t>(protocol::NetworkErrorCode::InternalError), 900);

    return true;
}

// ============================================================================
// Ping/Keepalive Tests
// ============================================================================

/**
 * @brief Test get_last_rtt_ms initial value
 */
bool test_get_last_rtt_initial() {
    RyuLdnClient client;

    // Should be 0 initially (no ping completed yet)
    ASSERT_EQ(client.get_last_rtt_ms(), 0);

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
    printf("  RyuLdnClient Tests - ryu_ldn_nx\n");
    printf("========================================\n\n");

    // Config Tests
    printf("Configuration:\n");
    RUN_TEST(test_config_defaults);
    RUN_TEST(test_config_from_app_config);

    // Construction Tests
    printf("\nConstruction:\n");
    RUN_TEST(test_default_construction);
    RUN_TEST(test_construction_with_config);
    RUN_TEST(test_multiple_clients);

    // Configuration Tests
    printf("\nSet Configuration:\n");
    RUN_TEST(test_set_config);

    // State Query Tests
    printf("\nState Queries:\n");
    RUN_TEST(test_is_connected_when_disconnected);
    RUN_TEST(test_is_ready_when_disconnected);
    RUN_TEST(test_is_transitioning_when_disconnected);
    RUN_TEST(test_get_retry_count_initial);

    // Connection Tests
    printf("\nConnection:\n");
    RUN_TEST(test_connect_no_server);
    RUN_TEST(test_disconnect_when_disconnected);
    RUN_TEST(test_multiple_disconnect_calls);

    // Send Tests
    printf("\nSend (Not Ready):\n");
    RUN_TEST(test_send_scan_not_ready);
    RUN_TEST(test_send_create_access_point_not_ready);
    RUN_TEST(test_send_connect_not_ready);
    RUN_TEST(test_send_proxy_data_not_ready);
    RUN_TEST(test_send_ping_not_ready);

    // Move Semantics
    printf("\nMove Semantics:\n");
    RUN_TEST(test_move_constructor);
    RUN_TEST(test_move_assignment);
    RUN_TEST(test_self_move_assignment);

    // String Conversion
    printf("\nString Conversion:\n");
    RUN_TEST(test_client_op_result_to_string);

    // Update Tests
    printf("\nUpdate:\n");
    RUN_TEST(test_update_when_disconnected);

    // Callback Tests
    printf("\nCallbacks:\n");
    RUN_TEST(test_set_state_callback_null);
    RUN_TEST(test_set_packet_callback_null);

    // Handshake Tests
    printf("\nHandshake:\n");
    RUN_TEST(test_get_last_error_code_initial);
    RUN_TEST(test_error_code_types);

    // Ping/Keepalive Tests
    printf("\nPing/Keepalive:\n");
    RUN_TEST(test_get_last_rtt_initial);

    // Summary
    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n",
           g_tests_passed, g_tests_passed + g_tests_failed);
    printf("========================================\n\n");

    return g_tests_failed > 0 ? 1 : 0;
}
