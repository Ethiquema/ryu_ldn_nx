/**
 * @file log_tests.cpp
 * @brief Unit tests for Logging System
 *
 * Tests for debug logging functionality including log levels,
 * file output, and conditional logging based on configuration.
 */

#include "debug/log.hpp"
#include "config/config.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace ryu_ldn::debug;
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
// Log Level Tests
// ============================================================================

TEST(log_level_to_string) {
    ASSERT_STREQ(log_level_to_string(LogLevel::Error), "ERROR");
    ASSERT_STREQ(log_level_to_string(LogLevel::Warning), "WARN");
    ASSERT_STREQ(log_level_to_string(LogLevel::Info), "INFO");
    ASSERT_STREQ(log_level_to_string(LogLevel::Verbose), "VERBOSE");
}

TEST(log_level_from_config) {
    ASSERT_EQ(static_cast<uint32_t>(LogLevel::Error), 0u);
    ASSERT_EQ(static_cast<uint32_t>(LogLevel::Warning), 1u);
    ASSERT_EQ(static_cast<uint32_t>(LogLevel::Info), 2u);
    ASSERT_EQ(static_cast<uint32_t>(LogLevel::Verbose), 3u);
}

// ============================================================================
// Logger Initialization Tests
// ============================================================================

TEST(logger_init_disabled) {
    DebugConfig config{};
    config.enabled = false;
    config.level = 3;
    config.log_to_file = false;

    Logger logger;
    logger.init(config);

    ASSERT_FALSE(logger.is_enabled());
}

TEST(logger_init_enabled) {
    DebugConfig config{};
    config.enabled = true;
    config.level = 2;
    config.log_to_file = false;

    Logger logger;
    logger.init(config);

    ASSERT_TRUE(logger.is_enabled());
    ASSERT_EQ(static_cast<uint32_t>(logger.get_level()), 2u);
}

TEST(logger_should_log_level_filtering) {
    DebugConfig config{};
    config.enabled = true;
    config.level = 1;  // Warning level
    config.log_to_file = false;

    Logger logger;
    logger.init(config);

    // Should log Error (0) and Warning (1)
    ASSERT_TRUE(logger.should_log(LogLevel::Error));
    ASSERT_TRUE(logger.should_log(LogLevel::Warning));
    // Should NOT log Info (2) or Verbose (3)
    ASSERT_FALSE(logger.should_log(LogLevel::Info));
    ASSERT_FALSE(logger.should_log(LogLevel::Verbose));
}

TEST(logger_should_log_all_levels) {
    DebugConfig config{};
    config.enabled = true;
    config.level = 3;  // Verbose (all levels)
    config.log_to_file = false;

    Logger logger;
    logger.init(config);

    ASSERT_TRUE(logger.should_log(LogLevel::Error));
    ASSERT_TRUE(logger.should_log(LogLevel::Warning));
    ASSERT_TRUE(logger.should_log(LogLevel::Info));
    ASSERT_TRUE(logger.should_log(LogLevel::Verbose));
}

TEST(logger_disabled_never_logs) {
    DebugConfig config{};
    config.enabled = false;
    config.level = 3;  // Even at verbose
    config.log_to_file = false;

    Logger logger;
    logger.init(config);

    ASSERT_FALSE(logger.should_log(LogLevel::Error));
    ASSERT_FALSE(logger.should_log(LogLevel::Warning));
    ASSERT_FALSE(logger.should_log(LogLevel::Info));
    ASSERT_FALSE(logger.should_log(LogLevel::Verbose));
}

// ============================================================================
// Log Message Formatting Tests
// ============================================================================

TEST(format_log_message_basic) {
    char buffer[256];
    format_log_message(buffer, sizeof(buffer), LogLevel::Info, "Test message");

    // Should contain level and message
    ASSERT_TRUE(strstr(buffer, "[INFO]") != nullptr);
    ASSERT_TRUE(strstr(buffer, "Test message") != nullptr);
}

TEST(format_log_message_with_args) {
    char buffer[256];
    format_log_message(buffer, sizeof(buffer), LogLevel::Error, "Error code: %d", 42);

    ASSERT_TRUE(strstr(buffer, "[ERROR]") != nullptr);
    ASSERT_TRUE(strstr(buffer, "Error code: 42") != nullptr);
}

TEST(format_log_message_truncation) {
    char buffer[32];
    format_log_message(buffer, sizeof(buffer), LogLevel::Info,
        "This is a very long message that should be truncated");

    // Buffer should be null-terminated and not overflow
    ASSERT_TRUE(strlen(buffer) < sizeof(buffer));
    ASSERT_TRUE(buffer[sizeof(buffer) - 1] == '\0' || strlen(buffer) < sizeof(buffer));
}

// ============================================================================
// File Logging Tests
// ============================================================================

TEST(logger_file_output_enabled) {
    char log_path[256];
    snprintf(log_path, sizeof(log_path), "/tmp/test_log_%d.log", rand());

    // Remove if exists
    std::remove(log_path);

    DebugConfig config{};
    config.enabled = true;
    config.level = 3;
    config.log_to_file = true;

    Logger logger;
    logger.init(config, log_path);

    // Log a message
    logger.log(LogLevel::Info, "Test file logging");
    logger.flush();

    // Verify file was created and contains our message
    std::ifstream f(log_path);
    ASSERT_TRUE(f.is_open());

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    f.close();

    ASSERT_TRUE(content.find("Test file logging") != std::string::npos);

    std::remove(log_path);
}

TEST(logger_file_output_disabled) {
    char log_path[256];
    snprintf(log_path, sizeof(log_path), "/tmp/test_log_disabled_%d.log", rand());

    // Remove if exists
    std::remove(log_path);

    DebugConfig config{};
    config.enabled = true;
    config.level = 3;
    config.log_to_file = false;  // File logging disabled

    Logger logger;
    logger.init(config, log_path);

    // Log a message
    logger.log(LogLevel::Info, "This should not go to file");

    // File should NOT exist
    std::ifstream f(log_path);
    ASSERT_FALSE(f.is_open());
}

// ============================================================================
// Log Buffer Tests
// ============================================================================

TEST(log_buffer_stores_messages) {
    LogBuffer buffer;
    buffer.init(5);  // Capacity of 5 messages

    buffer.add("Message 1");
    buffer.add("Message 2");
    buffer.add("Message 3");

    ASSERT_EQ(buffer.count(), 3u);
}

TEST(log_buffer_circular) {
    LogBuffer buffer;
    buffer.init(3);  // Small capacity

    buffer.add("Message 1");
    buffer.add("Message 2");
    buffer.add("Message 3");
    buffer.add("Message 4");  // Should overwrite Message 1

    ASSERT_EQ(buffer.count(), 3u);

    // First message should now be Message 2
    const char* first = buffer.get(0);
    ASSERT_TRUE(first != nullptr);
    ASSERT_TRUE(strstr(first, "Message 2") != nullptr);
}

TEST(log_buffer_get_all) {
    LogBuffer buffer;
    buffer.init(10);

    buffer.add("Line 1");
    buffer.add("Line 2");
    buffer.add("Line 3");

    char output[512];
    buffer.get_all(output, sizeof(output));

    ASSERT_TRUE(strstr(output, "Line 1") != nullptr);
    ASSERT_TRUE(strstr(output, "Line 2") != nullptr);
    ASSERT_TRUE(strstr(output, "Line 3") != nullptr);
}

TEST(log_buffer_clear) {
    LogBuffer buffer;
    buffer.init(5);

    buffer.add("Message 1");
    buffer.add("Message 2");
    ASSERT_EQ(buffer.count(), 2u);

    buffer.clear();
    ASSERT_EQ(buffer.count(), 0u);
}

// ============================================================================
// Integration with Config Tests
// ============================================================================

TEST(logger_from_full_config) {
    Config config = get_default_config();
    config.debug.enabled = true;
    config.debug.level = 2;
    config.debug.log_to_file = false;

    Logger logger;
    logger.init(config.debug);

    ASSERT_TRUE(logger.is_enabled());
    ASSERT_EQ(static_cast<uint32_t>(logger.get_level()), 2u);
    ASSERT_TRUE(logger.should_log(LogLevel::Error));
    ASSERT_TRUE(logger.should_log(LogLevel::Warning));
    ASSERT_TRUE(logger.should_log(LogLevel::Info));
    ASSERT_FALSE(logger.should_log(LogLevel::Verbose));
}

TEST(logger_default_config_disabled) {
    Config config = get_default_config();
    // Default should have debug disabled

    Logger logger;
    logger.init(config.debug);

    ASSERT_FALSE(logger.is_enabled());
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== ryu_ldn_nx Logging Unit Tests ===\n\n");
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
