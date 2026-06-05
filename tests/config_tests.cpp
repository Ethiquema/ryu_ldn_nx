/**
 * @file config_tests.cpp
 * @brief Unit tests for Configuration Manager
 *
 * Tests for parsing INI config files and default values.
 */

#include "config/config.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>

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

// Helper to create temp config file
class TempConfigFile {
public:
    TempConfigFile(const char* content) {
        snprintf(m_path, sizeof(m_path), "/tmp/test_config_%d.ini", rand());
        std::ofstream f(m_path);
        if (f.is_open()) {
            f << content;
            f.close();
        }
    }

    ~TempConfigFile() {
        std::remove(m_path);
    }

    const char* path() const { return m_path; }

private:
    char m_path[256];
};

// ============================================================================
// Default Values Tests
// ============================================================================

TEST(default_values) {
    Config config = get_default_config();

    // Server defaults
    ASSERT_STREQ(config.server.host, "90.93.156.13");
    ASSERT_EQ(config.server.port, 30456);

    // LDN defaults
    ASSERT_EQ(config.ldn.enabled, true);
    ASSERT_STREQ(config.ldn.passphrase, "");

    // Debug defaults
    ASSERT_EQ(config.debug.enabled, false);
    ASSERT_EQ(config.debug.level, 1u);

    // LDN passphrase filtering default
    ASSERT_EQ(config.ldn.use_passphrase, false);

    // P2P proxy default (disabled by default)
    ASSERT_EQ(config.ldn.disable_p2p, true);
}

// ============================================================================
// Parse Tests
// ============================================================================

TEST(parse_empty_file) {
    TempConfigFile file("");
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    // Should have defaults
    ASSERT_STREQ(config.server.host, "90.93.156.13");
}

TEST(parse_server_section) {
    const char* content =
        "[server]\n"
        "host = 192.168.1.100\n"
        "port = 12345\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_STREQ(config.server.host, "192.168.1.100");
    ASSERT_EQ(config.server.port, 12345);
}

TEST(parse_ldn_section) {
    const char* content =
        "[ldn]\n"
        "enabled = 0\n"
        "use_passphrase = 1\n"
        "passphrase = secret123\n"
        "disable_p2p = 0\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(config.ldn.enabled, false);
    ASSERT_EQ(config.ldn.use_passphrase, true);
    ASSERT_STREQ(config.ldn.passphrase, "secret123");
    ASSERT_EQ(config.ldn.disable_p2p, false);
}

TEST(parse_debug_section) {
    const char* content =
        "[debug]\n"
        "enabled = 1\n"
        "level = 3\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(config.debug.enabled, true);
    ASSERT_EQ(config.debug.level, 3u);
}

TEST(parse_comments_ignored) {
    const char* content =
        "; This is a comment\n"
        "[server]\n"
        "; Another comment\n"
        "host = test.server.com\n"
        "  ; Indented comment\n"
        "port = 9999\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_STREQ(config.server.host, "test.server.com");
    ASSERT_EQ(config.server.port, 9999);
}

TEST(parse_whitespace_handling) {
    const char* content =
        "[server]\n"
        "  host   =   spaced.server.com  \n"
        "port=12345\n";  // No spaces

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_STREQ(config.server.host, "spaced.server.com");
    ASSERT_EQ(config.server.port, 12345);
}

TEST(parse_unknown_section_ignored) {
    const char* content =
        "[unknown]\n"
        "foo = bar\n"
        "[server]\n"
        "port = 11111\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(config.server.port, 11111);
}

TEST(parse_unknown_key_ignored) {
    const char* content =
        "[server]\n"
        "unknown_key = value\n"
        "port = 22222\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(config.server.port, 22222);
}

TEST(file_not_found) {
    Config config = get_default_config();
    ConfigResult result = load_config("/nonexistent/path/config.ini", config);

    ASSERT_EQ(result, ConfigResult::FileNotFound);
    // Config should still have defaults
    ASSERT_STREQ(config.server.host, "90.93.156.13");
}

TEST(passphrase_truncated) {
    // Passphrase > 64 chars should be truncated
    const char* content =
        "[ldn]\n"
        "passphrase = 12345678901234567890123456789012345678901234567890123456789012345678901234567890\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(strlen(config.ldn.passphrase), 64u);
}

TEST(host_truncated) {
    // Host > 128 chars should be truncated
    char long_host[200];
    memset(long_host, 'a', sizeof(long_host) - 1);
    long_host[sizeof(long_host) - 1] = '\0';

    char content[512];
    snprintf(content, sizeof(content), "[server]\nhost = %s\n", long_host);

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(strlen(config.server.host), 128u);
}

TEST(full_config_example) {
    const char* content =
        "; Full example\n"
        "[server]\n"
        "host = custom.ldn.server\n"
        "port = 30000\n"
        "\n"
        "[ldn]\n"
        "enabled = 1\n"
        "use_passphrase = 0\n"
        "passphrase = myroom\n"
        "disable_p2p = 0\n"
        "\n"
        "[debug]\n"
        "enabled = 1\n"
        "level = 2\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_STREQ(config.server.host, "custom.ldn.server");
    ASSERT_EQ(config.server.port, 30000);
    ASSERT_EQ(config.ldn.enabled, true);
    ASSERT_STREQ(config.ldn.passphrase, "myroom");
    ASSERT_EQ(config.ldn.use_passphrase, false);
    ASSERT_EQ(config.ldn.disable_p2p, false);
    ASSERT_EQ(config.debug.enabled, true);
    ASSERT_EQ(config.debug.level, 2u);
}

// ============================================================================
// Save Config Tests
// ============================================================================

TEST(save_config_creates_file) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/test_save_config_%d.ini", rand());

    Config config = get_default_config();
    ConfigResult result = save_config(path, config);

    ASSERT_EQ(result, ConfigResult::Success);

    // Verify file exists and can be loaded back
    Config loaded = get_default_config();
    loaded.server.port = 0;  // Change to verify it gets loaded
    result = load_config(path, loaded);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(loaded.server.port, DEFAULT_PORT);

    std::remove(path);
}

TEST(save_config_preserves_values) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/test_save_preserve_%d.ini", rand());

    Config config = get_default_config();
    // Modify some values
    strcpy(config.server.host, "test.example.com");
    config.server.port = 12345;
    config.ldn.enabled = false;
    config.ldn.use_passphrase = true;
    strcpy(config.ldn.passphrase, "testpass");
    config.ldn.disable_p2p = false;
    config.debug.enabled = true;
    config.debug.level = 3;

    ConfigResult result = save_config(path, config);
    ASSERT_EQ(result, ConfigResult::Success);

    // Load it back
    Config loaded = get_default_config();
    result = load_config(path, loaded);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_STREQ(loaded.server.host, "test.example.com");
    ASSERT_EQ(loaded.server.port, 12345);
    ASSERT_EQ(loaded.ldn.enabled, false);
    ASSERT_STREQ(loaded.ldn.passphrase, "testpass");
    ASSERT_EQ(loaded.ldn.use_passphrase, true);
    ASSERT_EQ(loaded.ldn.disable_p2p, false);
    ASSERT_EQ(loaded.debug.enabled, true);
    ASSERT_EQ(loaded.debug.level, 3u);

    std::remove(path);
}

// ============================================================================
// Ensure Config Exists Tests
// ============================================================================

TEST(ensure_config_exists_creates_new) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/test_ensure_%d.ini", rand());

    // Make sure file doesn't exist
    std::remove(path);

    ConfigResult result = ensure_config_exists(path);
    ASSERT_EQ(result, ConfigResult::Success);

    // Verify file was created with defaults
    Config loaded = get_default_config();
    loaded.server.port = 0;  // Change to verify
    result = load_config(path, loaded);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(loaded.server.port, DEFAULT_PORT);
    ASSERT_STREQ(loaded.server.host, DEFAULT_HOST);

    std::remove(path);
}

TEST(ensure_config_exists_keeps_existing) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/test_ensure_existing_%d.ini", rand());

    // Create a config with custom values
    const char* content =
        "[server]\n"
        "host = custom.host.com\n"
        "port = 54321\n";

    std::ofstream f(path);
    f << content;
    f.close();

    // ensure_config_exists should not overwrite
    ConfigResult result = ensure_config_exists(path);
    ASSERT_EQ(result, ConfigResult::Success);

    // Verify original values are preserved
    Config loaded = get_default_config();
    result = load_config(path, loaded);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_STREQ(loaded.server.host, "custom.host.com");
    ASSERT_EQ(loaded.server.port, 54321);

    std::remove(path);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== ryu_ldn_nx Config Unit Tests ===\n\n");
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
