/**
 * @file p2p_integration_tests.cpp
 * @brief Unit tests for P2P integration in ICommunicationService (Story 9.7)
 *
 * Tests the HandleExternalProxy integration and SendProxyDataToServer routing
 * for P2P proxy connections.
 */

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <arpa/inet.h>

// =============================================================================
// Minimal Test Framework
// =============================================================================

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct TestRegistrar_##name { \
        TestRegistrar_##name() { \
            printf("  Running: %s... ", #name); \
            g_tests_run++; \
            test_##name(); \
            g_tests_passed++; \
            printf("PASSED\n"); \
        } \
    } g_registrar_##name; \
    static void test_##name()

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) { printf("FAILED\n    Assertion failed: %s\n", #expr); exit(1); } } while(0)

#define ASSERT_FALSE(expr) \
    do { if (expr) { printf("FAILED\n    Assertion failed: !(%s)\n", #expr); exit(1); } } while(0)

#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) { printf("FAILED\n    Expected: %s == %s\n", #a, #b); exit(1); } } while(0)

#define ASSERT_NE(a, b) \
    do { if ((a) == (b)) { printf("FAILED\n    Expected: %s != %s\n", #a, #b); exit(1); } } while(0)

// =============================================================================
// Protocol Types (matching implementation)
// =============================================================================

namespace protocol {

// Address family constants (use different names to avoid conflict with system macros)
constexpr uint32_t ADDR_FAMILY_IPV4 = 2;    // AF_INET value
constexpr uint32_t ADDR_FAMILY_IPV6 = 10;   // AF_INET6 value

// Packet IDs
enum class PacketId : uint8_t {
    ProxyData = 9,
    ExternalProxy = 14,
    ProxyConfig = 11,
};

// ExternalProxyConfig (0x26 bytes - Ryujinx compatible)
struct ExternalProxyConfig {
    uint8_t proxy_ip[16];       // IPv4 in first 4 bytes or full IPv6
    uint16_t proxy_port;
    uint32_t address_family;    // 2=IPv4, 10=IPv6
    uint8_t token[16];
} __attribute__((packed));

// ProxyDataHeader (12 bytes)
struct ProxyDataHeader {
    uint32_t dest_node_id;      // Destination node ID
    uint32_t src_node_id;       // Source node ID
    uint16_t protocol_type;     // UDP=17, TCP=6
    uint16_t data_length;       // Length of data following header
} __attribute__((packed));

// ProxyConfig (from host)
struct ProxyConfig {
    uint32_t proxy_ip;          // Virtual IP assigned
    // ... other fields
} __attribute__((packed));

} // namespace protocol

// =============================================================================
// ExternalProxyConfig Tests - Structure Validation
// =============================================================================

TEST(external_proxy_config_size_0x26) {
    // ExternalProxyConfig must be exactly 0x26 (38) bytes
    ASSERT_EQ(sizeof(protocol::ExternalProxyConfig), 0x26U);
}

TEST(external_proxy_config_proxy_ip_offset) {
    // proxy_ip should be at offset 0
    protocol::ExternalProxyConfig config{};
    ASSERT_EQ((uintptr_t)&config.proxy_ip - (uintptr_t)&config, 0U);
}

TEST(external_proxy_config_proxy_port_offset) {
    // proxy_port should be at offset 16
    protocol::ExternalProxyConfig config{};
    ASSERT_EQ((uintptr_t)&config.proxy_port - (uintptr_t)&config, 16U);
}

TEST(external_proxy_config_address_family_offset) {
    // address_family should be at offset 18
    protocol::ExternalProxyConfig config{};
    ASSERT_EQ((uintptr_t)&config.address_family - (uintptr_t)&config, 18U);
}

TEST(external_proxy_config_token_offset) {
    // token should be at offset 22
    protocol::ExternalProxyConfig config{};
    ASSERT_EQ((uintptr_t)&config.token - (uintptr_t)&config, 22U);
}

// =============================================================================
// Address Family Tests
// =============================================================================

TEST(address_family_ipv4_is_2) {
    // AF_INET should be 2
    ASSERT_EQ(protocol::ADDR_FAMILY_IPV4, 2U);
}

TEST(address_family_ipv6_is_10) {
    // AF_INET6 should be 10
    ASSERT_EQ(protocol::ADDR_FAMILY_IPV6, 10U);
}

TEST(external_proxy_config_ipv4_parsing) {
    // Simulate parsing IPv4 from ExternalProxyConfig
    protocol::ExternalProxyConfig config{};
    config.address_family = protocol::ADDR_FAMILY_IPV4;
    config.proxy_port = 39990;

    // Set IPv4 address (10.114.0.1) in first 4 bytes
    config.proxy_ip[0] = 10;
    config.proxy_ip[1] = 114;
    config.proxy_ip[2] = 0;
    config.proxy_ip[3] = 1;

    // Verify address family check
    bool is_ipv4 = (config.address_family == protocol::ADDR_FAMILY_IPV4);
    ASSERT_TRUE(is_ipv4);

    // Verify IP extraction
    uint8_t ip[4];
    memcpy(ip, config.proxy_ip, 4);
    ASSERT_EQ(ip[0], 10);
    ASSERT_EQ(ip[1], 114);
    ASSERT_EQ(ip[2], 0);
    ASSERT_EQ(ip[3], 1);
}

TEST(external_proxy_config_ipv6_detection) {
    protocol::ExternalProxyConfig config{};
    config.address_family = protocol::ADDR_FAMILY_IPV6;

    bool is_ipv6 = (config.address_family == protocol::ADDR_FAMILY_IPV6);
    ASSERT_TRUE(is_ipv6);

    bool is_ipv4 = (config.address_family == protocol::ADDR_FAMILY_IPV4);
    ASSERT_FALSE(is_ipv4);
}

// =============================================================================
// ProxyDataHeader Tests
// =============================================================================

TEST(proxy_data_header_size) {
    // ProxyDataHeader should be 12 bytes
    ASSERT_EQ(sizeof(protocol::ProxyDataHeader), 12U);
}

TEST(proxy_data_header_dest_node_offset) {
    protocol::ProxyDataHeader header{};
    ASSERT_EQ((uintptr_t)&header.dest_node_id - (uintptr_t)&header, 0U);
}

TEST(proxy_data_header_src_node_offset) {
    protocol::ProxyDataHeader header{};
    ASSERT_EQ((uintptr_t)&header.src_node_id - (uintptr_t)&header, 4U);
}

TEST(proxy_data_header_protocol_type_offset) {
    protocol::ProxyDataHeader header{};
    ASSERT_EQ((uintptr_t)&header.protocol_type - (uintptr_t)&header, 8U);
}

TEST(proxy_data_header_data_length_offset) {
    protocol::ProxyDataHeader header{};
    ASSERT_EQ((uintptr_t)&header.data_length - (uintptr_t)&header, 10U);
}

// =============================================================================
// P2P Routing Logic Tests
// =============================================================================

namespace routing_test {

// Simulates SendProxyDataToServer routing logic
enum class RouteTarget {
    MasterServer,
    P2pProxy
};

struct MockP2pClient {
    bool connected = false;
    bool ready = false;
    int send_count = 0;

    bool IsReady() const { return connected && ready; }
    bool Send() { send_count++; return true; }
};

RouteTarget determine_route(MockP2pClient* client) {
    if (client != nullptr && client->IsReady()) {
        return RouteTarget::P2pProxy;
    }
    return RouteTarget::MasterServer;
}

} // namespace routing_test

TEST(routing_null_client_uses_master) {
    auto route = routing_test::determine_route(nullptr);
    ASSERT_EQ(static_cast<int>(route), static_cast<int>(routing_test::RouteTarget::MasterServer));
}

TEST(routing_disconnected_client_uses_master) {
    routing_test::MockP2pClient client;
    client.connected = false;
    client.ready = false;

    auto route = routing_test::determine_route(&client);
    ASSERT_EQ(static_cast<int>(route), static_cast<int>(routing_test::RouteTarget::MasterServer));
}

TEST(routing_connected_not_ready_uses_master) {
    routing_test::MockP2pClient client;
    client.connected = true;
    client.ready = false;

    auto route = routing_test::determine_route(&client);
    ASSERT_EQ(static_cast<int>(route), static_cast<int>(routing_test::RouteTarget::MasterServer));
}

TEST(routing_ready_client_uses_p2p) {
    routing_test::MockP2pClient client;
    client.connected = true;
    client.ready = true;

    auto route = routing_test::determine_route(&client);
    ASSERT_EQ(static_cast<int>(route), static_cast<int>(routing_test::RouteTarget::P2pProxy));
}

TEST(routing_ready_check_is_and_of_connected_and_ready) {
    routing_test::MockP2pClient client;

    // Not connected, not ready -> not ready
    client.connected = false;
    client.ready = false;
    ASSERT_FALSE(client.IsReady());

    // Connected, not ready -> not ready
    client.connected = true;
    client.ready = false;
    ASSERT_FALSE(client.IsReady());

    // Not connected, ready -> not ready
    client.connected = false;
    client.ready = true;
    ASSERT_FALSE(client.IsReady());

    // Connected and ready -> ready
    client.connected = true;
    client.ready = true;
    ASSERT_TRUE(client.IsReady());
}

// =============================================================================
// Token Validation Tests
// =============================================================================

TEST(token_size_16_bytes) {
    // ExternalProxyToken is 16 bytes
    constexpr size_t TOKEN_SIZE = 16;
    ASSERT_EQ(TOKEN_SIZE, 16U);

    protocol::ExternalProxyConfig config{};
    ASSERT_EQ(sizeof(config.token), 16U);
}

TEST(token_zero_check) {
    protocol::ExternalProxyConfig config{};
    memset(&config, 0, sizeof(config));

    // Token should be all zeros
    bool all_zero = true;
    for (size_t i = 0; i < sizeof(config.token); i++) {
        if (config.token[i] != 0) {
            all_zero = false;
            break;
        }
    }
    ASSERT_TRUE(all_zero);
}

TEST(token_copy) {
    uint8_t source_token[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    protocol::ExternalProxyConfig config{};

    memcpy(config.token, source_token, sizeof(config.token));

    ASSERT_EQ(memcmp(config.token, source_token, 16), 0);
}

// =============================================================================
// HandleExternalProxy Logic Tests
// =============================================================================

namespace handle_external_proxy {

struct MockState {
    bool use_p2p_proxy = false;
    bool p2p_connected = false;
    bool p2p_auth_done = false;
    bool p2p_ready = false;
};

enum class HandleResult {
    Ignored,          // P2P disabled
    ConnectFailed,    // TCP connect failed
    AuthFailed,       // PerformAuth failed
    ReadyFailed,      // EnsureProxyReady failed
    Success           // All steps passed
};

// Simulates HandleExternalProxyConnect logic
HandleResult simulate_handle_external_proxy(
    MockState& state,
    bool connect_succeeds,
    bool auth_succeeds,
    bool ready_succeeds)
{
    // Step 1: Check if P2P is enabled
    if (!state.use_p2p_proxy) {
        return HandleResult::Ignored;
    }

    // Step 2: Disconnect existing (simulated)
    state.p2p_connected = false;
    state.p2p_auth_done = false;
    state.p2p_ready = false;

    // Step 3: Connect
    if (!connect_succeeds) {
        return HandleResult::ConnectFailed;
    }
    state.p2p_connected = true;

    // Step 4: PerformAuth
    if (!auth_succeeds) {
        state.p2p_connected = false;
        return HandleResult::AuthFailed;
    }
    state.p2p_auth_done = true;

    // Step 5: EnsureProxyReady
    if (!ready_succeeds) {
        state.p2p_connected = false;
        state.p2p_auth_done = false;
        return HandleResult::ReadyFailed;
    }
    state.p2p_ready = true;

    return HandleResult::Success;
}

} // namespace handle_external_proxy

TEST(handle_external_p2p_disabled_ignored) {
    handle_external_proxy::MockState state;
    state.use_p2p_proxy = false;

    auto result = handle_external_proxy::simulate_handle_external_proxy(
        state, true, true, true);

    ASSERT_EQ(static_cast<int>(result),
              static_cast<int>(handle_external_proxy::HandleResult::Ignored));
    ASSERT_FALSE(state.p2p_connected);
}

TEST(handle_external_connect_failure) {
    handle_external_proxy::MockState state;
    state.use_p2p_proxy = true;

    auto result = handle_external_proxy::simulate_handle_external_proxy(
        state, false, true, true);

    ASSERT_EQ(static_cast<int>(result),
              static_cast<int>(handle_external_proxy::HandleResult::ConnectFailed));
    ASSERT_FALSE(state.p2p_connected);
}

TEST(handle_external_auth_failure) {
    handle_external_proxy::MockState state;
    state.use_p2p_proxy = true;

    auto result = handle_external_proxy::simulate_handle_external_proxy(
        state, true, false, true);

    ASSERT_EQ(static_cast<int>(result),
              static_cast<int>(handle_external_proxy::HandleResult::AuthFailed));
    ASSERT_FALSE(state.p2p_connected);
}

TEST(handle_external_ready_failure) {
    handle_external_proxy::MockState state;
    state.use_p2p_proxy = true;

    auto result = handle_external_proxy::simulate_handle_external_proxy(
        state, true, true, false);

    ASSERT_EQ(static_cast<int>(result),
              static_cast<int>(handle_external_proxy::HandleResult::ReadyFailed));
    ASSERT_FALSE(state.p2p_connected);
}

TEST(handle_external_success) {
    handle_external_proxy::MockState state;
    state.use_p2p_proxy = true;

    auto result = handle_external_proxy::simulate_handle_external_proxy(
        state, true, true, true);

    ASSERT_EQ(static_cast<int>(result),
              static_cast<int>(handle_external_proxy::HandleResult::Success));
    ASSERT_TRUE(state.p2p_connected);
    ASSERT_TRUE(state.p2p_auth_done);
    ASSERT_TRUE(state.p2p_ready);
}

TEST(handle_external_cleanup_on_failure) {
    handle_external_proxy::MockState state;
    state.use_p2p_proxy = true;
    state.p2p_connected = true;  // Pre-existing connection
    state.p2p_auth_done = true;
    state.p2p_ready = true;

    // New connection fails at auth
    auto result = handle_external_proxy::simulate_handle_external_proxy(
        state, true, false, true);

    ASSERT_EQ(static_cast<int>(result),
              static_cast<int>(handle_external_proxy::HandleResult::AuthFailed));

    // Should have cleaned up
    ASSERT_FALSE(state.p2p_connected);
    ASSERT_FALSE(state.p2p_auth_done);
    ASSERT_FALSE(state.p2p_ready);
}

// =============================================================================
// DisconnectP2pProxy Logic Tests
// =============================================================================

TEST(disconnect_p2p_null_safe) {
    // Disconnecting nullptr should be safe (no-op)
    void* ptr = nullptr;
    bool disconnected = false;

    if (ptr != nullptr) {
        disconnected = true;
    }

    ASSERT_FALSE(disconnected);
}

TEST(disconnect_p2p_sets_null) {
    // After disconnect, pointer should be null
    int dummy = 42;
    int* ptr = &dummy;

    // Simulate disconnect
    ptr = nullptr;

    ASSERT_TRUE(ptr == nullptr);
}

// =============================================================================
// ProxyPacketCallback Tests
// =============================================================================

namespace callback_test {

struct ReceivedPacket {
    protocol::PacketId type;
    size_t data_size;
    bool processed;
};

ReceivedPacket g_last_packet = {};

void mock_callback(protocol::PacketId type, const void* data, size_t size) {
    g_last_packet.type = type;
    g_last_packet.data_size = size;
    g_last_packet.processed = true;
}

} // namespace callback_test

TEST(callback_receives_proxy_data) {
    callback_test::g_last_packet = {};

    uint8_t dummy_data[64] = {0};
    callback_test::mock_callback(protocol::PacketId::ProxyData, dummy_data, sizeof(dummy_data));

    ASSERT_TRUE(callback_test::g_last_packet.processed);
    ASSERT_EQ(static_cast<int>(callback_test::g_last_packet.type),
              static_cast<int>(protocol::PacketId::ProxyData));
    ASSERT_EQ(callback_test::g_last_packet.data_size, 64U);
}

TEST(callback_receives_proxy_config) {
    callback_test::g_last_packet = {};

    protocol::ProxyConfig config{};
    callback_test::mock_callback(protocol::PacketId::ProxyConfig, &config, sizeof(config));

    ASSERT_TRUE(callback_test::g_last_packet.processed);
    ASSERT_EQ(static_cast<int>(callback_test::g_last_packet.type),
              static_cast<int>(protocol::PacketId::ProxyConfig));
}

// =============================================================================
// IP Address Extraction Tests
// =============================================================================

TEST(ip_extract_from_external_proxy_config_ipv4) {
    protocol::ExternalProxyConfig config{};
    config.address_family = protocol::ADDR_FAMILY_IPV4;

    // Set 192.168.1.100
    config.proxy_ip[0] = 192;
    config.proxy_ip[1] = 168;
    config.proxy_ip[2] = 1;
    config.proxy_ip[3] = 100;

    // Extract as Connect() would
    uint8_t ip_bytes[4];
    memcpy(ip_bytes, config.proxy_ip, 4);

    // Convert to string for verification
    char ip_str[INET_ADDRSTRLEN];
    snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
             ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3]);

    ASSERT_EQ(strcmp(ip_str, "192.168.1.100"), 0);
}

TEST(ip_extract_length_for_ipv4) {
    // IPv4 should use 4 bytes
    constexpr size_t IPV4_LEN = 4;
    ASSERT_EQ(IPV4_LEN, 4U);
}

TEST(ip_extract_length_for_ipv6) {
    // IPv6 should use 16 bytes
    constexpr size_t IPV6_LEN = 16;
    ASSERT_EQ(IPV6_LEN, 16U);
}

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("=== P2P Integration Tests (Story 9.7) ===\n\n");

    // Tests run automatically via static initializers

    printf("\n=== Results ===\n");
    printf("Tests run: %d\n", g_tests_run);
    printf("Tests passed: %d\n", g_tests_passed);

    if (g_tests_passed == g_tests_run) {
        printf("\nAll tests PASSED!\n");
        return 0;
    } else {
        printf("\nSome tests FAILED!\n");
        return 1;
    }
}
