/**
 * @file ldn_packet_dispatcher_tests.cpp
 * @brief Unit tests for LDN Packet Dispatcher
 *
 * Tests the packet dispatch system that routes incoming packets
 * to registered handlers based on PacketId.
 *
 * Test-First: These tests are written BEFORE the implementation.
 */

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

// Include protocol types
#include "protocol/types.hpp"

// Include the dispatcher (will be created)
#include "ldn/ldn_packet_dispatcher.hpp"

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

// Track received callbacks
static struct {
    bool initialize_called;
    bool connected_called;
    bool sync_network_called;
    bool scan_reply_called;
    bool scan_reply_end_called;
    bool disconnect_called;
    bool ping_called;
    bool network_error_called;
    bool proxy_config_called;
    bool proxy_connect_called;
    bool proxy_connect_reply_called;
    bool proxy_data_called;
    bool proxy_disconnect_called;
    bool reject_called;
    bool reject_reply_called;
    bool set_accept_policy_called;

    // Store received data for verification
    InitializeMessage last_initialize;
    NetworkInfo last_network_info;
    PingMessage last_ping;
    NetworkErrorMessage last_error;
    ProxyConfig last_proxy_config;
    ProxyDataHeader last_proxy_header;
    std::vector<uint8_t> last_proxy_data;

    void reset() {
        initialize_called = false;
        connected_called = false;
        sync_network_called = false;
        scan_reply_called = false;
        scan_reply_end_called = false;
        disconnect_called = false;
        ping_called = false;
        network_error_called = false;
        proxy_config_called = false;
        proxy_connect_called = false;
        proxy_connect_reply_called = false;
        proxy_data_called = false;
        proxy_disconnect_called = false;
        reject_called = false;
        reject_reply_called = false;
        set_accept_policy_called = false;
        last_proxy_data.clear();
    }
} g_callback_state;

// ============================================================================
// Callback Functions
// ============================================================================

static void on_initialize(const LdnHeader& header, const InitializeMessage& msg) {
    g_callback_state.initialize_called = true;
    g_callback_state.last_initialize = msg;
}

static void on_connected(const LdnHeader& header, const NetworkInfo& info) {
    g_callback_state.connected_called = true;
    g_callback_state.last_network_info = info;
}

static void on_sync_network(const LdnHeader& header, const NetworkInfo& info) {
    g_callback_state.sync_network_called = true;
    g_callback_state.last_network_info = info;
}

static void on_scan_reply(const LdnHeader& header, const NetworkInfo& info) {
    g_callback_state.scan_reply_called = true;
    g_callback_state.last_network_info = info;
}

static void on_scan_reply_end(const LdnHeader& header) {
    g_callback_state.scan_reply_end_called = true;
}

static void on_disconnect(const LdnHeader& header, const DisconnectMessage& msg) {
    g_callback_state.disconnect_called = true;
}

static void on_ping(const LdnHeader& header, const PingMessage& msg) {
    g_callback_state.ping_called = true;
    g_callback_state.last_ping = msg;
}

static void on_network_error(const LdnHeader& header, const NetworkErrorMessage& msg) {
    g_callback_state.network_error_called = true;
    g_callback_state.last_error = msg;
}

static void on_proxy_config(const LdnHeader& header, const ProxyConfig& cfg) {
    g_callback_state.proxy_config_called = true;
    g_callback_state.last_proxy_config = cfg;
}

static void on_proxy_connect(const LdnHeader& header, const ProxyConnectRequest& req) {
    g_callback_state.proxy_connect_called = true;
}

static void on_proxy_connect_reply(const LdnHeader& header, const ProxyConnectResponse& resp) {
    g_callback_state.proxy_connect_reply_called = true;
}

static void on_proxy_data(const LdnHeader& header, const ProxyDataHeader& hdr, const uint8_t* data, size_t size) {
    g_callback_state.proxy_data_called = true;
    g_callback_state.last_proxy_header = hdr;
    g_callback_state.last_proxy_data.assign(data, data + size);
}

static void on_proxy_disconnect(const LdnHeader& header, const ProxyDisconnectMessage& msg) {
    g_callback_state.proxy_disconnect_called = true;
}

static void on_reject(const LdnHeader& header, const RejectRequest& req) {
    g_callback_state.reject_called = true;
}

static void on_reject_reply(const LdnHeader& header) {
    g_callback_state.reject_reply_called = true;
}

static void on_set_accept_policy(const LdnHeader& header, const SetAcceptPolicyRequest& req) {
    g_callback_state.set_accept_policy_called = true;
}

// ============================================================================
// Tests - Basic Dispatcher Functionality
// ============================================================================

TEST(dispatcher_default_construction) {
    ldn::PacketDispatcher dispatcher;
    // Should construct without crashing
    ASSERT_TRUE(true);
}

TEST(dispatcher_register_callback) {
    ldn::PacketDispatcher dispatcher;
    dispatcher.set_initialize_handler(on_initialize);
    // Should not crash
    ASSERT_TRUE(true);
}

TEST(dispatcher_dispatch_initialize) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_initialize_handler(on_initialize);

    // Create a valid Initialize packet
    InitializeMessage msg{};
    msg.id.data[0] = 0x12;
    msg.id.data[1] = 0x34;
    msg.mac_address.data[0] = 0xAA;
    msg.mac_address.data[5] = 0xBB;

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::Initialize);
    header.data_size = sizeof(InitializeMessage);

    dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));

    ASSERT_TRUE(g_callback_state.initialize_called);
    ASSERT_EQ(g_callback_state.last_initialize.id.data[0], 0x12);
    ASSERT_EQ(g_callback_state.last_initialize.mac_address.data[0], 0xAA);
}

TEST(dispatcher_dispatch_ping) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_ping_handler(on_ping);

    PingMessage msg{};
    msg.requester = 0;
    msg.id = 42;

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::Ping);
    header.data_size = sizeof(PingMessage);

    dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));

    ASSERT_TRUE(g_callback_state.ping_called);
    ASSERT_EQ(g_callback_state.last_ping.requester, 0);
    ASSERT_EQ(g_callback_state.last_ping.id, 42);
}

TEST(dispatcher_dispatch_network_error) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_network_error_handler(on_network_error);

    NetworkErrorMessage msg{};
    msg.error_code = static_cast<uint32_t>(NetworkErrorCode::SessionFull);

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::NetworkError);
    header.data_size = sizeof(NetworkErrorMessage);

    dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));

    ASSERT_TRUE(g_callback_state.network_error_called);
    ASSERT_EQ(g_callback_state.last_error.error_code, static_cast<uint32_t>(NetworkErrorCode::SessionFull));
}

TEST(dispatcher_dispatch_connected) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_connected_handler(on_connected);

    NetworkInfo info{};
    info.network_id.intent_id.local_communication_id = 0x0100000000001234ULL;
    info.ldn.node_count = 2;

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::Connected);
    header.data_size = sizeof(NetworkInfo);

    dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&info), sizeof(info));

    ASSERT_TRUE(g_callback_state.connected_called);
    ASSERT_EQ(g_callback_state.last_network_info.network_id.intent_id.local_communication_id, 0x0100000000001234ULL);
    ASSERT_EQ(g_callback_state.last_network_info.ldn.node_count, 2);
}

TEST(dispatcher_dispatch_sync_network) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_sync_network_handler(on_sync_network);

    NetworkInfo info{};
    info.ldn.node_count = 4;

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::SyncNetwork);
    header.data_size = sizeof(NetworkInfo);

    dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&info), sizeof(info));

    ASSERT_TRUE(g_callback_state.sync_network_called);
    ASSERT_EQ(g_callback_state.last_network_info.ldn.node_count, 4);
}

TEST(dispatcher_dispatch_scan_reply) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_scan_reply_handler(on_scan_reply);

    NetworkInfo info{};
    info.ldn.node_count_max = 8;

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::ScanReply);
    header.data_size = sizeof(NetworkInfo);

    dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&info), sizeof(info));

    ASSERT_TRUE(g_callback_state.scan_reply_called);
    ASSERT_EQ(g_callback_state.last_network_info.ldn.node_count_max, 8);
}

TEST(dispatcher_dispatch_scan_reply_end) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_scan_reply_end_handler(on_scan_reply_end);

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::ScanReplyEnd);
    header.data_size = 0;

    dispatcher.dispatch(header, nullptr, 0);

    ASSERT_TRUE(g_callback_state.scan_reply_end_called);
}

TEST(dispatcher_dispatch_disconnect) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_disconnect_handler(on_disconnect);

    DisconnectMessage msg{};
    msg.disconnect_ip = 0xC0A80101;

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::Disconnect);
    header.data_size = sizeof(DisconnectMessage);

    dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));

    ASSERT_TRUE(g_callback_state.disconnect_called);
}

// ============================================================================
// Tests - Proxy Packets
// ============================================================================

TEST(dispatcher_dispatch_proxy_config) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_proxy_config_handler(on_proxy_config);

    ProxyConfig cfg{};
    cfg.proxy_ip = 0x0A720001;
    cfg.proxy_subnet_mask = 0xFFFF0000;

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::ProxyConfig);
    header.data_size = sizeof(ProxyConfig);

    dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg));

    ASSERT_TRUE(g_callback_state.proxy_config_called);
    ASSERT_EQ(g_callback_state.last_proxy_config.proxy_ip, 0x0A720001u);
    ASSERT_EQ(g_callback_state.last_proxy_config.proxy_subnet_mask, 0xFFFF0000u);
}

TEST(dispatcher_dispatch_proxy_connect) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_proxy_connect_handler(on_proxy_connect);

    ProxyConnectRequest req{};
    req.info.source_ipv4 = 0xC0A80101;
    req.info.dest_ipv4 = 0xC0A80102;

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::ProxyConnect);
    header.data_size = sizeof(ProxyConnectRequest);

    dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&req), sizeof(req));

    ASSERT_TRUE(g_callback_state.proxy_connect_called);
}

TEST(dispatcher_dispatch_proxy_connect_reply) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_proxy_connect_reply_handler(on_proxy_connect_reply);

    ProxyConnectResponse resp{};
    resp.info.source_ipv4 = 0xC0A80101;

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::ProxyConnectReply);
    header.data_size = sizeof(ProxyConnectResponse);

    dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&resp), sizeof(resp));

    ASSERT_TRUE(g_callback_state.proxy_connect_reply_called);
}

TEST(dispatcher_dispatch_proxy_data) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_proxy_data_handler(on_proxy_data);

    // Build packet: ProxyDataHeader + data
    uint8_t game_data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};

    ProxyDataHeader hdr{};
    hdr.info.source_ipv4 = 0xC0A80101;
    hdr.info.source_port = 12345;
    hdr.info.dest_ipv4 = 0xC0A80102;
    hdr.info.dest_port = 54321;
    hdr.info.protocol = ProtocolType::Udp;
    hdr.data_length = sizeof(game_data);

    // Combine header + data
    std::vector<uint8_t> packet;
    packet.resize(sizeof(ProxyDataHeader) + sizeof(game_data));
    memcpy(packet.data(), &hdr, sizeof(hdr));
    memcpy(packet.data() + sizeof(hdr), game_data, sizeof(game_data));

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::ProxyData);
    header.data_size = packet.size();

    dispatcher.dispatch(header, packet.data(), packet.size());

    ASSERT_TRUE(g_callback_state.proxy_data_called);
    ASSERT_EQ(g_callback_state.last_proxy_header.info.source_ipv4, 0xC0A80101u);
    ASSERT_EQ(g_callback_state.last_proxy_header.info.dest_port, 54321);
    ASSERT_EQ(g_callback_state.last_proxy_header.data_length, sizeof(game_data));
    ASSERT_EQ(g_callback_state.last_proxy_data.size(), sizeof(game_data));
    ASSERT_EQ(g_callback_state.last_proxy_data[0], 0xDE);
    ASSERT_EQ(g_callback_state.last_proxy_data[3], 0xEF);
}

TEST(dispatcher_dispatch_proxy_disconnect) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_proxy_disconnect_handler(on_proxy_disconnect);

    ProxyDisconnectMessage msg{};
    msg.info.source_ipv4 = 0xC0A80101;
    msg.disconnect_reason = 1;

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::ProxyDisconnect);
    header.data_size = sizeof(ProxyDisconnectMessage);

    dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));

    ASSERT_TRUE(g_callback_state.proxy_disconnect_called);
}

// ============================================================================
// Tests - Control Packets
// ============================================================================

TEST(dispatcher_dispatch_reject) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_reject_handler(on_reject);

    RejectRequest req{};
    req.node_id = 3;
    req.disconnect_reason = static_cast<uint32_t>(DisconnectReason::Rejected);

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::Reject);
    header.data_size = sizeof(RejectRequest);

    dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&req), sizeof(req));

    ASSERT_TRUE(g_callback_state.reject_called);
}

TEST(dispatcher_dispatch_reject_reply) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_reject_reply_handler(on_reject_reply);

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::RejectReply);
    header.data_size = 0;

    dispatcher.dispatch(header, nullptr, 0);

    ASSERT_TRUE(g_callback_state.reject_reply_called);
}

TEST(dispatcher_dispatch_set_accept_policy) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_accept_policy_handler(on_set_accept_policy);

    SetAcceptPolicyRequest req{};
    req.accept_policy = static_cast<uint32_t>(AcceptPolicy::RejectAll);

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::SetAcceptPolicy);
    header.data_size = sizeof(SetAcceptPolicyRequest);

    dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&req), sizeof(req));

    ASSERT_TRUE(g_callback_state.set_accept_policy_called);
}

// ============================================================================
// Tests - Error Handling
// ============================================================================

TEST(dispatcher_no_handler_registered) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    // Don't register any handler

    PingMessage msg{};
    msg.id = 1;

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::Ping);
    header.data_size = sizeof(PingMessage);

    // Should not crash, just silently ignore
    dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));

    ASSERT_FALSE(g_callback_state.ping_called);
}

TEST(dispatcher_undersized_packet) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_ping_handler(on_ping);

    // Only 1 byte, but PingMessage needs 2
    uint8_t incomplete_data[1] = {0x00};

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketId::Ping);
    header.data_size = 1;  // Too small

    // Should not crash, should not call handler
    dispatcher.dispatch(header, incomplete_data, 1);

    ASSERT_FALSE(g_callback_state.ping_called);
}

TEST(dispatcher_unknown_packet_type) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;

    uint8_t data[10] = {0};

    LdnHeader header{};
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = 99;  // Unknown type
    header.data_size = 10;

    // Should not crash, just ignore
    dispatcher.dispatch(header, data, 10);

    ASSERT_TRUE(true);  // If we get here, we didn't crash
}

// ============================================================================
// Tests - Multiple Handlers
// ============================================================================

TEST(dispatcher_multiple_packet_types) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_ping_handler(on_ping);
    dispatcher.set_network_error_handler(on_network_error);
    dispatcher.set_connected_handler(on_connected);

    // Dispatch Ping
    {
        PingMessage msg{};
        msg.id = 1;
        LdnHeader header{};
        header.magic = PROTOCOL_MAGIC;
        header.version = PROTOCOL_VERSION;
        header.type = static_cast<uint8_t>(PacketId::Ping);
        header.data_size = sizeof(PingMessage);
        dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
    }

    // Dispatch NetworkError
    {
        NetworkErrorMessage msg{};
        msg.error_code = 100;
        LdnHeader header{};
        header.magic = PROTOCOL_MAGIC;
        header.version = PROTOCOL_VERSION;
        header.type = static_cast<uint8_t>(PacketId::NetworkError);
        header.data_size = sizeof(NetworkErrorMessage);
        dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
    }

    ASSERT_TRUE(g_callback_state.ping_called);
    ASSERT_TRUE(g_callback_state.network_error_called);
    ASSERT_FALSE(g_callback_state.connected_called);  // Not dispatched
}

TEST(dispatcher_clear_handler) {
    g_callback_state.reset();

    ldn::PacketDispatcher dispatcher;
    dispatcher.set_ping_handler(on_ping);

    // First dispatch should work
    {
        PingMessage msg{};
        msg.id = 1;
        LdnHeader header{};
        header.magic = PROTOCOL_MAGIC;
        header.version = PROTOCOL_VERSION;
        header.type = static_cast<uint8_t>(PacketId::Ping);
        header.data_size = sizeof(PingMessage);
        dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
    }
    ASSERT_TRUE(g_callback_state.ping_called);

    // Clear handler
    g_callback_state.reset();
    dispatcher.set_ping_handler(nullptr);

    // Second dispatch should not call handler
    {
        PingMessage msg{};
        msg.id = 2;
        LdnHeader header{};
        header.magic = PROTOCOL_MAGIC;
        header.version = PROTOCOL_VERSION;
        header.type = static_cast<uint8_t>(PacketId::Ping);
        header.data_size = sizeof(PingMessage);
        dispatcher.dispatch(header, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
    }
    ASSERT_FALSE(g_callback_state.ping_called);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("\n========================================\n");
    printf("  LDN Packet Dispatcher Tests\n");
    printf("========================================\n\n");

    // Tests run automatically via static initializers

    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n", g_tests_passed, g_tests_passed + g_tests_failed);
    printf("========================================\n\n");

    return g_tests_failed > 0 ? 1 : 0;
}
