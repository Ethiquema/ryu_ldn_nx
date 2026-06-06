/**
 * @file config_ipc_service_tests.cpp
 * @brief Unit tests for standalone ryu:cfg IPC configuration service
 *
 * Tests for the ryu:cfg service that allows Tesla overlay to communicate
 * with the sysmodule independently of ldn:u MITM service.
 *
 * ## Test Categories
 *
 * 1. **Command ID Tests**: Verify command enumeration values match header
 * 2. **Structure Tests**: Verify IPC structure sizes and layouts
 * 3. **ConfigService Logic Tests**: Test service method behavior via mock config
 * 4. **ConfigResult Tests**: Verify result code values
 *
 * ## ryu:cfg Command IDs (0-25)
 *
 * | ID | Command            | Description                       |
 * |----|--------------------|-----------------------------------|
 * | 0  | GetVersion         | Get sysmodule version string      |
 * | 1  | GetConnectionStatus| Get current connection state      |
 * | 2  | GetPassphrase      | Get room passphrase               |
 * | 3  | SetPassphrase      | Set room passphrase               |
 * | 4  | GetServerAddress   | Get server host and port          |
 * | 5  | SetServerAddress   | Set server host and port          |
 * | 6  | GetLdnEnabled      | Check if LDN emulation is on      |
 * | 7  | SetLdnEnabled      | Toggle LDN emulation              |
 * | 8  | GetDebugEnabled    | Check debug logging state        |
 * | 9  | SetDebugEnabled    | Toggle debug logging             |
 * | 10 | GetDebugLevel       | Get log verbosity (0-3)           |
 * | 11 | SetDebugLevel       | Set log verbosity                 |
 * | 12 | SaveConfig          | Persist config to SD card         |
 * | 13 | ReloadConfig        | Reload config from SD card        |
 * | 14 | IsServiceActive     | Ping to check service is running  |
 * | 15 | (reserved)          | (was GetUsePassphrase before parity fix) |
 * | 16 | GetUsePassphrase    | Check passphrase filtering        |
 * | 17 | SetUsePassphrase    | Toggle passphrase filtering        |
 *
 * | 18 | IsGameActive        | Check if game is using LDN        |
 * | 19 | GetLdnState         | Get LDN communication state      |
 * | 20 | GetSessionInfo      | Get session info struct           |
 * | 21 | GetLastRtt          | Get last RTT in milliseconds      |
 * | 22 | ForceReconnect      | Request reconnection             |
 * | 23 | GetActiveProcessId  | Get active game PID               |
 * | 24 | GetDisableP2p       | Check if P2P proxy is disabled   |
 * | 25 | SetDisableP2p       | Toggle P2P proxy                  |
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <array>

//=============================================================================
// Mock Switch/libnx types for testing
//=============================================================================

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int64_t s64;
typedef int32_t Result;

#define R_SUCCEEDED(res) ((res) == 0)
#define R_FAILED(res)    ((res) != 0)
#define R_SUCCEED() return 0

//=============================================================================
// IPC Command Enum (from config_ipc_service.hpp)
//=============================================================================

/**
 * @brief IPC command IDs for ryu:cfg service
 *
 * These values must match the enum in config_ipc_service.hpp exactly.
 */
enum class ConfigCmd : u32 {
    // Configuration commands (0-17)
    GetVersion          = 0,
    GetConnectionStatus = 1,
    GetPassphrase       = 2,
    SetPassphrase       = 3,
    GetServerAddress    = 4,
    SetServerAddress    = 5,
    GetLdnEnabled      = 6,
    SetLdnEnabled      = 7,
    GetDebugEnabled    = 8,
    SetDebugEnabled    = 9,
    GetDebugLevel       = 10,
    SetDebugLevel       = 11,
    SaveConfig          = 12,
    ReloadConfig        = 13,
    IsServiceActive     = 14,
    // 15 reserved (was GetUsePassphrase before parity fix)
    GetUsePassphrase    = 16,
    SetUsePassphrase    = 17,

    // Runtime LDN state commands (18-23)
    IsGameActive        = 18,
    GetLdnState         = 19,
    GetSessionInfo      = 20,
    GetLastRtt          = 21,
    ForceReconnect      = 22,
    GetActiveProcessId  = 23,

    // P2P Proxy control (24-25)
    GetDisableP2p       = 24,
    SetDisableP2p       = 25,
};

/**
 * @brief Configuration result codes
 *
 * Match the enum in config_ipc_service.hpp.
 */
enum class ConfigResult : u32 {
    Success = 0,
    FileNotFound = 1,
    ParseError = 2,
    IoError = 3,
    InvalidValue = 4,
};

//=============================================================================
// IPC Data Structures (from config_ipc_service.hpp)
//=============================================================================

/**
 * @brief Server address structure for IPC
 *
 * Used with GetServerAddress (cmd 4) and SetServerAddress (cmd 5).
 */
struct ServerAddressIpc {
    char host[64];  ///< Server hostname or IP (null-terminated)
    u16 port;       ///< Server port number
    u16 padding;    ///< Padding for alignment
};
static_assert(sizeof(ServerAddressIpc) == 68, "ServerAddressIpc must be 68 bytes");

//=============================================================================
// Mock Configuration State (simulates global config)
//=============================================================================

namespace mock {

/**
 * @brief Mock server configuration
 */
struct ServerConfig {
    char host[128];
    u16 port;
};

/**
 * @brief Mock LDN configuration
 */
struct LdnConfig {
    bool enabled;
    char passphrase[65];
    bool use_passphrase;
    bool disable_p2p;
};

/**
 * @brief Mock debug configuration
 */
struct DebugConfig {
    bool enabled;
    u32 level;
};

/**
 * @brief Mock complete configuration
 */
struct Config {
    ServerConfig server;
    LdnConfig ldn;
    DebugConfig debug;
};

/// Global mock config state
Config g_mock_config;

/**
 * @brief Initialize mock config with default values
 */
void init_mock_config() {
    std::memset(&g_mock_config, 0, sizeof(g_mock_config));

    // Server defaults
    std::strncpy(g_mock_config.server.host, "ryuldnnx.ddns.net",
                 sizeof(g_mock_config.server.host) - 1);
    g_mock_config.server.port = 30456;


    // LDN defaults
    g_mock_config.ldn.enabled = true;
    g_mock_config.ldn.passphrase[0] = '\0';

    // Debug defaults
    g_mock_config.debug.enabled = false;
    g_mock_config.debug.level = 1;

    // Passphrase filtering default
    g_mock_config.ldn.use_passphrase = false;

    // P2P proxy default (disabled by default, meaning P2P is off)
    g_mock_config.ldn.disable_p2p = true;
}

} // namespace mock

//=============================================================================
// Mock ConfigService Implementation
//=============================================================================

namespace mock {

/**
 * @brief Safe string copy utility (matches sysmodule implementation)
 */
void safe_strcpy(char* dest, const char* src, size_t max_len) {
    size_t i = 0;
    while (i < max_len && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/**
 * @brief Mock ConfigService for testing
 *
 * This simulates the ConfigService methods without Atmosphere dependencies.
 * Each method follows the same logic as the real implementation.
 */
class MockConfigService {
public:
    // Version
    Result GetVersion(std::array<char, 32>& out) {
        static constexpr const char* VERSION = "1.0.0";
        std::memset(out.data(), 0, out.size());
        safe_strcpy(out.data(), VERSION, out.size() - 1);
        R_SUCCEED();
    }

    // Connection status
    Result GetConnectionStatus(u32& out) {
        out = 0; // Always ready
        R_SUCCEED();
    }

    // Passphrase
    Result GetPassphrase(std::array<char, 64>& out) {
        std::memset(out.data(), 0, out.size());
        safe_strcpy(out.data(), g_mock_config.ldn.passphrase, out.size() - 1);
        R_SUCCEED();
    }

    Result SetPassphrase(const std::array<char, 64>& passphrase) {
        safe_strcpy(g_mock_config.ldn.passphrase, passphrase.data(), 64);
        R_SUCCEED();
    }

    // Server address
    Result GetServerAddress(ServerAddressIpc& out) {
        std::memset(&out, 0, sizeof(out));
        safe_strcpy(out.host, g_mock_config.server.host, sizeof(out.host) - 1);
        out.port = g_mock_config.server.port;
        R_SUCCEED();
    }

    Result SetServerAddress(const ServerAddressIpc& address) {
        safe_strcpy(g_mock_config.server.host, address.host, 127);
        g_mock_config.server.port = address.port;
        R_SUCCEED();
    }

    // LDN enabled
    Result GetLdnEnabled(u32& out) {
        out = g_mock_config.ldn.enabled ? 1 : 0;
        R_SUCCEED();
    }

    Result SetLdnEnabled(u32 enabled) {
        g_mock_config.ldn.enabled = (enabled != 0);
        R_SUCCEED();
    }


    // Debug enabled
    Result GetDebugEnabled(u32& out) {
        out = g_mock_config.debug.enabled ? 1 : 0;
        R_SUCCEED();
    }

    Result SetDebugEnabled(u32 enabled) {
        g_mock_config.debug.enabled = (enabled != 0);
        R_SUCCEED();
    }

    // Debug level
    Result GetDebugLevel(u32& out) {
        out = g_mock_config.debug.level;
        R_SUCCEED();
    }

    Result SetDebugLevel(u32 level) {
        g_mock_config.debug.level = level;
        R_SUCCEED();
    }

    // Service check
    Result IsServiceActive(u32& out) {
        out = 1;
        R_SUCCEED();
    }

    // Use passphrase filtering
    Result GetUsePassphrase(u32& out) {
        out = g_mock_config.ldn.use_passphrase ? 1 : 0;
        R_SUCCEED();
    }

    Result SetUsePassphrase(u32 enabled) {
        g_mock_config.ldn.use_passphrase = (enabled != 0);
        R_SUCCEED();
    }

    // P2P proxy disabled state
    Result GetDisableP2p(u32& out) {
        out = g_mock_config.ldn.disable_p2p ? 1 : 0;
        R_SUCCEED();
    }

    Result SetDisableP2p(u32 disabled) {
        g_mock_config.ldn.disable_p2p = (disabled != 0);
        R_SUCCEED();
    }
};

} // namespace mock

//=============================================================================
// Test Framework
//=============================================================================

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    g_tests_run++; \
    printf("  [TEST] %s...", #name); \
    mock::init_mock_config(); \
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
#define ASSERT_SUCCESS(r) ASSERT(R_SUCCEEDED(r))

//=============================================================================
// Command ID Tests
//=============================================================================

/**
 * @test Verify command IDs start from 0
 */
TEST(command_ids_start_from_zero) {
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetVersion), 0u);
}

/**
 * @test Verify command IDs are sequential
 */
TEST(command_ids_are_sequential) {
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetVersion), 0u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetConnectionStatus), 1u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetPassphrase), 2u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SetPassphrase), 3u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetServerAddress), 4u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SetServerAddress), 5u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetLdnEnabled), 6u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SetLdnEnabled), 7u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetDebugEnabled), 8u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SetDebugEnabled), 9u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetDebugLevel), 10u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SetDebugLevel), 11u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SaveConfig), 12u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::ReloadConfig), 13u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::IsServiceActive), 14u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetUsePassphrase), 16u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SetUsePassphrase), 17u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::IsGameActive), 18u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetLdnState), 19u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetSessionInfo), 20u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetLastRtt), 21u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::ForceReconnect), 22u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetActiveProcessId), 23u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetDisableP2p), 24u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SetDisableP2p), 25u);
}

/**
 * @test Verify Get/Set commands are paired (Get is even, Set is odd)
 */
TEST(command_ids_get_set_pairing) {
    // Config Get/Set pairs (Get is even, Set is odd)
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetPassphrase) % 2, 0u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetServerAddress) % 2, 0u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetLdnEnabled) % 2, 0u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetDebugEnabled) % 2, 0u);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetDebugLevel) % 2, 0u);
    // Get even, Set odd (convention: Get pair, Set pair)
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetUsePassphrase) % 2, 0u);  // 16, even (Get)
    ASSERT_EQ(static_cast<u32>(ConfigCmd::GetDisableP2p) % 2, 0u);     // 24, even (Get)

    // Config Set commands immediately follow Get
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SetPassphrase),
              static_cast<u32>(ConfigCmd::GetPassphrase) + 1);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SetServerAddress),
              static_cast<u32>(ConfigCmd::GetServerAddress) + 1);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SetLdnEnabled),
              static_cast<u32>(ConfigCmd::GetLdnEnabled) + 1);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SetDebugEnabled),
              static_cast<u32>(ConfigCmd::GetDebugEnabled) + 1);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SetDebugLevel),
              static_cast<u32>(ConfigCmd::GetDebugLevel) + 1);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SetUsePassphrase),
              static_cast<u32>(ConfigCmd::GetUsePassphrase) + 1);
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SetDisableP2p),
              static_cast<u32>(ConfigCmd::GetDisableP2p) + 1);
}

/**
 * @test Verify total command count
 */
TEST(command_count_is_26) {
    // Commands 0-25 with gap at 15 (reserved from parity swap)
    // 26 defined values but command 15 is unused/reserved
    ASSERT_EQ(static_cast<u32>(ConfigCmd::SetDisableP2p), 25u);
    // Total: 26 defined command IDs (0-25, minus reserved 15)
}

//=============================================================================
// Structure Size Tests
//=============================================================================

/**
 * @test ServerAddressIpc structure is exactly 68 bytes
 *
 * Size breakdown:
 * - host[64]: 64 bytes
 * - port: 2 bytes
 * - padding: 2 bytes
 * Total: 68 bytes
 */
TEST(server_address_ipc_size) {
    ASSERT_EQ(sizeof(ServerAddressIpc), 68u);
}

/**
 * @test ServerAddressIpc field offsets are correct
 */
TEST(server_address_ipc_layout) {
    ServerAddressIpc addr = {};

    // Verify host is at offset 0
    ASSERT_EQ(reinterpret_cast<char*>(&addr.host) -
              reinterpret_cast<char*>(&addr), 0);

    // Verify port is at offset 64
    ASSERT_EQ(reinterpret_cast<char*>(&addr.port) -
              reinterpret_cast<char*>(&addr), 64);

    // Verify padding is at offset 66
    ASSERT_EQ(reinterpret_cast<char*>(&addr.padding) -
              reinterpret_cast<char*>(&addr), 66);
}

/**
 * @test ServerAddressIpc is POD (trivially copyable)
 */
TEST(server_address_ipc_is_pod) {
    ServerAddressIpc src = {};
    std::strncpy(src.host, "test.server.com", sizeof(src.host));
    src.port = 12345;
    src.padding = 0;

    ServerAddressIpc dst;
    std::memcpy(&dst, &src, sizeof(ServerAddressIpc));

    ASSERT_STREQ(dst.host, "test.server.com");
    ASSERT_EQ(dst.port, 12345u);
}

//=============================================================================
// ConfigResult Tests
//=============================================================================

/**
 * @test ConfigResult values match expected codes
 */
TEST(config_result_values) {
    ASSERT_EQ(static_cast<u32>(ConfigResult::Success), 0u);
    ASSERT_EQ(static_cast<u32>(ConfigResult::FileNotFound), 1u);
    ASSERT_EQ(static_cast<u32>(ConfigResult::ParseError), 2u);
    ASSERT_EQ(static_cast<u32>(ConfigResult::IoError), 3u);
    ASSERT_EQ(static_cast<u32>(ConfigResult::InvalidValue), 4u);
}

/**
 * @test ConfigResult::Success is falsy when used as bool
 */
TEST(config_result_success_is_zero) {
    ConfigResult result = ConfigResult::Success;
    ASSERT_EQ(static_cast<u32>(result), 0u);
}

//=============================================================================
// ConfigService - Version & Status Tests
//=============================================================================

/**
 * @test GetVersion returns valid version string
 */
TEST(get_version_returns_string) {
    mock::MockConfigService svc;
    std::array<char, 32> version = {};

    Result r = svc.GetVersion(version);

    ASSERT_SUCCESS(r);
    ASSERT_STREQ(version.data(), "1.0.0");
}

/**
 * @test GetConnectionStatus returns 0 (ready)
 */
TEST(get_connection_status_returns_ready) {
    mock::MockConfigService svc;
    u32 status = 99;

    Result r = svc.GetConnectionStatus(status);

    ASSERT_SUCCESS(r);
    ASSERT_EQ(status, 0u);
}

/**
 * @test IsServiceActive returns 1
 */
TEST(is_service_active_returns_true) {
    mock::MockConfigService svc;
    u32 active = 0;

    Result r = svc.IsServiceActive(active);

    ASSERT_SUCCESS(r);
    ASSERT_EQ(active, 1u);
}

//=============================================================================
// ConfigService - Passphrase Tests
//=============================================================================

/**
 * @test GetPassphrase returns empty string by default
 */
TEST(get_passphrase_default_empty) {
    mock::MockConfigService svc;
    std::array<char, 64> passphrase = {};
    passphrase[0] = 'X'; // Pre-fill to verify it gets cleared

    Result r = svc.GetPassphrase(passphrase);

    ASSERT_SUCCESS(r);
    ASSERT_STREQ(passphrase.data(), "");
}

/**
 * @test SetPassphrase/GetPassphrase roundtrip
 */
TEST(passphrase_set_get_roundtrip) {
    mock::MockConfigService svc;

    // Set passphrase
    std::array<char, 64> input = {};
    std::strncpy(input.data(), "mysecret", input.size());
    Result r = svc.SetPassphrase(input);
    ASSERT_SUCCESS(r);

    // Get passphrase
    std::array<char, 64> output = {};
    r = svc.GetPassphrase(output);
    ASSERT_SUCCESS(r);

    ASSERT_STREQ(output.data(), "mysecret");
}

/**
 * @test SetPassphrase with empty string clears passphrase
 */
TEST(passphrase_set_empty_clears) {
    mock::MockConfigService svc;

    // Set non-empty first
    std::array<char, 64> input = {};
    std::strncpy(input.data(), "secret", input.size());
    svc.SetPassphrase(input);

    // Clear with empty string
    std::array<char, 64> empty = {};
    svc.SetPassphrase(empty);

    // Verify cleared
    std::array<char, 64> output = {};
    svc.GetPassphrase(output);
    ASSERT_STREQ(output.data(), "");
}

//=============================================================================
// ConfigService - Server Address Tests
//=============================================================================

/**
 * @test GetServerAddress returns defaults
 */
TEST(get_server_address_default) {
    mock::MockConfigService svc;
    ServerAddressIpc addr = {};

    Result r = svc.GetServerAddress(addr);

    ASSERT_SUCCESS(r);
    ASSERT_STREQ(addr.host, "ryuldnnx.ddns.net");
    ASSERT_EQ(addr.port, 30456u);
}

/**
 * @test SetServerAddress/GetServerAddress roundtrip
 */
TEST(server_address_set_get_roundtrip) {
    mock::MockConfigService svc;

    // Set address
    ServerAddressIpc input = {};
    std::strncpy(input.host, "192.168.1.100", sizeof(input.host));
    input.port = 9999;
    Result r = svc.SetServerAddress(input);
    ASSERT_SUCCESS(r);

    // Get address
    ServerAddressIpc output = {};
    r = svc.GetServerAddress(output);
    ASSERT_SUCCESS(r);

    ASSERT_STREQ(output.host, "192.168.1.100");
    ASSERT_EQ(output.port, 9999u);
}

/**
 * @test SetServerAddress with max-length hostname
 */
TEST(server_address_max_length_host) {
    mock::MockConfigService svc;

    ServerAddressIpc input = {};
    // Fill with 63 chars (max for null-terminated 64-byte buffer)
    std::memset(input.host, 'a', 63);
    input.host[63] = '\0';
    input.port = 1234;

    svc.SetServerAddress(input);

    ServerAddressIpc output = {};
    svc.GetServerAddress(output);

    ASSERT_EQ(std::strlen(output.host), 63u);
}

//=============================================================================
// ConfigService - Boolean Settings Tests
//=============================================================================

/**
 * @test LdnEnabled default is true
 */
TEST(ldn_enabled_default_true) {
    mock::MockConfigService svc;
    u32 enabled = 0;

    Result r = svc.GetLdnEnabled(enabled);

    ASSERT_SUCCESS(r);
    ASSERT_EQ(enabled, 1u);
}

/**
 * @test SetLdnEnabled/GetLdnEnabled roundtrip
 */
TEST(ldn_enabled_set_get_roundtrip) {
    mock::MockConfigService svc;

    // Disable
    svc.SetLdnEnabled(0);
    u32 enabled = 1;
    svc.GetLdnEnabled(enabled);
    ASSERT_EQ(enabled, 0u);

    // Re-enable
    svc.SetLdnEnabled(1);
    svc.GetLdnEnabled(enabled);
    ASSERT_EQ(enabled, 1u);
}


/**
 * @test DebugEnabled default is false
 */
TEST(debug_enabled_default_false) {
    mock::MockConfigService svc;
    u32 enabled = 1;

    svc.GetDebugEnabled(enabled);

    ASSERT_EQ(enabled, 0u);
}


/**
 * @test Non-zero values for Set are treated as true
 */
TEST(boolean_nonzero_is_true) {
    mock::MockConfigService svc;

    // Any non-zero should enable
    svc.SetDebugEnabled(42);
    u32 enabled = 0;
    svc.GetDebugEnabled(enabled);
    ASSERT_EQ(enabled, 1u);

    svc.SetDebugEnabled(255);
    svc.GetDebugEnabled(enabled);
    ASSERT_EQ(enabled, 1u);
}

//=============================================================================
// ConfigService - Debug Level Tests
//=============================================================================

/**
 * @test DebugLevel default is 1 (Warning)
 */
TEST(debug_level_default_warning) {
    mock::MockConfigService svc;
    u32 level = 99;

    svc.GetDebugLevel(level);

    ASSERT_EQ(level, 1u);
}

/**
 * @test SetDebugLevel/GetDebugLevel roundtrip
 */
TEST(debug_level_set_get_roundtrip) {
    mock::MockConfigService svc;

    for (u32 i = 0; i <= 3; i++) {
        svc.SetDebugLevel(i);
        u32 level = 99;
        svc.GetDebugLevel(level);
        ASSERT_EQ(level, i);
    }
}
//=============================================================================
// Use Passphrase Tests
//=============================================================================

TEST(use_passphrase_default_false) {
    mock::MockConfigService svc;
    u32 enabled = 99;
    Result r = svc.GetUsePassphrase(enabled);
    ASSERT_SUCCESS(r);
    ASSERT_EQ(enabled, 0u);
}

TEST(use_passphrase_set_get_roundtrip) {
    mock::MockConfigService svc;
    svc.SetUsePassphrase(1);
    u32 enabled = 0;
    svc.GetUsePassphrase(enabled);
    ASSERT_EQ(enabled, 1u);

    svc.SetUsePassphrase(0);
    svc.GetUsePassphrase(enabled);
    ASSERT_EQ(enabled, 0u);
}

//=============================================================================
// Disable P2P Tests
//=============================================================================

TEST(disable_p2p_default_true) {
    mock::MockConfigService svc;
    u32 disabled = 0;
    Result r = svc.GetDisableP2p(disabled);
    ASSERT_SUCCESS(r);
    ASSERT_EQ(disabled, 1u);
}

TEST(disable_p2p_set_get_roundtrip) {
    mock::MockConfigService svc;

    // Enable P2P (disable_p2p = 0)
    svc.SetDisableP2p(0);
    u32 disabled = 1;
    svc.GetDisableP2p(disabled);
    ASSERT_EQ(disabled, 0u);

    // Disable P2P (disable_p2p = 1)
    svc.SetDisableP2p(1);
    svc.GetDisableP2p(disabled);
    ASSERT_EQ(disabled, 1u);
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("\n========================================\n");
    printf("  ryu:cfg IPC Service Tests\n");
    printf("  ryu_ldn_nx\n");
    printf("========================================\n\n");

    printf("--- Command ID Tests ---\n");
    RUN_TEST(command_ids_start_from_zero);
    RUN_TEST(command_ids_are_sequential);
    RUN_TEST(command_ids_get_set_pairing);
    RUN_TEST(command_count_is_26);

    printf("\n--- Structure Size Tests ---\n");
    RUN_TEST(server_address_ipc_size);
    RUN_TEST(server_address_ipc_layout);
    RUN_TEST(server_address_ipc_is_pod);

    printf("\n--- ConfigResult Tests ---\n");
    RUN_TEST(config_result_values);
    RUN_TEST(config_result_success_is_zero);

    printf("\n--- Version & Status Tests ---\n");
    RUN_TEST(get_version_returns_string);
    RUN_TEST(get_connection_status_returns_ready);
    RUN_TEST(is_service_active_returns_true);

    printf("\n--- Passphrase Tests ---\n");
    RUN_TEST(get_passphrase_default_empty);
    RUN_TEST(passphrase_set_get_roundtrip);
    RUN_TEST(passphrase_set_empty_clears);

    printf("\n--- Server Address Tests ---\n");
    RUN_TEST(get_server_address_default);
    RUN_TEST(server_address_set_get_roundtrip);
    RUN_TEST(server_address_max_length_host);

    printf("\n--- Boolean Settings Tests ---\n");
    RUN_TEST(ldn_enabled_default_true);
    RUN_TEST(ldn_enabled_set_get_roundtrip);
    RUN_TEST(boolean_nonzero_is_true);

    printf("\n--- Debug Level Tests ---\n");
    RUN_TEST(debug_level_default_warning);
    RUN_TEST(debug_level_set_get_roundtrip);

    printf("\n--- Use Passphrase Tests ---\n");
    RUN_TEST(use_passphrase_default_false);
    RUN_TEST(use_passphrase_set_get_roundtrip);

    printf("\n--- Disable P2P Tests ---\n");
    RUN_TEST(disable_p2p_default_true);
    RUN_TEST(disable_p2p_set_get_roundtrip);

    printf("\n--- Timeout Tests ---\n");

    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n", g_tests_passed, g_tests_run);
    printf("========================================\n\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
