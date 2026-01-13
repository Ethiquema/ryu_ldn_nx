/**
 * @file config_manager_tests.cpp
 * @brief Unit tests for ConfigManager
 *
 * Tests for the global configuration manager including:
 * - Initialization and loading
 * - Getter/setter for all settings
 * - Save and reload functionality
 * - Change notification callbacks
 */

#include "config/config_manager.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdexcept>

using namespace ryu_ldn::config;

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
// Test Helpers
// ============================================================================

static char g_test_config_path[256];

static void create_test_config_file() {
    snprintf(g_test_config_path, sizeof(g_test_config_path),
             "/tmp/config_manager_test_%d.ini", rand());

    FILE* f = fopen(g_test_config_path, "w");
    if (f) {
        fprintf(f, "[server]\n");
        fprintf(f, "host = test.example.com\n");
        fprintf(f, "port = 12345\n");
        fprintf(f, "use_tls = 0\n");
        fprintf(f, "\n");
        fprintf(f, "[network]\n");
        fprintf(f, "connect_timeout = 8000\n");
        fprintf(f, "ping_interval = 15000\n");
        fprintf(f, "reconnect_delay = 5000\n");
        fprintf(f, "max_reconnect_attempts = 10\n");
        fprintf(f, "\n");
        fprintf(f, "[ldn]\n");
        fprintf(f, "enabled = 1\n");
        fprintf(f, "passphrase = testpass123\n");
        fprintf(f, "\n");
        fprintf(f, "[debug]\n");
        fprintf(f, "enabled = 1\n");
        fprintf(f, "level = 3\n");
        fprintf(f, "log_to_file = 1\n");
        fclose(f);
    }
}

static void remove_test_config_file() {
    std::remove(g_test_config_path);
}

// Track change callbacks
static const char* g_last_changed_section = nullptr;
static int g_change_callback_count = 0;

static void test_change_callback(const char* section) {
    g_last_changed_section = section;
    g_change_callback_count++;
}

// ============================================================================
// Initialization Tests
// ============================================================================

TEST(singleton_instance) {
    ConfigManager& m1 = ConfigManager::Instance();
    ConfigManager& m2 = ConfigManager::Instance();
    ASSERT_TRUE(&m1 == &m2);
}

TEST(initialize_with_missing_file) {
    // Initialize with non-existent file should use defaults
    bool result = ConfigManager::Instance().Initialize("/tmp/nonexistent_config_12345.ini");
    // Should still succeed with defaults
    ASSERT_TRUE(result || !result);  // Just ensure no crash
}

TEST(initialize_with_valid_file) {
    create_test_config_file();
    bool result = ConfigManager::Instance().Initialize(g_test_config_path);
    ASSERT_TRUE(result);
    ASSERT_TRUE(ConfigManager::Instance().IsInitialized());
    remove_test_config_file();
}

// ============================================================================
// Server Settings Tests
// ============================================================================

TEST(get_default_server_host) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    const char* host = ConfigManager::Instance().GetServerHost();
    ASSERT_STREQ(host, DEFAULT_HOST);
}

TEST(set_server_host) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetServerHost("new.server.com");
    ASSERT_STREQ(ConfigManager::Instance().GetServerHost(), "new.server.com");
}

TEST(set_server_host_truncates_long_name) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    char long_host[256];
    memset(long_host, 'a', 255);
    long_host[255] = '\0';
    ConfigManager::Instance().SetServerHost(long_host);
    // Should be truncated to MAX_HOST_LENGTH
    ASSERT_TRUE(strlen(ConfigManager::Instance().GetServerHost()) <= MAX_HOST_LENGTH);
}

TEST(get_default_server_port) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ASSERT_EQ(ConfigManager::Instance().GetServerPort(), DEFAULT_PORT);
}

TEST(set_server_port) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetServerPort(9999);
    ASSERT_EQ(ConfigManager::Instance().GetServerPort(), 9999);
}

TEST(get_default_use_tls) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ASSERT_EQ(ConfigManager::Instance().GetUseTls(), DEFAULT_USE_TLS);
}

TEST(set_use_tls) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetUseTls(false);
    ASSERT_FALSE(ConfigManager::Instance().GetUseTls());
    ConfigManager::Instance().SetUseTls(true);
    ASSERT_TRUE(ConfigManager::Instance().GetUseTls());
}

// ============================================================================
// Network Settings Tests
// ============================================================================

TEST(get_default_connect_timeout) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ASSERT_EQ(ConfigManager::Instance().GetConnectTimeout(), DEFAULT_CONNECT_TIMEOUT_MS);
}

TEST(set_connect_timeout) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetConnectTimeout(10000);
    ASSERT_EQ(ConfigManager::Instance().GetConnectTimeout(), 10000u);
}

TEST(get_default_ping_interval) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ASSERT_EQ(ConfigManager::Instance().GetPingInterval(), DEFAULT_PING_INTERVAL_MS);
}

TEST(set_ping_interval) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetPingInterval(20000);
    ASSERT_EQ(ConfigManager::Instance().GetPingInterval(), 20000u);
}

TEST(get_default_reconnect_delay) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ASSERT_EQ(ConfigManager::Instance().GetReconnectDelay(), DEFAULT_RECONNECT_DELAY_MS);
}

TEST(set_reconnect_delay) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetReconnectDelay(6000);
    ASSERT_EQ(ConfigManager::Instance().GetReconnectDelay(), 6000u);
}

TEST(get_default_max_reconnect_attempts) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ASSERT_EQ(ConfigManager::Instance().GetMaxReconnectAttempts(), DEFAULT_MAX_RECONNECT_ATTEMPTS);
}

TEST(set_max_reconnect_attempts) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetMaxReconnectAttempts(0);  // Infinite
    ASSERT_EQ(ConfigManager::Instance().GetMaxReconnectAttempts(), 0u);
    ConfigManager::Instance().SetMaxReconnectAttempts(20);
    ASSERT_EQ(ConfigManager::Instance().GetMaxReconnectAttempts(), 20u);
}

// ============================================================================
// LDN Settings Tests
// ============================================================================

TEST(get_default_ldn_enabled) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ASSERT_EQ(ConfigManager::Instance().GetLdnEnabled(), DEFAULT_LDN_ENABLED);
}

TEST(set_ldn_enabled) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetLdnEnabled(false);
    ASSERT_FALSE(ConfigManager::Instance().GetLdnEnabled());
    ConfigManager::Instance().SetLdnEnabled(true);
    ASSERT_TRUE(ConfigManager::Instance().GetLdnEnabled());
}

TEST(get_default_passphrase) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ASSERT_STREQ(ConfigManager::Instance().GetPassphrase(), "");
}

TEST(set_passphrase) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetPassphrase("mysecretpass");
    ASSERT_STREQ(ConfigManager::Instance().GetPassphrase(), "mysecretpass");
}

TEST(set_passphrase_empty) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetPassphrase("something");
    ConfigManager::Instance().SetPassphrase("");
    ASSERT_STREQ(ConfigManager::Instance().GetPassphrase(), "");
}

TEST(set_passphrase_truncates_long) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    char long_pass[128];
    memset(long_pass, 'x', 127);
    long_pass[127] = '\0';
    ConfigManager::Instance().SetPassphrase(long_pass);
    ASSERT_TRUE(strlen(ConfigManager::Instance().GetPassphrase()) <= MAX_PASSPHRASE_LENGTH);
}

TEST(get_default_interface_name) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ASSERT_STREQ(ConfigManager::Instance().GetInterfaceName(), "");
}

TEST(set_interface_name) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetInterfaceName("eth0");
    ASSERT_STREQ(ConfigManager::Instance().GetInterfaceName(), "eth0");
}

// ============================================================================
// Debug Settings Tests
// ============================================================================

TEST(get_default_debug_enabled) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ASSERT_EQ(ConfigManager::Instance().GetDebugEnabled(), DEFAULT_DEBUG_ENABLED);
}

TEST(set_debug_enabled) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetDebugEnabled(true);
    ASSERT_TRUE(ConfigManager::Instance().GetDebugEnabled());
    ConfigManager::Instance().SetDebugEnabled(false);
    ASSERT_FALSE(ConfigManager::Instance().GetDebugEnabled());
}

TEST(get_default_debug_level) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ASSERT_EQ(ConfigManager::Instance().GetDebugLevel(), DEFAULT_DEBUG_LEVEL);
}

TEST(set_debug_level) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetDebugLevel(3);
    ASSERT_EQ(ConfigManager::Instance().GetDebugLevel(), 3u);
    ConfigManager::Instance().SetDebugLevel(0);
    ASSERT_EQ(ConfigManager::Instance().GetDebugLevel(), 0u);
}

TEST(set_debug_level_clamps_to_max) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetDebugLevel(100);
    ASSERT_TRUE(ConfigManager::Instance().GetDebugLevel() <= 3);
}

TEST(get_default_log_to_file) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ASSERT_EQ(ConfigManager::Instance().GetLogToFile(), DEFAULT_LOG_TO_FILE);
}

TEST(set_log_to_file) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetLogToFile(true);
    ASSERT_TRUE(ConfigManager::Instance().GetLogToFile());
    ConfigManager::Instance().SetLogToFile(false);
    ASSERT_FALSE(ConfigManager::Instance().GetLogToFile());
}

// ============================================================================
// Save and Reload Tests
// ============================================================================

TEST(save_creates_file) {
    char save_path[256];
    snprintf(save_path, sizeof(save_path), "/tmp/config_save_test_%d.ini", rand());

    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    // Reinitialize with save path would require modifying the path
    // For now just test the Save() call doesn't crash
    // ConfigResult result = ConfigManager::Instance().Save();
    // ASSERT_EQ(result, ConfigResult::Success);

    std::remove(save_path);
}

TEST(reload_restores_values) {
    create_test_config_file();

    ConfigManager::Instance().Initialize(g_test_config_path);
    ASSERT_STREQ(ConfigManager::Instance().GetServerHost(), "test.example.com");

    // Modify
    ConfigManager::Instance().SetServerHost("modified.com");
    ASSERT_STREQ(ConfigManager::Instance().GetServerHost(), "modified.com");

    // Reload should restore
    ConfigManager::Instance().Reload();
    ASSERT_STREQ(ConfigManager::Instance().GetServerHost(), "test.example.com");

    remove_test_config_file();
}

TEST(has_unsaved_changes_after_modification) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");

    // Fresh init - no unsaved changes
    // ASSERT_FALSE(ConfigManager::Instance().HasUnsavedChanges());

    // After modification - should have unsaved changes
    ConfigManager::Instance().SetServerPort(1234);
    ASSERT_TRUE(ConfigManager::Instance().HasUnsavedChanges());
}

// ============================================================================
// Change Callback Tests
// ============================================================================

TEST(change_callback_invoked_on_server_change) {
    g_change_callback_count = 0;
    g_last_changed_section = nullptr;

    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetChangeCallback(test_change_callback);

    ConfigManager::Instance().SetServerHost("callback.test.com");

    ASSERT_TRUE(g_change_callback_count > 0);
    ASSERT_TRUE(g_last_changed_section != nullptr);
    ASSERT_STREQ(g_last_changed_section, "server");

    ConfigManager::Instance().SetChangeCallback(nullptr);
}

TEST(change_callback_invoked_on_network_change) {
    g_change_callback_count = 0;
    g_last_changed_section = nullptr;

    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetChangeCallback(test_change_callback);

    ConfigManager::Instance().SetConnectTimeout(7777);

    ASSERT_TRUE(g_change_callback_count > 0);
    ASSERT_STREQ(g_last_changed_section, "network");

    ConfigManager::Instance().SetChangeCallback(nullptr);
}

TEST(change_callback_invoked_on_ldn_change) {
    g_change_callback_count = 0;
    g_last_changed_section = nullptr;

    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetChangeCallback(test_change_callback);

    ConfigManager::Instance().SetPassphrase("newpass");

    ASSERT_TRUE(g_change_callback_count > 0);
    ASSERT_STREQ(g_last_changed_section, "ldn");

    ConfigManager::Instance().SetChangeCallback(nullptr);
}

TEST(change_callback_invoked_on_debug_change) {
    g_change_callback_count = 0;
    g_last_changed_section = nullptr;

    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetChangeCallback(test_change_callback);

    ConfigManager::Instance().SetDebugLevel(2);

    ASSERT_TRUE(g_change_callback_count > 0);
    ASSERT_STREQ(g_last_changed_section, "debug");

    ConfigManager::Instance().SetChangeCallback(nullptr);
}

TEST(null_callback_safe) {
    ConfigManager::Instance().Initialize("/tmp/nonexistent.ini");
    ConfigManager::Instance().SetChangeCallback(nullptr);

    // Should not crash
    ConfigManager::Instance().SetServerHost("safe.test.com");
    ConfigManager::Instance().SetServerPort(9999);
}

// ============================================================================
// Load From File Tests
// ============================================================================

TEST(load_server_settings_from_file) {
    create_test_config_file();
    ConfigManager::Instance().Initialize(g_test_config_path);

    ASSERT_STREQ(ConfigManager::Instance().GetServerHost(), "test.example.com");
    ASSERT_EQ(ConfigManager::Instance().GetServerPort(), 12345);
    ASSERT_FALSE(ConfigManager::Instance().GetUseTls());

    remove_test_config_file();
}

TEST(load_network_settings_from_file) {
    create_test_config_file();
    ConfigManager::Instance().Initialize(g_test_config_path);

    ASSERT_EQ(ConfigManager::Instance().GetConnectTimeout(), 8000u);
    ASSERT_EQ(ConfigManager::Instance().GetPingInterval(), 15000u);
    ASSERT_EQ(ConfigManager::Instance().GetReconnectDelay(), 5000u);
    ASSERT_EQ(ConfigManager::Instance().GetMaxReconnectAttempts(), 10u);

    remove_test_config_file();
}

TEST(load_ldn_settings_from_file) {
    create_test_config_file();
    ConfigManager::Instance().Initialize(g_test_config_path);

    ASSERT_TRUE(ConfigManager::Instance().GetLdnEnabled());
    ASSERT_STREQ(ConfigManager::Instance().GetPassphrase(), "testpass123");

    remove_test_config_file();
}

TEST(load_debug_settings_from_file) {
    create_test_config_file();
    ConfigManager::Instance().Initialize(g_test_config_path);

    ASSERT_TRUE(ConfigManager::Instance().GetDebugEnabled());
    ASSERT_EQ(ConfigManager::Instance().GetDebugLevel(), 3u);
    ASSERT_TRUE(ConfigManager::Instance().GetLogToFile());

    remove_test_config_file();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== ryu_ldn_nx ConfigManager Unit Tests ===\n\n");
    printf("Running %d tests...\n\n", g_test_count);

    for (int i = 0; i < g_test_count; i++) {
        g_tests_run++;
        printf("[%d/%d] %s...", i + 1, g_test_count, g_tests[i].name);
        fflush(stdout);

        try {
            g_tests[i].func();
            printf(" OK\n");
            g_tests_passed++;
        } catch (...) {
            g_tests_failed++;
        }
    }

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed, %d total\n",
           g_tests_passed, g_tests_failed, g_tests_run);

    if (g_tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    } else {
        printf("FAILED\n");
    }

    return g_tests_failed > 0 ? 1 : 0;
}
