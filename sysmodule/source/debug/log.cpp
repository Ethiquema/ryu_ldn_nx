/**
 * @file log.cpp
 * @brief Debug Logging System Implementation
 *
 * On Nintendo Switch (Atmosphere), uses ams::fs API for safe SD card access
 * at boot. The standard library fopen/fclose causes kernel panic when called
 * before the filesystem is fully ready.
 */

#include "log.hpp"
#include "../config/config.hpp"
#include <cstdio>
#include <cstring>
#include <cstdarg>

#ifdef __SWITCH__
#include <stratosphere.hpp>
#endif

namespace ryu_ldn::debug {

// =============================================================================
// Global Logger Instance
// =============================================================================

Logger g_logger;

#ifdef __SWITCH__
// File handle stored here to avoid exposing ams::fs types in header
static ams::fs::FileHandle s_log_file_handle;
#endif

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
 */
void get_timestamp(char* buffer, size_t buffer_size) {
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
    close_file();
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

    // Close any existing file - file will be opened on-demand
    close_file();

    // Reset header flag - new session needs new header
    m_header_written = false;

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
#ifdef __SWITCH__
    if (m_file_open) {
        ams::fs::FlushFile(s_log_file_handle);
    }
#else
    if (m_file != nullptr) {
        std::fflush(static_cast<FILE*>(m_file));
    }
#endif
}

void Logger::output_message(const char* message) {
    // Add to circular buffer (for overlay display)
    m_buffer.add(message);

    // Output to console (printf on Switch goes to debug console)
    std::printf("%s\n", message);

    // Output to file if enabled
    if (m_log_to_file) {
        // Open file on-demand if not already open
        if (!m_file_open) {
            open_file();
        }

#ifdef __SWITCH__
        if (m_file_open) {
            size_t msg_len = std::strlen(message);
            char line[MAX_LOG_MESSAGE_LENGTH + 2];
            std::memcpy(line, message, msg_len);
            line[msg_len] = '\n';
            line[msg_len + 1] = '\0';

            ams::fs::WriteFile(s_log_file_handle, m_file_offset, line, msg_len + 1,
                               ams::fs::WriteOption::Flush);
            m_file_offset += msg_len + 1;

            // Update last write time
            m_last_write_tick = armGetSystemTick();
        }
#else
        if (m_file != nullptr) {
            std::fprintf(static_cast<FILE*>(m_file), "%s\n", message);
            std::fflush(static_cast<FILE*>(m_file));

            // Update last write time (simple incrementing counter for non-Switch)
            m_last_write_tick++;
        }
#endif
    }
}

void Logger::open_file() {
    if (m_file_open) return;

#ifdef __SWITCH__
    // Ensure parent directory exists
    char dir_path[256];
    safe_strcpy(dir_path, m_log_path, sizeof(dir_path) - 1);
    char* last_slash = std::strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        ams::fs::EnsureDirectory(dir_path);
    }

    // Check if file exists
    ams::fs::DirectoryEntryType entry_type;
    bool file_exists = R_SUCCEEDED(ams::fs::GetEntryType(&entry_type, m_log_path));

    if (!file_exists) {
        // Create new file
        if (R_SUCCEEDED(ams::fs::CreateFile(m_log_path, 0))) {
            file_exists = true;
        }
    }

    // Open file for append
    if (file_exists) {
        if (R_SUCCEEDED(ams::fs::OpenFile(&s_log_file_handle, m_log_path,
                        ams::fs::OpenMode_Write | ams::fs::OpenMode_AllowAppend))) {
            m_file_open = true;

            // Get current file size for append offset
            s64 file_size;
            if (R_SUCCEEDED(ams::fs::GetFileSize(&file_size, s_log_file_handle))) {
                m_file_offset = static_cast<size_t>(file_size);
            }

            // Write header only once per session
            if (!m_header_written) {
                const char* header = "\n=== ryu_ldn_nx Log Started ===\n";
                size_t header_len = std::strlen(header);
                ams::fs::WriteFile(s_log_file_handle, m_file_offset, header, header_len,
                                   ams::fs::WriteOption::Flush);
                m_file_offset += header_len;
                m_header_written = true;
            }

            m_last_write_tick = armGetSystemTick();
        }
    }
#else
    // Open in append mode
    m_file = std::fopen(m_log_path, "a");

    if (m_file != nullptr) {
        m_file_open = true;

        // Write header only once per session
        if (!m_header_written) {
            std::fprintf(static_cast<FILE*>(m_file),
                         "\n=== ryu_ldn_nx Log Started ===\n");
            m_header_written = true;
        }
        std::fflush(static_cast<FILE*>(m_file));

        // Simple counter for non-Switch (timeout not critical for testing)
        m_last_write_tick++;
    }
#endif
}

void Logger::close_file() {
#ifdef __SWITCH__
    if (m_file_open) {
        ams::fs::FlushFile(s_log_file_handle);
        ams::fs::CloseFile(s_log_file_handle);
        m_file_open = false;
    }
#else
    if (m_file != nullptr) {
        std::fflush(static_cast<FILE*>(m_file));
        std::fclose(static_cast<FILE*>(m_file));
        m_file = nullptr;
        m_file_open = false;
    }
#endif
}

void Logger::check_idle_timeout() {
    if (!m_file_open) return;

#ifdef __SWITCH__
    uint64_t current_tick = armGetSystemTick();
    uint64_t elapsed_ns = armTicksToNs(current_tick - m_last_write_tick);

    if (elapsed_ns >= FILE_IDLE_TIMEOUT_NS) {
        close_file();
    }
#else
    // Non-Switch: always close after check (for testing simplicity)
    // Real timeout only matters on Switch hardware
    close_file();
#endif
}

} // namespace ryu_ldn::debug
