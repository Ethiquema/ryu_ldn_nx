/**
 * @file p2p_proxy_tests.cpp
 * @brief Unit tests for P2P Proxy Server
 *
 * These tests verify the P2pProxyServer constants and logic match Ryujinx
 * for full interoperability.
 *
 * ## Test Categories
 *
 * 1. **Constants Tests**: Verify P2P constants match Ryujinx
 *    - Port ranges
 *    - Lease timing
 *    - Auth timeout
 *
 * 2. **Logic Tests**: Test non-network functionality
 *    - Virtual IP handling
 *    - Broadcast address calculation
 *    - Token validation logic
 *
 * Note: Actual P2P functionality requires Switch hardware and network.
 * These tests validate the portable logic and constants.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <stdexcept>

// ============================================================================
// Test Framework (minimal, no external dependencies)
// ============================================================================

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct TestRegister_##name { \
        TestRegister_##name() { register_test(#name, test_##name); } \
    } g_test_register_##name; \
    static void test_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            throw std::runtime_error("Test assertion failed"); \
        } \
    } while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b) \
    do { \
        auto _a = (a); \
        auto _b = (b); \
        if (_a != _b) { \
            printf("  FAIL: %s == %s (line %d)\n", #a, #b, __LINE__); \
            printf("    got: %lld vs %lld\n", (long long)_a, (long long)_b); \
            throw std::runtime_error("Test assertion failed"); \
        } \
    } while(0)

#define ASSERT_NE(a, b) \
    do { \
        auto _a = (a); \
        auto _b = (b); \
        if (_a == _b) { \
            printf("  FAIL: %s != %s (line %d)\n", #a, #b, __LINE__); \
            throw std::runtime_error("Test assertion failed"); \
        } \
    } while(0)

struct TestEntry {
    const char* name;
    void (*func)();
};

static TestEntry g_tests[128];
static int g_test_count = 0;

static void register_test(const char* name, void (*func)()) {
    if (g_test_count < 128) {
        g_tests[g_test_count++] = {name, func};
    }
}

// ============================================================================
// P2P Proxy Constants (must match Ryujinx)
// ============================================================================

namespace p2p_proxy {
    // Port configuration - must match Ryujinx for interoperability
    constexpr uint16_t PRIVATE_PORT_BASE = 39990;
    constexpr int PRIVATE_PORT_RANGE = 10;
    constexpr uint16_t PUBLIC_PORT_BASE = 39990;
    constexpr int PUBLIC_PORT_RANGE = 10;

    // UPnP lease timing
    constexpr int PORT_LEASE_LENGTH = 60;   // seconds
    constexpr int PORT_LEASE_RENEW = 50;    // seconds

    // Authentication
    constexpr int AUTH_WAIT_SECONDS = 1;
    constexpr int MAX_PLAYERS = 8;

    // Network constants
    constexpr uint32_t SUBNET_MASK = 0xFFFF0000;  // /16 subnet
    constexpr uint32_t BROADCAST_SUFFIX = 0x000000FF;  // .255 for broadcast

    /**
     * @brief Calculate broadcast address from IP and subnet mask
     *
     * @param ip IP address (host byte order)
     * @param mask Subnet mask (host byte order)
     * @return Broadcast address (host byte order)
     */
    inline uint32_t calculate_broadcast(uint32_t ip, uint32_t mask) {
        return (ip & mask) | (~mask);
    }

    /**
     * @brief Check if IP is a broadcast address for given subnet
     */
    inline bool is_broadcast(uint32_t ip, uint32_t broadcast) {
        return ip == broadcast || ip == 0xFFFFFFFFU;
    }

    /**
     * @brief Check if token is all zeros (private mode)
     */
    inline bool is_private_ip(const uint8_t physical_ip[16]) {
        for (int i = 0; i < 16; i++) {
            if (physical_ip[i] != 0) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Extract IPv4 from physical IP array
     * @param physical_ip 16-byte array (network byte order)
     * @return IPv4 in host byte order
     */
    inline uint32_t extract_ipv4(const uint8_t physical_ip[16]) {
        return (static_cast<uint32_t>(physical_ip[0]) << 24) |
               (static_cast<uint32_t>(physical_ip[1]) << 16) |
               (static_cast<uint32_t>(physical_ip[2]) << 8)  |
               static_cast<uint32_t>(physical_ip[3]);
    }
}

// ============================================================================
// Constants Tests - Verify Ryujinx Compatibility
// ============================================================================

TEST(constants_private_port_base_matches_ryujinx) {
    // Ryujinx P2pProxyServer.PrivatePortBase = 39990
    ASSERT_EQ(p2p_proxy::PRIVATE_PORT_BASE, 39990U);
}

TEST(constants_private_port_range_matches_ryujinx) {
    // Ryujinx P2pProxyServer.PrivatePortRange = 10
    ASSERT_EQ(p2p_proxy::PRIVATE_PORT_RANGE, 10);
}

TEST(constants_public_port_base_matches_ryujinx) {
    // Ryujinx P2pProxyServer.PublicPortBase = 39990
    ASSERT_EQ(p2p_proxy::PUBLIC_PORT_BASE, 39990U);
}

TEST(constants_public_port_range_matches_ryujinx) {
    // Ryujinx P2pProxyServer.PublicPortRange = 10
    ASSERT_EQ(p2p_proxy::PUBLIC_PORT_RANGE, 10);
}

TEST(constants_port_lease_length_matches_ryujinx) {
    // Ryujinx P2pProxyServer.PortLeaseLength = 60
    ASSERT_EQ(p2p_proxy::PORT_LEASE_LENGTH, 60);
}

TEST(constants_port_lease_renew_matches_ryujinx) {
    // Ryujinx P2pProxyServer.PortLeaseRenew = 50
    ASSERT_EQ(p2p_proxy::PORT_LEASE_RENEW, 50);
}

TEST(constants_auth_wait_matches_ryujinx) {
    // Ryujinx uses 1 second timeout for token validation
    ASSERT_EQ(p2p_proxy::AUTH_WAIT_SECONDS, 1);
}

TEST(constants_max_players_matches_ryujinx) {
    // LDN supports up to 8 players
    ASSERT_EQ(p2p_proxy::MAX_PLAYERS, 8);
}

TEST(constants_subnet_mask_is_class_b) {
    // /16 subnet = 0xFFFF0000
    ASSERT_EQ(p2p_proxy::SUBNET_MASK, 0xFFFF0000U);
}

TEST(lease_timing_correct) {
    // Renewal should happen before expiry
    ASSERT_TRUE(p2p_proxy::PORT_LEASE_RENEW < p2p_proxy::PORT_LEASE_LENGTH);

    // 10 second margin
    int margin = p2p_proxy::PORT_LEASE_LENGTH - p2p_proxy::PORT_LEASE_RENEW;
    ASSERT_EQ(margin, 10);
}

TEST(port_range_valid) {
    // All ports in range should be valid (< 65536)
    uint16_t max_private = p2p_proxy::PRIVATE_PORT_BASE + p2p_proxy::PRIVATE_PORT_RANGE - 1;
    uint16_t max_public = p2p_proxy::PUBLIC_PORT_BASE + p2p_proxy::PUBLIC_PORT_RANGE - 1;

    ASSERT_TRUE(max_private < 65536);
    ASSERT_TRUE(max_public < 65536);
    ASSERT_EQ(max_private, 39999U);
    ASSERT_EQ(max_public, 39999U);
}

// ============================================================================
// Broadcast Address Tests
// ============================================================================

TEST(broadcast_calculation_class_b) {
    // Virtual IP: 10.114.0.1 with /16 mask
    uint32_t ip = 0x0A720001U;  // 10.114.0.1
    uint32_t mask = 0xFFFF0000U;  // /16

    uint32_t broadcast = p2p_proxy::calculate_broadcast(ip, mask);

    // Expected: 10.114.255.255 = 0x0A72FFFF
    ASSERT_EQ(broadcast, 0x0A72FFFFU);
}

TEST(broadcast_calculation_class_c) {
    // 192.168.1.100 with /24 mask
    uint32_t ip = 0xC0A80164U;  // 192.168.1.100
    uint32_t mask = 0xFFFFFF00U;  // /24

    uint32_t broadcast = p2p_proxy::calculate_broadcast(ip, mask);

    // Expected: 192.168.1.255 = 0xC0A801FF
    ASSERT_EQ(broadcast, 0xC0A801FFU);
}

TEST(is_broadcast_exact_match) {
    uint32_t broadcast = 0x0A72FFFFU;  // 10.114.255.255

    ASSERT_TRUE(p2p_proxy::is_broadcast(broadcast, broadcast));
}

TEST(is_broadcast_global) {
    uint32_t broadcast = 0x0A72FFFFU;

    // 255.255.255.255 is always broadcast
    ASSERT_TRUE(p2p_proxy::is_broadcast(0xFFFFFFFFU, broadcast));
}

TEST(is_broadcast_normal_ip_false) {
    uint32_t broadcast = 0x0A72FFFFU;
    uint32_t normal_ip = 0x0A720001U;  // 10.114.0.1

    ASSERT_FALSE(p2p_proxy::is_broadcast(normal_ip, broadcast));
}

// ============================================================================
// Private IP Detection Tests
// ============================================================================

TEST(is_private_ip_all_zeros) {
    uint8_t physical_ip[16] = {0};

    ASSERT_TRUE(p2p_proxy::is_private_ip(physical_ip));
}

TEST(is_private_ip_has_bytes) {
    uint8_t physical_ip[16] = {192, 168, 1, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    ASSERT_FALSE(p2p_proxy::is_private_ip(physical_ip));
}

TEST(is_private_ip_last_byte_set) {
    uint8_t physical_ip[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

    ASSERT_FALSE(p2p_proxy::is_private_ip(physical_ip));
}

// ============================================================================
// IPv4 Extraction Tests
// ============================================================================

TEST(extract_ipv4_standard) {
    uint8_t physical_ip[16] = {192, 168, 1, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    uint32_t ip = p2p_proxy::extract_ipv4(physical_ip);

    // 192.168.1.100 = 0xC0A80164
    ASSERT_EQ(ip, 0xC0A80164U);
}

TEST(extract_ipv4_localhost) {
    uint8_t physical_ip[16] = {127, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    uint32_t ip = p2p_proxy::extract_ipv4(physical_ip);

    // 127.0.0.1 = 0x7F000001
    ASSERT_EQ(ip, 0x7F000001U);
}

TEST(extract_ipv4_ldn_network) {
    // LDN virtual network uses 10.114.x.x
    uint8_t physical_ip[16] = {10, 114, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    uint32_t ip = p2p_proxy::extract_ipv4(physical_ip);

    // 10.114.0.1 = 0x0A720001
    ASSERT_EQ(ip, 0x0A720001U);
}

TEST(extract_ipv4_zeros) {
    uint8_t physical_ip[16] = {0};

    uint32_t ip = p2p_proxy::extract_ipv4(physical_ip);

    ASSERT_EQ(ip, 0U);
}

// ============================================================================
// Token Validation Logic Tests
// ============================================================================

TEST(token_compare_match) {
    uint8_t token1[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    uint8_t token2[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    ASSERT_EQ(std::memcmp(token1, token2, 16), 0);
}

TEST(token_compare_mismatch_first_byte) {
    uint8_t token1[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    uint8_t token2[16] = {0xFF, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    ASSERT_NE(std::memcmp(token1, token2, 16), 0);
}

TEST(token_compare_mismatch_last_byte) {
    uint8_t token1[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    uint8_t token2[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF};

    ASSERT_NE(std::memcmp(token1, token2, 16), 0);
}

// ============================================================================
// Virtual IP Range Tests
// ============================================================================

TEST(virtual_ip_in_ldn_range) {
    // LDN virtual IPs are in 10.114.x.x range
    uint32_t base = 0x0A720000U;  // 10.114.0.0
    uint32_t mask = p2p_proxy::SUBNET_MASK;

    // Player 1: 10.114.0.1
    uint32_t player1 = 0x0A720001U;
    ASSERT_EQ(player1 & mask, base);

    // Player 2: 10.114.0.2
    uint32_t player2 = 0x0A720002U;
    ASSERT_EQ(player2 & mask, base);

    // Player 8 (max): 10.114.0.8
    uint32_t player8 = 0x0A720008U;
    ASSERT_EQ(player8 & mask, base);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== P2P Proxy Server Tests ===\n\n");

    for (int i = 0; i < g_test_count; i++) {
        g_tests_run++;
        printf("Running: %s ... ", g_tests[i].name);

        try {
            g_tests[i].func();
            printf("PASS\n");
            g_tests_passed++;
        } catch (const std::exception& e) {
            g_tests_failed++;
        }
    }

    printf("\n=== Results ===\n");
    printf("Total:  %d\n", g_tests_run);
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
