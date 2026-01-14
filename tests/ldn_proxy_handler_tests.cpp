/**
 * @file ldn_proxy_handler_tests.cpp
 * @brief Unit tests for LDN Proxy Handler
 *
 * Tests the P2P proxy management logic that handles virtual network
 * connections tunneled through the RyuLDN server.
 *
 * Test-First: These tests are written BEFORE the implementation.
 */

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

// Include protocol types
#include "protocol/types.hpp"

// Include the proxy handler (will be created)
#include "ldn/ldn_proxy_handler.hpp"

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

/**
 * @brief Create a test ProxyInfo structure
 */
static ProxyInfo make_test_proxy_info(uint32_t src_ip, uint16_t src_port,
                                       uint32_t dest_ip, uint16_t dest_port,
                                       ProtocolType proto = ProtocolType::Udp) {
    ProxyInfo info{};
    info.source_ipv4 = src_ip;
    info.source_port = src_port;
    info.dest_ipv4 = dest_ip;
    info.dest_port = dest_port;
    info.protocol = proto;
    return info;
}

// Track callback invocations
static struct {
    bool config_received;
    uint32_t proxy_ip;
    uint32_t proxy_subnet_mask;

    bool connect_received;
    ProxyInfo connect_info;

    bool connect_reply_received;
    ProxyInfo connect_reply_info;

    bool data_received;
    ProxyInfo data_info;
    std::vector<uint8_t> data_payload;

    bool disconnect_received;
    ProxyInfo disconnect_info;
    int32_t disconnect_reason;

    void reset() {
        config_received = false;
        proxy_ip = 0;
        proxy_subnet_mask = 0;
        connect_received = false;
        connect_info = {};
        connect_reply_received = false;
        connect_reply_info = {};
        data_received = false;
        data_info = {};
        data_payload.clear();
        disconnect_received = false;
        disconnect_info = {};
        disconnect_reason = 0;
    }
} g_callback_state;

// Callback functions
static void on_proxy_config(const ProxyConfig& config) {
    g_callback_state.config_received = true;
    g_callback_state.proxy_ip = config.proxy_ip;
    g_callback_state.proxy_subnet_mask = config.proxy_subnet_mask;
}

static void on_proxy_connect(const ProxyInfo& info) {
    g_callback_state.connect_received = true;
    g_callback_state.connect_info = info;
}

static void on_proxy_connect_reply(const ProxyInfo& info) {
    g_callback_state.connect_reply_received = true;
    g_callback_state.connect_reply_info = info;
}

static void on_proxy_data(const ProxyInfo& info, const uint8_t* data, size_t length) {
    g_callback_state.data_received = true;
    g_callback_state.data_info = info;
    g_callback_state.data_payload.assign(data, data + length);
}

static void on_proxy_disconnect(const ProxyInfo& info, int32_t reason) {
    g_callback_state.disconnect_received = true;
    g_callback_state.disconnect_info = info;
    g_callback_state.disconnect_reason = reason;
}

// ============================================================================
// Tests - Proxy Handler Construction
// ============================================================================

TEST(proxy_handler_default_construction) {
    ldn::LdnProxyHandler handler;
    ASSERT_FALSE(handler.is_configured());
}

TEST(proxy_handler_initial_state) {
    ldn::LdnProxyHandler handler;
    ASSERT_FALSE(handler.is_configured());
    ASSERT_EQ(handler.get_connection_count(), 0);
}

// ============================================================================
// Tests - ProxyConfig Handling
// ============================================================================

TEST(proxy_handler_process_config) {
    g_callback_state.reset();

    ldn::LdnProxyHandler handler;
    handler.set_config_callback(on_proxy_config);

    ProxyConfig config{};
    config.proxy_ip = 0x0A720001;        // 10.114.0.1
    config.proxy_subnet_mask = 0xFFFF0000; // 255.255.0.0

    LdnHeader header = make_test_header(PacketId::ProxyConfig, sizeof(config));
    handler.handle_proxy_config(header, config);

    ASSERT_TRUE(g_callback_state.config_received);
    ASSERT_EQ(g_callback_state.proxy_ip, 0x0A720001);
    ASSERT_EQ(g_callback_state.proxy_subnet_mask, 0xFFFF0000);
    ASSERT_TRUE(handler.is_configured());
}

TEST(proxy_handler_config_stores_values) {
    ldn::LdnProxyHandler handler;

    ProxyConfig config{};
    config.proxy_ip = 0xC0A80001;        // 192.168.0.1
    config.proxy_subnet_mask = 0xFFFFFF00; // 255.255.255.0

    LdnHeader header = make_test_header(PacketId::ProxyConfig, sizeof(config));
    handler.handle_proxy_config(header, config);

    ASSERT_EQ(handler.get_proxy_ip(), 0xC0A80001);
    ASSERT_EQ(handler.get_proxy_subnet_mask(), 0xFFFFFF00);
}

// ============================================================================
// Tests - ProxyConnect Handling
// ============================================================================

TEST(proxy_handler_process_connect) {
    g_callback_state.reset();

    ldn::LdnProxyHandler handler;
    handler.set_connect_callback(on_proxy_connect);

    ProxyConnectRequest req{};
    req.info = make_test_proxy_info(0x0A720001, 1234, 0x0A720002, 5678, ProtocolType::Tcp);

    LdnHeader header = make_test_header(PacketId::ProxyConnect, sizeof(req));
    handler.handle_proxy_connect(header, req);

    ASSERT_TRUE(g_callback_state.connect_received);
    ASSERT_EQ(g_callback_state.connect_info.source_ipv4, 0x0A720001);
    ASSERT_EQ(g_callback_state.connect_info.source_port, 1234);
    ASSERT_EQ(g_callback_state.connect_info.dest_ipv4, 0x0A720002);
    ASSERT_EQ(g_callback_state.connect_info.dest_port, 5678);
}

TEST(proxy_handler_connect_adds_connection) {
    ldn::LdnProxyHandler handler;

    ASSERT_EQ(handler.get_connection_count(), 0);

    ProxyConnectRequest req{};
    req.info = make_test_proxy_info(0x0A720001, 1234, 0x0A720002, 5678);

    LdnHeader header = make_test_header(PacketId::ProxyConnect, sizeof(req));
    handler.handle_proxy_connect(header, req);

    ASSERT_EQ(handler.get_connection_count(), 1);
    ASSERT_TRUE(handler.has_connection(0x0A720001, 1234, 0x0A720002, 5678));
}

// ============================================================================
// Tests - ProxyConnectReply Handling
// ============================================================================

TEST(proxy_handler_process_connect_reply) {
    g_callback_state.reset();

    ldn::LdnProxyHandler handler;
    handler.set_connect_reply_callback(on_proxy_connect_reply);

    ProxyConnectResponse resp{};
    resp.info = make_test_proxy_info(0x0A720002, 5678, 0x0A720001, 1234);

    LdnHeader header = make_test_header(PacketId::ProxyConnectReply, sizeof(resp));
    handler.handle_proxy_connect_reply(header, resp);

    ASSERT_TRUE(g_callback_state.connect_reply_received);
    ASSERT_EQ(g_callback_state.connect_reply_info.source_ipv4, 0x0A720002);
    ASSERT_EQ(g_callback_state.connect_reply_info.dest_ipv4, 0x0A720001);
}

// ============================================================================
// Tests - ProxyData Handling
// ============================================================================

TEST(proxy_handler_process_data) {
    g_callback_state.reset();

    ldn::LdnProxyHandler handler;
    handler.set_data_callback(on_proxy_data);

    ProxyDataHeader data_header{};
    data_header.info = make_test_proxy_info(0x0A720001, 1234, 0x0A720002, 5678);
    data_header.data_length = 4;

    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};

    LdnHeader header = make_test_header(PacketId::ProxyData, sizeof(data_header) + 4);
    handler.handle_proxy_data(header, data_header, payload, 4);

    ASSERT_TRUE(g_callback_state.data_received);
    ASSERT_EQ(g_callback_state.data_info.source_ipv4, 0x0A720001);
    ASSERT_EQ(g_callback_state.data_payload.size(), 4);
    ASSERT_EQ(g_callback_state.data_payload[0], 0xDE);
    ASSERT_EQ(g_callback_state.data_payload[3], 0xEF);
}

TEST(proxy_handler_data_without_callback) {
    ldn::LdnProxyHandler handler;
    // No callback registered

    ProxyDataHeader data_header{};
    data_header.info = make_test_proxy_info(0x0A720001, 1234, 0x0A720002, 5678);
    data_header.data_length = 4;

    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};

    LdnHeader header = make_test_header(PacketId::ProxyData, sizeof(data_header) + 4);

    // Should not crash
    handler.handle_proxy_data(header, data_header, payload, 4);
}

TEST(proxy_handler_data_empty_payload) {
    g_callback_state.reset();

    ldn::LdnProxyHandler handler;
    handler.set_data_callback(on_proxy_data);

    ProxyDataHeader data_header{};
    data_header.info = make_test_proxy_info(0x0A720001, 1234, 0x0A720002, 5678);
    data_header.data_length = 0;

    LdnHeader header = make_test_header(PacketId::ProxyData, sizeof(data_header));
    handler.handle_proxy_data(header, data_header, nullptr, 0);

    ASSERT_TRUE(g_callback_state.data_received);
    ASSERT_EQ(g_callback_state.data_payload.size(), 0);
}

// ============================================================================
// Tests - ProxyDisconnect Handling
// ============================================================================

TEST(proxy_handler_process_disconnect) {
    g_callback_state.reset();

    ldn::LdnProxyHandler handler;
    handler.set_disconnect_callback(on_proxy_disconnect);

    // First establish a connection
    ProxyConnectRequest req{};
    req.info = make_test_proxy_info(0x0A720001, 1234, 0x0A720002, 5678);
    LdnHeader connect_header = make_test_header(PacketId::ProxyConnect, sizeof(req));
    handler.handle_proxy_connect(connect_header, req);

    ASSERT_EQ(handler.get_connection_count(), 1);

    // Then disconnect
    ProxyDisconnectMessage msg{};
    msg.info = make_test_proxy_info(0x0A720001, 1234, 0x0A720002, 5678);
    msg.disconnect_reason = static_cast<int32_t>(DisconnectReason::User);

    LdnHeader header = make_test_header(PacketId::ProxyDisconnect, sizeof(msg));
    handler.handle_proxy_disconnect(header, msg);

    ASSERT_TRUE(g_callback_state.disconnect_received);
    ASSERT_EQ(g_callback_state.disconnect_info.source_ipv4, 0x0A720001);
    ASSERT_EQ(g_callback_state.disconnect_reason, static_cast<int32_t>(DisconnectReason::User));
}

TEST(proxy_handler_disconnect_removes_connection) {
    ldn::LdnProxyHandler handler;

    // Establish connection
    ProxyConnectRequest req{};
    req.info = make_test_proxy_info(0x0A720001, 1234, 0x0A720002, 5678);
    LdnHeader connect_header = make_test_header(PacketId::ProxyConnect, sizeof(req));
    handler.handle_proxy_connect(connect_header, req);

    ASSERT_EQ(handler.get_connection_count(), 1);

    // Disconnect
    ProxyDisconnectMessage msg{};
    msg.info = make_test_proxy_info(0x0A720001, 1234, 0x0A720002, 5678);
    msg.disconnect_reason = 0;

    LdnHeader header = make_test_header(PacketId::ProxyDisconnect, sizeof(msg));
    handler.handle_proxy_disconnect(header, msg);

    ASSERT_EQ(handler.get_connection_count(), 0);
    ASSERT_FALSE(handler.has_connection(0x0A720001, 1234, 0x0A720002, 5678));
}

// ============================================================================
// Tests - Multiple Connections
// ============================================================================

TEST(proxy_handler_multiple_connections) {
    ldn::LdnProxyHandler handler;

    // Add 3 connections
    for (uint16_t port = 1000; port < 1003; port++) {
        ProxyConnectRequest req{};
        req.info = make_test_proxy_info(0x0A720001, port, 0x0A720002, 5678);
        LdnHeader header = make_test_header(PacketId::ProxyConnect, sizeof(req));
        handler.handle_proxy_connect(header, req);
    }

    ASSERT_EQ(handler.get_connection_count(), 3);

    // Remove middle connection
    ProxyDisconnectMessage msg{};
    msg.info = make_test_proxy_info(0x0A720001, 1001, 0x0A720002, 5678);
    msg.disconnect_reason = 0;
    LdnHeader header = make_test_header(PacketId::ProxyDisconnect, sizeof(msg));
    handler.handle_proxy_disconnect(header, msg);

    ASSERT_EQ(handler.get_connection_count(), 2);
    ASSERT_TRUE(handler.has_connection(0x0A720001, 1000, 0x0A720002, 5678));
    ASSERT_FALSE(handler.has_connection(0x0A720001, 1001, 0x0A720002, 5678));
    ASSERT_TRUE(handler.has_connection(0x0A720001, 1002, 0x0A720002, 5678));
}

// ============================================================================
// Tests - Reset
// ============================================================================

TEST(proxy_handler_reset) {
    ldn::LdnProxyHandler handler;

    // Configure
    ProxyConfig config{};
    config.proxy_ip = 0x0A720001;
    config.proxy_subnet_mask = 0xFFFF0000;
    LdnHeader config_header = make_test_header(PacketId::ProxyConfig, sizeof(config));
    handler.handle_proxy_config(config_header, config);

    // Add connections
    for (int i = 0; i < 3; i++) {
        ProxyConnectRequest req{};
        req.info = make_test_proxy_info(0x0A720001, 1000 + i, 0x0A720002, 5678);
        LdnHeader header = make_test_header(PacketId::ProxyConnect, sizeof(req));
        handler.handle_proxy_connect(header, req);
    }

    ASSERT_TRUE(handler.is_configured());
    ASSERT_EQ(handler.get_connection_count(), 3);

    // Reset
    handler.reset();

    ASSERT_FALSE(handler.is_configured());
    ASSERT_EQ(handler.get_connection_count(), 0);
    ASSERT_EQ(handler.get_proxy_ip(), 0);
}

// ============================================================================
// Tests - Protocol Type Filtering
// ============================================================================

TEST(proxy_handler_tcp_and_udp_separate) {
    ldn::LdnProxyHandler handler;

    // Add TCP connection
    ProxyConnectRequest tcp_req{};
    tcp_req.info = make_test_proxy_info(0x0A720001, 1234, 0x0A720002, 5678, ProtocolType::Tcp);
    LdnHeader tcp_header = make_test_header(PacketId::ProxyConnect, sizeof(tcp_req));
    handler.handle_proxy_connect(tcp_header, tcp_req);

    // Add UDP connection with same ports
    ProxyConnectRequest udp_req{};
    udp_req.info = make_test_proxy_info(0x0A720001, 1234, 0x0A720002, 5678, ProtocolType::Udp);
    LdnHeader udp_header = make_test_header(PacketId::ProxyConnect, sizeof(udp_req));
    handler.handle_proxy_connect(udp_header, udp_req);

    // Should have 2 separate connections
    ASSERT_EQ(handler.get_connection_count(), 2);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("\n========================================\n");
    printf("  LDN Proxy Handler Tests\n");
    printf("========================================\n\n");

    // Tests run automatically via static initializers

    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n", g_tests_passed, g_tests_passed + g_tests_failed);
    printf("========================================\n\n");

    return g_tests_failed > 0 ? 1 : 0;
}
