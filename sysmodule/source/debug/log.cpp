/**
 * @file log.cpp
 * @brief Debug Logging System Implementation
 */

#include "log.hpp"
#include "../config/config.hpp"
#include <cstdio>
#include <cstring>
#include <cstdarg>

namespace ryu_ldn::debug {

// =============================================================================
// Global Logger Instance
// =============================================================================

Logger g_logger;

// =============================================================================
// Helper Functions
// =============================================================================

namespace {

/**
 * @brief Copy string with length limit
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
 * @brief Get a simple timestamp string
 *
 * For Switch sysmodule, we use a simple counter or tick-based timestamp
 * since we don't have easy access to wall clock time.
 */
void get_timestamp(char* buffer, size_t buffer_size) {
    // Simple format: just use a static counter for now
    // In production, this could use os::Tick or similar
    static uint32_t s_log_counter = 0;
    snprintf(buffer, buffer_size, "%06u", s_log_counter++);
}

} // anonymous namespace

// =============================================================================
// Message Formatting
// =============================================================================

void format_log_message(char* buffer, size_t buffer_size, LogLevel level,
                        const char* format, ...) {
    va_list args;
    va_start(args, format);
    format_log_message_v(buffer, buffer_size, level, format, args);
    va_end(args);
}

void format_log_message_v(char* buffer, size_t buffer_size, LogLevel level,
                          const char* format, va_list args) {
    if (buffer_size == 0) return;

    // Format: [TIMESTAMP] [LEVEL] message
    char timestamp[16];
    get_timestamp(timestamp, sizeof(timestamp));

    int prefix_len = snprintf(buffer, buffer_size, "[%s] [%s] ",
                               timestamp, log_level_to_string(level));

    if (prefix_len < 0 || static_cast<size_t>(prefix_len) >= buffer_size) {
        buffer[buffer_size - 1] = '\0';
        return;
    }

    // Append the actual message
    vsnprintf(buffer + prefix_len, buffer_size - prefix_len, format, args);
    buffer[buffer_size - 1] = '\0';
}

// =============================================================================
// LogBuffer Implementation
// =============================================================================

void LogBuffer::init(size_t capacity) {
    m_capacity = (capacity > MAX_LOG_BUFFER_ENTRIES) ? MAX_LOG_BUFFER_ENTRIES : capacity;
    clear();
}

void LogBuffer::add(const char* message) {
    if (message == nullptr || m_capacity == 0) return;

    // Copy message to current tail position
    safe_strcpy(m_messages[m_tail], message, MAX_LOG_MESSAGE_LENGTH - 1);

    // Advance tail
    m_tail = (m_tail + 1) % m_capacity;

    // If buffer is full, advance head (overwrite oldest)
    if (m_count == m_capacity) {
        m_head = (m_head + 1) % m_capacity;
    } else {
        m_count++;
    }
}

const char* LogBuffer::get(size_t index) const {
    if (index >= m_count) return nullptr;

    size_t actual_index = (m_head + index) % m_capacity;
    return m_messages[actual_index];
}

void LogBuffer::get_all(char* buffer, size_t buffer_size) const {
    if (buffer == nullptr || buffer_size == 0) return;

    buffer[0] = '\0';
    size_t offset = 0;

    for (size_t i = 0; i < m_count && offset < buffer_size - 1; i++) {
        const char* msg = get(i);
        if (msg == nullptr) continue;

        size_t msg_len = strlen(msg);
        size_t remaining = buffer_size - offset - 1;

        if (msg_len > remaining) {
            msg_len = remaining;
        }

        memcpy(buffer + offset, msg, msg_len);
        offset += msg_len;

        // Add newline if space
        if (offset < buffer_size - 1) {
            buffer[offset++] = '\n';
        }
    }

    buffer[offset] = '\0';
}

void LogBuffer::clear() {
    m_count = 0;
    m_head = 0;
    m_tail = 0;
}

// =============================================================================
// Logger Implementation
// =============================================================================

Logger::~Logger() {
    if (m_file != nullptr) {
        std::fclose(static_cast<FILE*>(m_file));
        m_file = nullptr;
    }
}

void Logger::init(const config::DebugConfig& config, const char* log_path) {
    m_enabled = config.enabled;
    m_level = static_cast<LogLevel>(config.level);
    m_log_to_file = config.log_to_file;

    if (log_path != nullptr) {
        safe_strcpy(m_log_path, log_path, sizeof(m_log_path) - 1);
    } else {
        safe_strcpy(m_log_path, DEFAULT_LOG_PATH, sizeof(m_log_path) - 1);
    }

    // Initialize log buffer
    m_buffer.init(MAX_LOG_BUFFER_ENTRIES);

    // Open log file if enabled
    if (m_enabled && m_log_to_file) {
        // Close existing file if open
        if (m_file != nullptr) {
            std::fclose(static_cast<FILE*>(m_file));
        }

        // Open in append mode
        m_file = std::fopen(m_log_path, "a");

        if (m_file != nullptr) {
            // Write header
            std::fprintf(static_cast<FILE*>(m_file),
                         "\n=== ryu_ldn_nx Log Started ===\n");
            std::fflush(static_cast<FILE*>(m_file));
        }
    }

    // Log initialization
    if (m_enabled) {
        log(LogLevel::Info, "Logger initialized (level=%u, file=%s)",
            static_cast<uint32_t>(m_level),
            m_log_to_file ? "enabled" : "disabled");
    }
}

bool Logger::should_log(LogLevel level) const {
    if (!m_enabled) return false;
    return static_cast<uint32_t>(level) <= static_cast<uint32_t>(m_level);
}

void Logger::log(LogLevel level, const char* format, ...) {
    if (!should_log(level)) return;

    va_list args;
    va_start(args, format);
    log_v(level, format, args);
    va_end(args);
}

void Logger::log_v(LogLevel level, const char* format, va_list args) {
    if (!should_log(level)) return;

    char message[MAX_LOG_MESSAGE_LENGTH];
    format_log_message_v(message, sizeof(message), level, format, args);

    output_message(message);
}

void Logger::flush() {
    if (m_file != nullptr) {
        std::fflush(static_cast<FILE*>(m_file));
    }
}

void Logger::output_message(const char* message) {
    // Add to circular buffer (for overlay display)
    m_buffer.add(message);

    // Output to console (printf on Switch goes to debug console)
    std::printf("%s\n", message);

    // Output to file if enabled
    if (m_log_to_file && m_file != nullptr) {
        std::fprintf(static_cast<FILE*>(m_file), "%s\n", message);
        // Flush after each message to ensure it's written in case of crash
        std::fflush(static_cast<FILE*>(m_file));
    }
}

} // namespace ryu_ldn::debug
