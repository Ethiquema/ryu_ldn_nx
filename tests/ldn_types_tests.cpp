/**
 * @file ldn_types_tests.cpp
 * @brief Unit tests for LDN types and type conversions
 *
 * Tests the LDN data structures and conversions between
 * ams::mitm::ldn types and ryu_ldn::protocol types.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include <cstdio>
#include <cstring>
#include <cstdint>

// Include protocol types (host-compatible)
#include "protocol/types.hpp"

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

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            printf("FAIL\n    Expected: %s != %s\n    at %s:%d\n", #a, #b, __FILE__, __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_STREQ(a, b) \
    do { \
        if (strcmp((a), (b)) != 0) { \
            printf("FAIL\n    Expected: \"%s\" == \"%s\"\n    at %s:%d\n", (a), (b), __FILE__, __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

// ============================================================================
// Protocol Type Structure Size Tests
// ============================================================================

TEST(protocol_mac_address_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::MacAddress), 6u);
}

TEST(protocol_ssid_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::Ssid), 34u);
}

TEST(protocol_session_id_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::SessionId), 16u);
}

TEST(protocol_intent_id_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::IntentId), 16u);
}

TEST(protocol_network_id_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::NetworkId), 32u);
}

TEST(protocol_node_info_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::NodeInfo), 64u);
}

TEST(protocol_common_network_info_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::CommonNetworkInfo), 48u);
}

TEST(protocol_ldn_network_info_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::LdnNetworkInfo), 0x430u);
}

TEST(protocol_network_info_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::NetworkInfo), 0x480u);
}

TEST(protocol_security_config_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::SecurityConfig), 68u);
}

TEST(protocol_user_config_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::UserConfig), 48u);
}

TEST(protocol_network_config_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::NetworkConfig), 32u);
}

TEST(protocol_scan_filter_full_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::ScanFilterFull), 0x60u);  // 96 bytes with Pack=8 alignment
}

TEST(protocol_connect_request_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::ConnectRequest), 0x4FCu);
}

TEST(protocol_create_access_point_request_size) {
    // SecurityConfig(0x44) + UserConfig(0x30) + NetworkConfig(0x20) + RyuNetworkConfig(0x28) = 0xBC
    ASSERT_EQ(sizeof(ryu_ldn::protocol::CreateAccessPointRequest), 0xBCu);
}

// ============================================================================
// Protocol Type Initialization Tests
// ============================================================================

TEST(mac_address_zero_initialized) {
    ryu_ldn::protocol::MacAddress mac{};
    ASSERT_TRUE(mac.is_zero());
}

TEST(mac_address_not_zero_after_set) {
    ryu_ldn::protocol::MacAddress mac{};
    mac.data[0] = 0x12;
    mac.data[1] = 0x34;
    mac.data[2] = 0x56;
    mac.data[3] = 0x78;
    mac.data[4] = 0x9A;
    mac.data[5] = 0xBC;
    ASSERT_FALSE(mac.is_zero());
}

TEST(session_id_zero_initialized) {
    ryu_ldn::protocol::SessionId sid{};
    ASSERT_TRUE(sid.is_zero());
}

TEST(session_id_not_zero_after_set) {
    ryu_ldn::protocol::SessionId sid{};
    sid.data[0] = 0x01;
    ASSERT_FALSE(sid.is_zero());
}

TEST(ssid_default_empty) {
    ryu_ldn::protocol::Ssid ssid{};
    ASSERT_EQ(ssid.length, 0u);
}

TEST(ssid_set_name) {
    ryu_ldn::protocol::Ssid ssid{};
    const char* name = "TestNetwork";
    ssid.length = strlen(name);
    memcpy(ssid.name, name, ssid.length);

    ASSERT_EQ(ssid.length, 11u);
    ASSERT_EQ(memcmp(ssid.name, "TestNetwork", 11), 0);
}

// ============================================================================
// Intent ID Tests
// ============================================================================

TEST(intent_id_fields) {
    ryu_ldn::protocol::IntentId id{};
    id.local_communication_id = 0x0100000000001234ULL;
    id.scene_id = 42;

    ASSERT_EQ(id.local_communication_id, 0x0100000000001234ULL);
    ASSERT_EQ(id.scene_id, 42u);
}

// ============================================================================
// Network ID Tests
// ============================================================================

TEST(network_id_structure) {
    ryu_ldn::protocol::NetworkId nid{};
    nid.intent_id.local_communication_id = 0x0100000000001234ULL;
    nid.intent_id.scene_id = 1;
    nid.session_id.data[0] = 0xAB;

    ASSERT_EQ(nid.intent_id.local_communication_id, 0x0100000000001234ULL);
    ASSERT_EQ(nid.intent_id.scene_id, 1u);
    ASSERT_EQ(nid.session_id.data[0], 0xABu);
}

// ============================================================================
// Security Config Tests
// ============================================================================

TEST(security_config_passphrase) {
    ryu_ldn::protocol::SecurityConfig cfg{};
    cfg.security_mode = 1;
    cfg.passphrase_size = 8;
    memcpy(cfg.passphrase, "password", 8);

    ASSERT_EQ(cfg.security_mode, 1u);
    ASSERT_EQ(cfg.passphrase_size, 8u);
    ASSERT_EQ(memcmp(cfg.passphrase, "password", 8), 0);
}

// ============================================================================
// User Config Tests
// ============================================================================

TEST(user_config_username) {
    ryu_ldn::protocol::UserConfig cfg{};
    const char* name = "Player1";
    memcpy(cfg.user_name, name, strlen(name) + 1);

    ASSERT_STREQ(cfg.user_name, "Player1");
}

// ============================================================================
// Network Config Tests
// ============================================================================

TEST(network_config_fields) {
    ryu_ldn::protocol::NetworkConfig cfg{};
    cfg.intent_id.local_communication_id = 0x0100000000005678ULL;
    cfg.intent_id.scene_id = 2;
    cfg.channel = 6;
    cfg.node_count_max = 8;
    cfg.local_communication_version = 1;

    ASSERT_EQ(cfg.intent_id.local_communication_id, 0x0100000000005678ULL);
    ASSERT_EQ(cfg.channel, 6u);
    ASSERT_EQ(cfg.node_count_max, 8u);
    ASSERT_EQ(cfg.local_communication_version, 1u);
}

// ============================================================================
// Node Info Tests
// ============================================================================

TEST(node_info_fields) {
    ryu_ldn::protocol::NodeInfo node{};
    node.ipv4_address = 0x0A720001; // 10.114.0.1
    node.node_id = 0;
    node.is_connected = 1;
    memcpy(node.user_name, "HostPlayer", 11);

    ASSERT_EQ(node.ipv4_address, 0x0A720001u);
    ASSERT_EQ(node.node_id, 0);
    ASSERT_EQ(node.is_connected, 1);
    ASSERT_STREQ(node.user_name, "HostPlayer");
}

// ============================================================================
// Scan Filter Tests
// ============================================================================

TEST(scan_filter_full_fields) {
    ryu_ldn::protocol::ScanFilterFull filter{};
    filter.flag = 0x01;
    filter.network_type = 2;
    filter.network_id.intent_id.local_communication_id = 0x0100000000001234ULL;
    filter.ssid.length = 4;
    memcpy(filter.ssid.name, "Test", 4);

    ASSERT_EQ(filter.flag, 0x01u);
    ASSERT_EQ(filter.network_type, 2u);
    ASSERT_EQ(filter.network_id.intent_id.local_communication_id, 0x0100000000001234ULL);
    ASSERT_EQ(filter.ssid.length, 4u);
}

// ============================================================================
// Connect Request Tests
// ============================================================================

TEST(connect_request_structure) {
    ryu_ldn::protocol::ConnectRequest req{};

    // Security config
    req.security_config.security_mode = 1;
    req.security_config.passphrase_size = 4;
    memcpy(req.security_config.passphrase, "pass", 4);

    // User config
    memcpy(req.user_config.user_name, "Client", 7);

    // Options
    req.local_communication_version = 1;
    req.option_unknown = 0;

    // Network info (just set network ID)
    req.network_info.network_id.intent_id.local_communication_id = 0x0100000000001234ULL;

    ASSERT_EQ(req.security_config.security_mode, 1u);
    ASSERT_EQ(req.local_communication_version, 1u);
    ASSERT_EQ(req.network_info.network_id.intent_id.local_communication_id, 0x0100000000001234ULL);
}

// ============================================================================
// Create Access Point Request Tests
// ============================================================================

TEST(create_access_point_request_structure) {
    ryu_ldn::protocol::CreateAccessPointRequest req{};

    // Security config
    req.security_config.security_mode = 2;

    // User config
    memcpy(req.user_config.user_name, "Host", 5);

    // Network config
    req.network_config.intent_id.local_communication_id = 0x0100000000005678ULL;
    req.network_config.intent_id.scene_id = 1;
    req.network_config.channel = 1;
    req.network_config.node_count_max = 4;
    req.network_config.local_communication_version = 1;

    ASSERT_EQ(req.security_config.security_mode, 2u);
    ASSERT_STREQ(req.user_config.user_name, "Host");
    ASSERT_EQ(req.network_config.node_count_max, 4u);
}

// ============================================================================
// LDN Network Info Tests
// ============================================================================

TEST(ldn_network_info_nodes) {
    ryu_ldn::protocol::LdnNetworkInfo info{};
    info.node_count_max = 8;
    info.node_count = 2;

    // Set host node
    info.nodes[0].node_id = 0;
    info.nodes[0].is_connected = 1;
    info.nodes[0].ipv4_address = 0x0A720001;
    memcpy(info.nodes[0].user_name, "Host", 5);

    // Set client node
    info.nodes[1].node_id = 1;
    info.nodes[1].is_connected = 1;
    info.nodes[1].ipv4_address = 0x0A720002;
    memcpy(info.nodes[1].user_name, "Client", 7);

    ASSERT_EQ(info.node_count, 2u);
    ASSERT_EQ(info.nodes[0].node_id, 0);
    ASSERT_EQ(info.nodes[1].node_id, 1);
    ASSERT_STREQ(info.nodes[0].user_name, "Host");
    ASSERT_STREQ(info.nodes[1].user_name, "Client");
}

TEST(ldn_network_info_advertise_data) {
    ryu_ldn::protocol::LdnNetworkInfo info{};

    const uint8_t adv_data[] = {0x01, 0x02, 0x03, 0x04};
    info.advertise_data_size = sizeof(adv_data);
    memcpy(info.advertise_data, adv_data, sizeof(adv_data));

    ASSERT_EQ(info.advertise_data_size, 4u);
    ASSERT_EQ(info.advertise_data[0], 0x01u);
    ASSERT_EQ(info.advertise_data[3], 0x04u);
}

// ============================================================================
// Common Network Info Tests
// ============================================================================

TEST(common_network_info_fields) {
    ryu_ldn::protocol::CommonNetworkInfo info{};

    // MAC Address (BSSID)
    info.mac_address.data[0] = 0x12;
    info.mac_address.data[5] = 0x78;

    // SSID
    info.ssid.length = 8;
    memcpy(info.ssid.name, "GameRoom", 8);

    // Channel and link level
    info.channel = 36;
    info.link_level = 3;  // Signal strength indicator (0-3)
    info.network_type = 2;

    ASSERT_EQ(info.mac_address.data[0], 0x12u);
    ASSERT_EQ(info.channel, 36);
    ASSERT_EQ(info.link_level, 3u);
    ASSERT_EQ(info.network_type, 2u);
}

// ============================================================================
// Full Network Info Tests
// ============================================================================

TEST(network_info_complete_structure) {
    ryu_ldn::protocol::NetworkInfo info{};

    // Network ID
    info.network_id.intent_id.local_communication_id = 0x0100000000001234ULL;
    info.network_id.intent_id.scene_id = 1;

    // Common info
    info.common.channel = 6;
    info.common.network_type = 2;
    info.common.ssid.length = 4;
    memcpy(info.common.ssid.name, "Game", 4);

    // LDN info
    info.ldn.node_count_max = 8;
    info.ldn.node_count = 1;
    info.ldn.security_mode = 1;

    ASSERT_EQ(info.network_id.intent_id.local_communication_id, 0x0100000000001234ULL);
    ASSERT_EQ(info.common.channel, 6);
    ASSERT_EQ(info.ldn.node_count_max, 8u);
}

// ============================================================================
// Proxy Header Tests
// ============================================================================

TEST(proxy_data_header_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::ProxyDataHeader), 0x14u);  // 20 bytes
}

TEST(proxy_data_header_fields) {
    ryu_ldn::protocol::ProxyDataHeader header{};
    header.info.source_ipv4 = 0xC0A80101;   // 192.168.1.1
    header.info.source_port = 12345;
    header.info.dest_ipv4 = 0xC0A80102;     // 192.168.1.2
    header.info.dest_port = 54321;
    header.info.protocol = ryu_ldn::protocol::ProtocolType::Udp;
    header.data_length = 100;

    ASSERT_EQ(header.info.source_ipv4, 0xC0A80101u);
    ASSERT_EQ(header.info.dest_ipv4, 0xC0A80102u);
    ASSERT_EQ(header.data_length, 100u);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("\n========================================\n");
    printf("  LDN Types Tests - ryu_ldn_nx\n");
    printf("========================================\n\n");

    // Tests run automatically via static initializers

    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n", g_tests_passed, g_tests_passed + g_tests_failed);
    printf("========================================\n\n");

    return g_tests_failed > 0 ? 1 : 0;
}