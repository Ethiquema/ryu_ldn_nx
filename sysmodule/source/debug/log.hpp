/**
 * @file log.hpp
 * @brief Debug Logging System for ryu_ldn_nx
 *
 * Provides configurable logging functionality for debugging and troubleshooting.
 * Supports multiple log levels, console output, and optional file logging.
 *
 * ## Design Principles
 *
 * 1. **Zero Overhead When Disabled**: Logging calls compile to no-ops when
 *    debug is disabled, ensuring no performance impact in production.
 *
 * 2. **No Dynamic Allocation**: Uses fixed-size buffers suitable for
 *    embedded/sysmodule use on Nintendo Switch.
 *
 * 3. **Thread-Safe**: All logging operations are thread-safe.
 *
 * 4. **Configurable at Runtime**: Log level and file output can be changed
 *    via config.ini without recompilation.
 *
 * ## Log Levels
 *
 * - **Error (0)**: Critical issues that prevent normal operation
 * - **Warning (1)**: Potential problems that don't prevent operation
 * - **Info (2)**: Normal operational messages
 * - **Verbose (3)**: Detailed debugging information
 *
 * ## Usage Example
 *
 * @code
 * #include "debug/log.hpp"
 *
 * // Initialize logger (typically done once at startup)
 * ryu_ldn::debug::g_logger.init(config.debug);
 *
 * // Use logging macros
 * LOG_ERROR("Connection failed: %s", error_msg);
 * LOG_WARN("Retrying connection, attempt %d", attempt);
 * LOG_INFO("Connected to server %s:%d", host, port);
 * LOG_VERBOSE("Packet received: %zu bytes", size);
 * @endcode
 *
 * ## File Logging
 *
 * When enabled, logs are written to: `/config/ryu_ldn_nx/ryu_ldn_nx.log`
 *
 * @see config/config.hpp for DebugConfig structure
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdarg>

// Forward declaration to avoid circular include
namespace ryu_ldn::config {
    struct DebugConfig;
}

namespace ryu_ldn::debug {

// =============================================================================
// Constants
// =============================================================================

/** @brief Maximum length of a single log message */
constexpr size_t MAX_LOG_MESSAGE_LENGTH = 256;

/** @brief Maximum number of messages in the circular log buffer */
constexpr size_t MAX_LOG_BUFFER_ENTRIES = 64;

/** @brief Default log file path on SD card */
constexpr const char* DEFAULT_LOG_PATH = "sdmc:/config/ryu_ldn_nx/ryu_ldn_nx.log";

// =============================================================================
// Log Levels
// =============================================================================

/**
 * @brief Log severity levels
 *
 * Lower values indicate higher severity. The configured level determines
 * which messages are output - only messages at or below the configured
 * level will be logged.
 */
enum class LogLevel : uint32_t {
    Error = 0,      ///< Critical errors (always logged when enabled)
    Warning = 1,    ///< Warnings (potential issues)
    Info = 2,       ///< Informational messages
    Verbose = 3     ///< Detailed debug output
};

/**
 * @brief Convert LogLevel to human-readable string
 *
 * @param level The log level
 * @return Static string representation (e.g., "ERROR", "WARN")
 */
inline const char* log_level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Verbose: return "VERBOSE";
        default:                return "UNKNOWN";
    }
}

// =============================================================================
// Log Message Formatting
// =============================================================================

/**
 * @brief Format a log message with timestamp and level prefix
 *
 * @param buffer Output buffer for formatted message
 * @param buffer_size Size of output buffer
 * @param level Log level for prefix
 * @param format printf-style format string
 * @param ... Format arguments
 */
void format_log_message(char* buffer, size_t buffer_size, LogLevel level,
                        const char* format, ...);

/**
 * @brief Format a log message with va_list
 *
 * @param buffer Output buffer for formatted message
 * @param buffer_size Size of output buffer
 * @param level Log level for prefix
 * @param format printf-style format string
 * @param args Format arguments as va_list
 */
void format_log_message_v(char* buffer, size_t buffer_size, LogLevel level,
                          const char* format, va_list args);

// =============================================================================
// Circular Log Buffer
// =============================================================================

/**
 * @brief Circular buffer for storing recent log messages
 *
 * Stores the most recent log messages in memory for display in the
 * Tesla overlay or other debugging tools.
 */
class LogBuffer {
public:
    /**
     * @brief Initialize the log buffer
     *
     * @param capacity Maximum number of messages to store (up to MAX_LOG_BUFFER_ENTRIES)
     */
    void init(size_t capacity = MAX_LOG_BUFFER_ENTRIES);

    /**
     * @brief Add a message to the buffer
     *
     * If buffer is full, oldest message is overwritten.
     *
     * @param message Null-terminated message string
     */
    void add(const char* message);

    /**
     * @brief Get message at index
     *
     * @param index Index from oldest (0) to newest (count-1)
     * @return Message string or nullptr if index invalid
     */
    const char* get(size_t index) const;

    /**
     * @brief Get all messages as a single string
     *
     * @param buffer Output buffer
     * @param buffer_size Size of output buffer
     */
    void get_all(char* buffer, size_t buffer_size) const;

    /**
     * @brief Get number of messages in buffer
     */
    size_t count() const { return m_count; }

    /**
     * @brief Clear all messages
     */
    void clear();

private:
    char m_messages[MAX_LOG_BUFFER_ENTRIES][MAX_LOG_MESSAGE_LENGTH];
    size_t m_capacity = MAX_LOG_BUFFER_ENTRIES;
    size_t m_count = 0;
    size_t m_head = 0;  // Index of oldest message
    size_t m_tail = 0;  // Index where next message will be written
};

// =============================================================================
// Logger Class
// =============================================================================

/**
 * @brief Main logger class
 *
 * Handles log message formatting, filtering by level, and output to
 * console and/or file. Thread-safe for concurrent logging.
 */
class Logger {
public:
    Logger() = default;
    ~Logger();

    // Non-copyable
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * @brief Initialize logger with configuration
     *
     * @param config Debug configuration from config.ini
     * @param log_path Optional custom log file path (default: DEFAULT_LOG_PATH)
     */
    void init(const config::DebugConfig& config,
              const char* log_path = DEFAULT_LOG_PATH);

    /**
     * @brief Check if logging is enabled
     */
    bool is_enabled() const { return m_enabled; }

    /**
     * @brief Get current log level
     */
    LogLevel get_level() const { return m_level; }

    /**
     * @brief Check if a message at given level should be logged
     *
     * @param level Log level to check
     * @return true if message should be logged
     */
    bool should_log(LogLevel level) const;

    /**
     * @brief Log a message
     *
     * @param level Log level
     * @param format printf-style format string
     * @param ... Format arguments
     */
    void log(LogLevel level, const char* format, ...);

    /**
     * @brief Log a message with va_list
     *
     * @param level Log level
     * @param format printf-style format string
     * @param args Format arguments
     */
    void log_v(LogLevel level, const char* format, va_list args);

    /**
     * @brief Flush any buffered output to file
     */
    void flush();

    /**
     * @brief Get access to the log buffer for overlay display
     *
     * @return Reference to internal log buffer
     */
    const LogBuffer& get_buffer() const { return m_buffer; }

    /**
     * @brief Get mutable access to log buffer
     *
     * @return Reference to internal log buffer
     */
    LogBuffer& get_buffer() { return m_buffer; }

    /**
     * @brief Check and close file if idle timeout expired
     *
     * Should be called periodically. Closes the file if no writes
     * occurred within the timeout period (5 seconds).
     */
    void check_idle_timeout();

private:
    void output_message(const char* message);
    void open_file();
    void close_file();

    bool m_enabled = false;
    LogLevel m_level = LogLevel::Warning;
    bool m_log_to_file = false;
    char m_log_path[256] = {0};
    LogBuffer m_buffer;
    void* m_file = nullptr;  // FILE* on PC, unused on Switch
    bool m_file_open = false;
    size_t m_file_offset = 0;
    uint64_t m_last_write_tick = 0;  // Last write timestamp (ticks)
    bool m_header_written = false;   // Track if header was written this session

    static constexpr uint64_t FILE_IDLE_TIMEOUT_NS = 5000000000ULL;  // 5 seconds in nanoseconds
};

// =============================================================================
// Global Logger Instance
// =============================================================================

/**
 * @brief Global logger instance
 *
 * Use this for all logging throughout the sysmodule. Initialize once
 * at startup with g_logger.init(config.debug).
 */
extern Logger g_logger;

// =============================================================================
// Logging Macros
// =============================================================================

/**
 * @brief Log an error message
 *
 * Always logged when debug is enabled (level >= 0).
 *
 * @note Uses GNU extension ##__VA_ARGS__ to handle zero variadic arguments.
 *       This is well-supported by GCC, Clang, and devkitPro toolchains.
 */
#define LOG_ERROR(fmt, ...) \
    do { \
        if (ryu_ldn::debug::g_logger.should_log(ryu_ldn::debug::LogLevel::Error)) { \
            ryu_ldn::debug::g_logger.log(ryu_ldn::debug::LogLevel::Error, fmt, ##__VA_ARGS__); \
        } \
    } while(0)

/**
 * @brief Log a warning message
 *
 * Logged when debug level >= 1.
 */
#define LOG_WARN(fmt, ...) \
    do { \
        if (ryu_ldn::debug::g_logger.should_log(ryu_ldn::debug::LogLevel::Warning)) { \
            ryu_ldn::debug::g_logger.log(ryu_ldn::debug::LogLevel::Warning, fmt, ##__VA_ARGS__); \
        } \
    } while(0)

/**
 * @brief Log an info message
 *
 * Logged when debug level >= 2.
 */
#define LOG_INFO(fmt, ...) \
    do { \
        if (ryu_ldn::debug::g_logger.should_log(ryu_ldn::debug::LogLevel::Info)) { \
            ryu_ldn::debug::g_logger.log(ryu_ldn::debug::LogLevel::Info, fmt, ##__VA_ARGS__); \
        } \
    } while(0)

/**
 * @brief Log a verbose debug message
 *
 * Logged when debug level >= 3.
 */
#define LOG_VERBOSE(fmt, ...) \
    do { \
        if (ryu_ldn::debug::g_logger.should_log(ryu_ldn::debug::LogLevel::Verbose)) { \
            ryu_ldn::debug::g_logger.log(ryu_ldn::debug::LogLevel::Verbose, fmt, ##__VA_ARGS__); \
        } \
    } while(0)

} // namespace ryu_ldn::debug
