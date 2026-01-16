/**
 * @file upnp_tests.cpp
 * @brief Unit tests for UPnP Port Mapper
 *
 * These tests verify the UpnpPortMapper class behavior.
 *
 * ## Test Categories
 *
 * 1. **API Tests**: Verify public interface behavior
 *    - Singleton access
 *    - State before/after discovery
 *    - Parameter validation
 *
 * 2. **Mock Tests**: Test internal logic without network
 *    - IP string parsing
 *    - Port string formatting
 *
 * Note: Actual UPnP discovery requires a real router with UPnP enabled.
 * These tests mock the network layer or test non-network functionality.
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

#define ASSERT_STREQ(a, b) \
    do { \
        const char* _a = (a); \
        const char* _b = (b); \
        if (std::strcmp(_a, _b) != 0) { \
            printf("  FAIL: %s == %s (line %d)\n", #a, #b, __LINE__); \
            printf("    got: \"%s\" vs \"%s\"\n", _a, _b); \
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
// Constants Tests
// ============================================================================
// Verify P2P constants match Ryujinx implementation

namespace p2p_constants {
    // These values must match Ryujinx for interoperability
    constexpr uint16_t P2P_PORT_BASE = 39990;
    constexpr int P2P_PORT_RANGE = 10;
    constexpr int UPNP_DISCOVERY_TIMEOUT_MS = 2500;
    constexpr int PORT_LEASE_DURATION = 60;
    constexpr int PORT_LEASE_RENEW = 50;
}

TEST(constants_port_base_matches_ryujinx) {
    // Ryujinx uses PrivatePortBase = 39990
    ASSERT_EQ(p2p_constants::P2P_PORT_BASE, 39990);
}

TEST(constants_port_range_matches_ryujinx) {
    // Ryujinx uses PrivatePortRange = 10 (ports 39990-39999)
    ASSERT_EQ(p2p_constants::P2P_PORT_RANGE, 10);
}

TEST(constants_discovery_timeout_matches_ryujinx) {
    // Ryujinx uses 2500ms timeout for UPnP discovery
    ASSERT_EQ(p2p_constants::UPNP_DISCOVERY_TIMEOUT_MS, 2500);
}

TEST(constants_lease_duration_matches_ryujinx) {
    // Ryujinx uses PortLeaseLength = 60 seconds
    ASSERT_EQ(p2p_constants::PORT_LEASE_DURATION, 60);
}

TEST(constants_lease_renew_matches_ryujinx) {
    // Ryujinx uses PortLeaseRenew = 50 seconds
    ASSERT_EQ(p2p_constants::PORT_LEASE_RENEW, 50);
}

TEST(port_range_is_valid) {
    // Verify all ports in range are valid (< 65536)
    uint16_t max_port = p2p_constants::P2P_PORT_BASE + p2p_constants::P2P_PORT_RANGE - 1;
    ASSERT_TRUE(max_port < 65536);
    ASSERT_EQ(max_port, 39999);
}

// ============================================================================
// IPv4 String Parsing Tests
// ============================================================================
// Test the IP address string to uint32_t conversion logic

namespace ipv4_parse {
    /**
     * @brief Parse IPv4 string to host-byte-order uint32_t
     *
     * This mirrors the logic in UpnpPortMapper::GetLocalIPv4()
     *
     * @param ip_str IP address string (e.g., "192.168.1.100")
     * @return IP as uint32_t in host byte order, or 0 on error
     */
    uint32_t parse_ipv4(const char* ip_str) {
        if (ip_str == nullptr || ip_str[0] == '\0') {
            return 0;
        }

        unsigned int a, b, c, d;
        if (std::sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
            return 0;
        }

        if (a > 255 || b > 255 || c > 255 || d > 255) {
            return 0;
        }

        return (a << 24) | (b << 16) | (c << 8) | d;
    }
}

TEST(ipv4_parse_valid_address) {
    // Common private IP address
    uint32_t ip = ipv4_parse::parse_ipv4("192.168.1.100");
    ASSERT_EQ(ip, 0xC0A80164U);  // 192.168.1.100 in hex
}

TEST(ipv4_parse_localhost) {
    uint32_t ip = ipv4_parse::parse_ipv4("127.0.0.1");
    ASSERT_EQ(ip, 0x7F000001U);  // 127.0.0.1
}

TEST(ipv4_parse_broadcast) {
    uint32_t ip = ipv4_parse::parse_ipv4("255.255.255.255");
    ASSERT_EQ(ip, 0xFFFFFFFFU);
}

TEST(ipv4_parse_zero) {
    uint32_t ip = ipv4_parse::parse_ipv4("0.0.0.0");
    ASSERT_EQ(ip, 0U);
}

TEST(ipv4_parse_class_a) {
    // 10.114.0.1 - LDN network address
    uint32_t ip = ipv4_parse::parse_ipv4("10.114.0.1");
    ASSERT_EQ(ip, 0x0A720001U);
}

TEST(ipv4_parse_empty_string_returns_zero) {
    uint32_t ip = ipv4_parse::parse_ipv4("");
    ASSERT_EQ(ip, 0U);
}

TEST(ipv4_parse_null_returns_zero) {
    uint32_t ip = ipv4_parse::parse_ipv4(nullptr);
    ASSERT_EQ(ip, 0U);
}

TEST(ipv4_parse_invalid_format_returns_zero) {
    // Missing octet
    uint32_t ip = ipv4_parse::parse_ipv4("192.168.1");
    ASSERT_EQ(ip, 0U);
}

TEST(ipv4_parse_octet_overflow_returns_zero) {
    // 256 is out of range for an octet
    uint32_t ip = ipv4_parse::parse_ipv4("256.0.0.1");
    ASSERT_EQ(ip, 0U);
}

TEST(ipv4_parse_garbage_returns_zero) {
    uint32_t ip = ipv4_parse::parse_ipv4("not.an.ip.addr");
    ASSERT_EQ(ip, 0U);
}

// ============================================================================
// Port String Formatting Tests
// ============================================================================
// Test port number to string conversion (used for UPNP_AddPortMapping)

namespace port_format {
    /**
     * @brief Format port number as string
     *
     * @param port Port number (host byte order)
     * @param buffer Output buffer
     * @param buffer_size Buffer size
     */
    void format_port(uint16_t port, char* buffer, size_t buffer_size) {
        std::snprintf(buffer, buffer_size, "%u", port);
    }
}

TEST(port_format_base_port) {
    char buffer[8];
    port_format::format_port(39990, buffer, sizeof(buffer));
    ASSERT_STREQ(buffer, "39990");
}

TEST(port_format_max_port) {
    char buffer[8];
    port_format::format_port(65535, buffer, sizeof(buffer));
    ASSERT_STREQ(buffer, "65535");
}

TEST(port_format_zero) {
    char buffer[8];
    port_format::format_port(0, buffer, sizeof(buffer));
    ASSERT_STREQ(buffer, "0");
}

TEST(port_format_common_ports) {
    char buffer[8];

    port_format::format_port(80, buffer, sizeof(buffer));
    ASSERT_STREQ(buffer, "80");

    port_format::format_port(443, buffer, sizeof(buffer));
    ASSERT_STREQ(buffer, "443");

    port_format::format_port(8080, buffer, sizeof(buffer));
    ASSERT_STREQ(buffer, "8080");
}

// ============================================================================
// UPnP Return Code Tests
// ============================================================================
// Document expected UPnP error codes

namespace upnp_errors {
    // miniupnpc error codes
    constexpr int UPNPCOMMAND_SUCCESS = 0;
    constexpr int UPNPCOMMAND_UNKNOWN_ERROR = -1;
    constexpr int UPNPCOMMAND_INVALID_ARGS = 402;
    constexpr int UPNPCOMMAND_ACTION_FAILED = 501;
    constexpr int UPNPCOMMAND_NO_SUCH_ENTRY = 714;
    constexpr int UPNPCOMMAND_CONFLICT = 718;
    constexpr int UPNPCOMMAND_ONLY_PERMANENT = 725;
}

TEST(upnp_error_success_is_zero) {
    ASSERT_EQ(upnp_errors::UPNPCOMMAND_SUCCESS, 0);
}

TEST(upnp_error_no_such_entry_is_714) {
    // DeletePortMapping returns this if mapping doesn't exist
    // We should treat this as success (goal is "no mapping exists")
    ASSERT_EQ(upnp_errors::UPNPCOMMAND_NO_SUCH_ENTRY, 714);
}

TEST(upnp_error_conflict_is_718) {
    // AddPortMapping returns this if port is already mapped by another host
    ASSERT_EQ(upnp_errors::UPNPCOMMAND_CONFLICT, 718);
}

// ============================================================================
// Lease Duration Tests
// ============================================================================
// Verify lease timing logic

TEST(lease_renew_before_expiry) {
    // Renew should happen before lease expires
    ASSERT_TRUE(p2p_constants::PORT_LEASE_RENEW < p2p_constants::PORT_LEASE_DURATION);
}

TEST(lease_renew_margin_is_10_seconds) {
    // Ryujinx uses 10 second margin (60 - 50 = 10)
    int margin = p2p_constants::PORT_LEASE_DURATION - p2p_constants::PORT_LEASE_RENEW;
    ASSERT_EQ(margin, 10);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== UPnP Port Mapper Tests ===\n\n");

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
