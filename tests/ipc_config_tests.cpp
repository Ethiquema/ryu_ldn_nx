/**
 * @file ipc_config_tests.cpp
 * @brief Unit tests for extended IPC configuration commands
 *
 * Tests the IPC structures and helper functions for the extended
 * configuration service that allows Tesla overlay to modify all settings.
 *
 * Extended IPC Commands (65011-65030):
 * - 65011: GetPassphrase / 65012: SetPassphrase
 * - 65013: GetLdnEnabled / 65014: SetLdnEnabled
 * - 65015: GetUseTls / 65016: SetUseTls
 * - 65017: GetConnectTimeout / 65018: SetConnectTimeout
 * - 65019: GetPingInterval / 65020: SetPingInterval
 * - 65021: GetReconnectDelay / 65022: SetReconnectDelay
 * - 65023: GetMaxReconnectAttempts / 65024: SetMaxReconnectAttempts
 * - 65025: GetDebugLevel / 65026: SetDebugLevel
 * - 65027: GetLogToFile / 65028: SetLogToFile
 * - 65029: SaveConfig
 * - 65030: ReloadConfig
 *
 * Also tests overlay helper functions for formatting settings values.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

//=============================================================================
// Mock Switch/libnx types for testing
//=============================================================================

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int32_t Result;

#define R_SUCCEEDED(res) ((res) == 0)
#define R_FAILED(res)    ((res) != 0)

//=============================================================================
// IPC Command IDs (from ldn_config_service.hpp)
//=============================================================================

// Existing commands (65001-65010)
constexpr u32 IPC_CMD_GET_VERSION = 65001;
constexpr u32 IPC_CMD_GET_CONNECTION_STATUS = 65002;
constexpr u32 IPC_CMD_GET_LDN_STATE = 65003;
constexpr u32 IPC_CMD_GET_SESSION_INFO = 65004;
constexpr u32 IPC_CMD_GET_SERVER_ADDRESS = 65005;
constexpr u32 IPC_CMD_SET_SERVER_ADDRESS = 65006;
constexpr u32 IPC_CMD_GET_DEBUG_ENABLED = 65007;
constexpr u32 IPC_CMD_SET_DEBUG_ENABLED = 65008;
constexpr u32 IPC_CMD_FORCE_RECONNECT = 65009;
constexpr u32 IPC_CMD_GET_LAST_RTT = 65010;

// Extended commands (65011-65030)
constexpr u32 IPC_CMD_GET_PASSPHRASE = 65011;
constexpr u32 IPC_CMD_SET_PASSPHRASE = 65012;
constexpr u32 IPC_CMD_GET_LDN_ENABLED = 65013;
constexpr u32 IPC_CMD_SET_LDN_ENABLED = 65014;
constexpr u32 IPC_CMD_GET_USE_TLS = 65015;
constexpr u32 IPC_CMD_SET_USE_TLS = 65016;
constexpr u32 IPC_CMD_GET_CONNECT_TIMEOUT = 65017;
constexpr u32 IPC_CMD_SET_CONNECT_TIMEOUT = 65018;
constexpr u32 IPC_CMD_GET_PING_INTERVAL = 65019;
constexpr u32 IPC_CMD_SET_PING_INTERVAL = 65020;
constexpr u32 IPC_CMD_GET_RECONNECT_DELAY = 65021;
constexpr u32 IPC_CMD_SET_RECONNECT_DELAY = 65022;
constexpr u32 IPC_CMD_GET_MAX_RECONNECT_ATTEMPTS = 65023;
constexpr u32 IPC_CMD_SET_MAX_RECONNECT_ATTEMPTS = 65024;
constexpr u32 IPC_CMD_GET_DEBUG_LEVEL = 65025;
constexpr u32 IPC_CMD_SET_DEBUG_LEVEL = 65026;
constexpr u32 IPC_CMD_GET_LOG_TO_FILE = 65027;
constexpr u32 IPC_CMD_SET_LOG_TO_FILE = 65028;
constexpr u32 IPC_CMD_SAVE_CONFIG = 65029;
constexpr u32 IPC_CMD_RELOAD_CONFIG = 65030;

//=============================================================================
// IPC Data Structures
//=============================================================================

// Passphrase structure (max 64 chars)
struct IpcPassphrase {
    char passphrase[64];
};
static_assert(sizeof(IpcPassphrase) == 64, "IpcPassphrase must be 64 bytes");

// Server address structure (existing)
struct IpcServerAddress {
    char host[64];
    u16 port;
    u16 padding;
};
static_assert(sizeof(IpcServerAddress) == 68, "IpcServerAddress must be 68 bytes");

// Network settings structure (for batch get)
struct IpcNetworkSettings {
    u32 connect_timeout_ms;
    u32 ping_interval_ms;
    u32 reconnect_delay_ms;
    u32 max_reconnect_attempts;
};
static_assert(sizeof(IpcNetworkSettings) == 16, "IpcNetworkSettings must be 16 bytes");

// Debug settings structure (for batch get)
struct IpcDebugSettings {
    u32 enabled;      // bool as u32
    u32 level;        // 0-3
    u32 log_to_file;  // bool as u32
    u32 reserved;
};
static_assert(sizeof(IpcDebugSettings) == 16, "IpcDebugSettings must be 16 bytes");

// Config result codes
enum class IpcConfigResult : u32 {
    Success = 0,
    FileNotFound = 1,
    ParseError = 2,
    IoError = 3,
    InvalidValue = 4,
};

//=============================================================================
// Helper Functions for Overlay
//=============================================================================

// Format passphrase for display (masked)
void FormatPassphraseMasked(const char* passphrase, char* buf, size_t bufSize) {
    if (passphrase == nullptr || passphrase[0] == '\0') {
        snprintf(buf, bufSize, "(not set)");
    } else {
        size_t len = strlen(passphrase);
        if (len <= 4) {
            snprintf(buf, bufSize, "****");
        } else {
            snprintf(buf, bufSize, "%c%c****%c%c",
                     passphrase[0], passphrase[1],
                     passphrase[len-2], passphrase[len-1]);
        }
    }
}

// Format boolean setting
const char* FormatBoolSetting(u32 value) {
    return value ? "Enabled" : "Disabled";
}

// Format timeout value
void FormatTimeout(u32 timeout_ms, char* buf, size_t bufSize) {
    if (timeout_ms < 1000) {
        snprintf(buf, bufSize, "%u ms", timeout_ms);
    } else {
        snprintf(buf, bufSize, "%.1f s", timeout_ms / 1000.0);
    }
}

// Format debug level
const char* FormatDebugLevel(u32 level) {
    switch (level) {
        case 0: return "Error";
        case 1: return "Warning";
        case 2: return "Info";
        case 3: return "Verbose";
        default: return "Unknown";
    }
}

// Validate port number
bool IsValidPort(u16 port) {
    return port > 0 && port <= 65535;
}

// Validate timeout (reasonable range)
bool IsValidTimeout(u32 timeout_ms) {
    return timeout_ms >= 100 && timeout_ms <= 300000; // 100ms to 5 minutes
}

// Validate debug level
bool IsValidDebugLevel(u32 level) {
    return level <= 3;
}

//=============================================================================
// Test Framework
//=============================================================================

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    g_tests_run++; \
    printf("  [TEST] %s...", #name); \
    name(); \
    g_tests_passed++; \
    printf(" PASS\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAIL\n    Assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_STREQ(a, b) ASSERT(strcmp((a), (b)) == 0)
#define ASSERT_TRUE(a) ASSERT((a) == true)
#define ASSERT_FALSE(a) ASSERT((a) == false)

//=============================================================================
// Command ID Tests
//=============================================================================

TEST(command_ids_are_sequential) {
    ASSERT_EQ(IPC_CMD_GET_PASSPHRASE, 65011u);
    ASSERT_EQ(IPC_CMD_SET_PASSPHRASE, 65012u);
    ASSERT_EQ(IPC_CMD_GET_LDN_ENABLED, 65013u);
    ASSERT_EQ(IPC_CMD_SET_LDN_ENABLED, 65014u);
    ASSERT_EQ(IPC_CMD_GET_USE_TLS, 65015u);
    ASSERT_EQ(IPC_CMD_SET_USE_TLS, 65016u);
    ASSERT_EQ(IPC_CMD_SAVE_CONFIG, 65029u);
    ASSERT_EQ(IPC_CMD_RELOAD_CONFIG, 65030u);
}

TEST(command_ids_no_overlap_with_existing) {
    // Ensure new commands don't overlap with existing ones
    ASSERT(IPC_CMD_GET_PASSPHRASE > IPC_CMD_GET_LAST_RTT);
}

TEST(command_ids_paired_get_set) {
    // Verify get/set commands are paired correctly
    ASSERT_EQ(IPC_CMD_SET_PASSPHRASE, IPC_CMD_GET_PASSPHRASE + 1);
    ASSERT_EQ(IPC_CMD_SET_LDN_ENABLED, IPC_CMD_GET_LDN_ENABLED + 1);
    ASSERT_EQ(IPC_CMD_SET_USE_TLS, IPC_CMD_GET_USE_TLS + 1);
    ASSERT_EQ(IPC_CMD_SET_CONNECT_TIMEOUT, IPC_CMD_GET_CONNECT_TIMEOUT + 1);
    ASSERT_EQ(IPC_CMD_SET_PING_INTERVAL, IPC_CMD_GET_PING_INTERVAL + 1);
    ASSERT_EQ(IPC_CMD_SET_RECONNECT_DELAY, IPC_CMD_GET_RECONNECT_DELAY + 1);
    ASSERT_EQ(IPC_CMD_SET_MAX_RECONNECT_ATTEMPTS, IPC_CMD_GET_MAX_RECONNECT_ATTEMPTS + 1);
    ASSERT_EQ(IPC_CMD_SET_DEBUG_LEVEL, IPC_CMD_GET_DEBUG_LEVEL + 1);
    ASSERT_EQ(IPC_CMD_SET_LOG_TO_FILE, IPC_CMD_GET_LOG_TO_FILE + 1);
}

//=============================================================================
// Structure Size Tests
//=============================================================================

TEST(ipc_passphrase_size) {
    ASSERT_EQ(sizeof(IpcPassphrase), 64u);
}

TEST(ipc_server_address_size) {
    ASSERT_EQ(sizeof(IpcServerAddress), 68u);
}

TEST(ipc_network_settings_size) {
    ASSERT_EQ(sizeof(IpcNetworkSettings), 16u);
}

TEST(ipc_debug_settings_size) {
    ASSERT_EQ(sizeof(IpcDebugSettings), 16u);
}

TEST(structures_are_pod) {
    // Verify structures are trivially copyable (POD-like)
    IpcPassphrase p1 = {};
    IpcPassphrase p2;
    memcpy(&p2, &p1, sizeof(IpcPassphrase));
    ASSERT_EQ(p2.passphrase[0], '\0');

    IpcNetworkSettings n1 = {1000, 2000, 3000, 5};
    IpcNetworkSettings n2;
    memcpy(&n2, &n1, sizeof(IpcNetworkSettings));
    ASSERT_EQ(n2.connect_timeout_ms, 1000u);
    ASSERT_EQ(n2.ping_interval_ms, 2000u);
}

//=============================================================================
// Passphrase Format Tests
//=============================================================================

TEST(passphrase_empty_shows_not_set) {
    char buf[32];
    FormatPassphraseMasked("", buf, sizeof(buf));
    ASSERT_STREQ(buf, "(not set)");
}

TEST(passphrase_null_shows_not_set) {
    char buf[32];
    FormatPassphraseMasked(nullptr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "(not set)");
}

TEST(passphrase_short_shows_masked) {
    char buf[32];
    FormatPassphraseMasked("abc", buf, sizeof(buf));
    ASSERT_STREQ(buf, "****");
}

TEST(passphrase_four_chars_shows_masked) {
    char buf[32];
    FormatPassphraseMasked("test", buf, sizeof(buf));
    ASSERT_STREQ(buf, "****");
}

TEST(passphrase_long_shows_partial) {
    char buf[32];
    FormatPassphraseMasked("mySecretPass", buf, sizeof(buf));
    ASSERT_STREQ(buf, "my****ss");
}

TEST(passphrase_five_chars_shows_partial) {
    char buf[32];
    FormatPassphraseMasked("hello", buf, sizeof(buf));
    ASSERT_STREQ(buf, "he****lo");
}

//=============================================================================
// Boolean Format Tests
//=============================================================================

TEST(bool_enabled_format) {
    ASSERT_STREQ(FormatBoolSetting(1), "Enabled");
    ASSERT_STREQ(FormatBoolSetting(42), "Enabled"); // Any non-zero
}

TEST(bool_disabled_format) {
    ASSERT_STREQ(FormatBoolSetting(0), "Disabled");
}

//=============================================================================
// Timeout Format Tests
//=============================================================================

TEST(timeout_milliseconds) {
    char buf[32];
    FormatTimeout(500, buf, sizeof(buf));
    ASSERT_STREQ(buf, "500 ms");
}

TEST(timeout_one_second) {
    char buf[32];
    FormatTimeout(1000, buf, sizeof(buf));
    ASSERT_STREQ(buf, "1.0 s");
}

TEST(timeout_seconds_decimal) {
    char buf[32];
    FormatTimeout(5500, buf, sizeof(buf));
    ASSERT_STREQ(buf, "5.5 s");
}

TEST(timeout_large_value) {
    char buf[32];
    FormatTimeout(30000, buf, sizeof(buf));
    ASSERT_STREQ(buf, "30.0 s");
}

TEST(timeout_999ms) {
    char buf[32];
    FormatTimeout(999, buf, sizeof(buf));
    ASSERT_STREQ(buf, "999 ms");
}

//=============================================================================
// Debug Level Format Tests
//=============================================================================

TEST(debug_level_error) {
    ASSERT_STREQ(FormatDebugLevel(0), "Error");
}

TEST(debug_level_warning) {
    ASSERT_STREQ(FormatDebugLevel(1), "Warning");
}

TEST(debug_level_info) {
    ASSERT_STREQ(FormatDebugLevel(2), "Info");
}

TEST(debug_level_verbose) {
    ASSERT_STREQ(FormatDebugLevel(3), "Verbose");
}

TEST(debug_level_unknown) {
    ASSERT_STREQ(FormatDebugLevel(4), "Unknown");
    ASSERT_STREQ(FormatDebugLevel(99), "Unknown");
}

//=============================================================================
// Validation Tests
//=============================================================================

TEST(port_valid_range) {
    ASSERT_TRUE(IsValidPort(1));
    ASSERT_TRUE(IsValidPort(80));
    ASSERT_TRUE(IsValidPort(39990));
    ASSERT_TRUE(IsValidPort(65535));
}

TEST(port_invalid_zero) {
    ASSERT_FALSE(IsValidPort(0));
}

TEST(timeout_valid_range) {
    ASSERT_TRUE(IsValidTimeout(100));    // Min
    ASSERT_TRUE(IsValidTimeout(5000));   // Typical
    ASSERT_TRUE(IsValidTimeout(30000));  // 30s
    ASSERT_TRUE(IsValidTimeout(300000)); // 5 min max
}

TEST(timeout_invalid_too_small) {
    ASSERT_FALSE(IsValidTimeout(0));
    ASSERT_FALSE(IsValidTimeout(50));
    ASSERT_FALSE(IsValidTimeout(99));
}

TEST(timeout_invalid_too_large) {
    ASSERT_FALSE(IsValidTimeout(300001));
    ASSERT_FALSE(IsValidTimeout(600000));
}

TEST(debug_level_valid) {
    ASSERT_TRUE(IsValidDebugLevel(0));
    ASSERT_TRUE(IsValidDebugLevel(1));
    ASSERT_TRUE(IsValidDebugLevel(2));
    ASSERT_TRUE(IsValidDebugLevel(3));
}

TEST(debug_level_invalid) {
    ASSERT_FALSE(IsValidDebugLevel(4));
    ASSERT_FALSE(IsValidDebugLevel(10));
    ASSERT_FALSE(IsValidDebugLevel(255));
}

//=============================================================================
// Config Result Tests
//=============================================================================

TEST(config_result_values) {
    ASSERT_EQ(static_cast<u32>(IpcConfigResult::Success), 0u);
    ASSERT_EQ(static_cast<u32>(IpcConfigResult::FileNotFound), 1u);
    ASSERT_EQ(static_cast<u32>(IpcConfigResult::ParseError), 2u);
    ASSERT_EQ(static_cast<u32>(IpcConfigResult::IoError), 3u);
    ASSERT_EQ(static_cast<u32>(IpcConfigResult::InvalidValue), 4u);
}

//=============================================================================
// Network Settings Tests
//=============================================================================

TEST(network_settings_zero_init) {
    IpcNetworkSettings settings = {};
    ASSERT_EQ(settings.connect_timeout_ms, 0u);
    ASSERT_EQ(settings.ping_interval_ms, 0u);
    ASSERT_EQ(settings.reconnect_delay_ms, 0u);
    ASSERT_EQ(settings.max_reconnect_attempts, 0u);
}

TEST(network_settings_assignment) {
    IpcNetworkSettings settings = {
        .connect_timeout_ms = 5000,
        .ping_interval_ms = 10000,
        .reconnect_delay_ms = 3000,
        .max_reconnect_attempts = 10
    };
    ASSERT_EQ(settings.connect_timeout_ms, 5000u);
    ASSERT_EQ(settings.ping_interval_ms, 10000u);
    ASSERT_EQ(settings.reconnect_delay_ms, 3000u);
    ASSERT_EQ(settings.max_reconnect_attempts, 10u);
}

//=============================================================================
// Debug Settings Tests
//=============================================================================

TEST(debug_settings_zero_init) {
    IpcDebugSettings settings = {};
    ASSERT_EQ(settings.enabled, 0u);
    ASSERT_EQ(settings.level, 0u);
    ASSERT_EQ(settings.log_to_file, 0u);
    ASSERT_EQ(settings.reserved, 0u);
}

TEST(debug_settings_assignment) {
    IpcDebugSettings settings = {
        .enabled = 1,
        .level = 2,
        .log_to_file = 1,
        .reserved = 0
    };
    ASSERT_EQ(settings.enabled, 1u);
    ASSERT_EQ(settings.level, 2u);
    ASSERT_EQ(settings.log_to_file, 1u);
}

//=============================================================================
// Server Address Tests
//=============================================================================

TEST(server_address_default_port) {
    IpcServerAddress addr = {};
    strncpy(addr.host, "localhost", sizeof(addr.host));
    addr.port = 39990;
    ASSERT_STREQ(addr.host, "localhost");
    ASSERT_EQ(addr.port, 39990u);
}

TEST(server_address_long_hostname) {
    IpcServerAddress addr = {};
    const char* longHost = "very-long-hostname.subdomain.example.com";
    strncpy(addr.host, longHost, sizeof(addr.host) - 1);
    addr.host[sizeof(addr.host) - 1] = '\0';
    ASSERT(strlen(addr.host) < sizeof(addr.host));
}

TEST(server_address_truncates_too_long) {
    IpcServerAddress addr = {};
    // Try to copy a 70+ char hostname into 64-byte buffer
    const char* tooLong = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    strncpy(addr.host, tooLong, sizeof(addr.host) - 1);
    addr.host[sizeof(addr.host) - 1] = '\0';
    ASSERT_EQ(strlen(addr.host), 63u);
}

//=============================================================================
// Passphrase Structure Tests
//=============================================================================

TEST(passphrase_empty) {
    IpcPassphrase p = {};
    ASSERT_EQ(p.passphrase[0], '\0');
}

TEST(passphrase_copy) {
    IpcPassphrase p = {};
    strncpy(p.passphrase, "mysecret", sizeof(p.passphrase));
    ASSERT_STREQ(p.passphrase, "mysecret");
}

TEST(passphrase_max_length) {
    IpcPassphrase p = {};
    // Fill with 63 chars + null terminator
    memset(p.passphrase, 'x', 63);
    p.passphrase[63] = '\0';
    ASSERT_EQ(strlen(p.passphrase), 63u);
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("\n========================================\n");
    printf("  IPC Config Tests - ryu_ldn_nx\n");
    printf("========================================\n\n");

    printf("--- Command ID Tests ---\n");
    RUN_TEST(command_ids_are_sequential);
    RUN_TEST(command_ids_no_overlap_with_existing);
    RUN_TEST(command_ids_paired_get_set);

    printf("\n--- Structure Size Tests ---\n");
    RUN_TEST(ipc_passphrase_size);
    RUN_TEST(ipc_server_address_size);
    RUN_TEST(ipc_network_settings_size);
    RUN_TEST(ipc_debug_settings_size);
    RUN_TEST(structures_are_pod);

    printf("\n--- Passphrase Format Tests ---\n");
    RUN_TEST(passphrase_empty_shows_not_set);
    RUN_TEST(passphrase_null_shows_not_set);
    RUN_TEST(passphrase_short_shows_masked);
    RUN_TEST(passphrase_four_chars_shows_masked);
    RUN_TEST(passphrase_long_shows_partial);
    RUN_TEST(passphrase_five_chars_shows_partial);

    printf("\n--- Boolean Format Tests ---\n");
    RUN_TEST(bool_enabled_format);
    RUN_TEST(bool_disabled_format);

    printf("\n--- Timeout Format Tests ---\n");
    RUN_TEST(timeout_milliseconds);
    RUN_TEST(timeout_one_second);
    RUN_TEST(timeout_seconds_decimal);
    RUN_TEST(timeout_large_value);
    RUN_TEST(timeout_999ms);

    printf("\n--- Debug Level Format Tests ---\n");
    RUN_TEST(debug_level_error);
    RUN_TEST(debug_level_warning);
    RUN_TEST(debug_level_info);
    RUN_TEST(debug_level_verbose);
    RUN_TEST(debug_level_unknown);

    printf("\n--- Validation Tests ---\n");
    RUN_TEST(port_valid_range);
    RUN_TEST(port_invalid_zero);
    RUN_TEST(timeout_valid_range);
    RUN_TEST(timeout_invalid_too_small);
    RUN_TEST(timeout_invalid_too_large);
    RUN_TEST(debug_level_valid);
    RUN_TEST(debug_level_invalid);

    printf("\n--- Config Result Tests ---\n");
    RUN_TEST(config_result_values);

    printf("\n--- Network Settings Tests ---\n");
    RUN_TEST(network_settings_zero_init);
    RUN_TEST(network_settings_assignment);

    printf("\n--- Debug Settings Tests ---\n");
    RUN_TEST(debug_settings_zero_init);
    RUN_TEST(debug_settings_assignment);

    printf("\n--- Server Address Tests ---\n");
    RUN_TEST(server_address_default_port);
    RUN_TEST(server_address_long_hostname);
    RUN_TEST(server_address_truncates_too_long);

    printf("\n--- Passphrase Structure Tests ---\n");
    RUN_TEST(passphrase_empty);
    RUN_TEST(passphrase_copy);
    RUN_TEST(passphrase_max_length);

    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n", g_tests_passed, g_tests_run);
    printf("========================================\n\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
