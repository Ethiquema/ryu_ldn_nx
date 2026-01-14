/**
 * @file config.cpp
 * @brief Configuration Manager Implementation
 *
 * On Nintendo Switch (Atmosphere), uses ams::fs API for safe SD card access
 * at boot. The standard library fopen/fclose causes kernel panic (DABRT 0x101)
 * when called before the filesystem is fully ready.
 *
 * For testing on PC, uses standard C file I/O.
 */

#include "config.hpp"
#include <cstring>
#include <cstdlib>
#include <cstdio>

#ifdef __SWITCH__
#include <stratosphere.hpp>
#else
#include <sys/stat.h>
#endif

namespace ryu_ldn::config {

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

/**
 * @brief Trim leading whitespace from string
 */
const char* trim_start(const char* str) {
    while (*str == ' ' || *str == '\t') {
        str++;
    }
    return str;
}

/**
 * @brief Trim trailing whitespace from string (modifies in place)
 */
void trim_end(char* str) {
    size_t len = std::strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' ||
                       str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[--len] = '\0';
    }
}

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
 * @brief Parse boolean value (0/1, true/false, yes/no)
 */
bool parse_bool(const char* value) {
    if (value[0] == '0' || value[0] == 'f' || value[0] == 'F' ||
        value[0] == 'n' || value[0] == 'N') {
        return false;
    }
    return true;  // Default to true for any non-false value
}

/**
 * @brief Parse unsigned integer
 */
uint32_t parse_uint32(const char* value) {
    return static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
}

/**
 * @brief Parse unsigned 16-bit integer
 */
uint16_t parse_uint16(const char* value) {
    return static_cast<uint16_t>(std::strtoul(value, nullptr, 10));
}

// Section identifiers
enum class Section {
    None,
    Server,
    Network,
    Ldn,
    Debug,
    Unknown
};

/**
 * @brief Identify section from header line
 */
Section parse_section(const char* line) {
    if (std::strcmp(line, "[server]") == 0) return Section::Server;
    if (std::strcmp(line, "[network]") == 0) return Section::Network;
    if (std::strcmp(line, "[ldn]") == 0) return Section::Ldn;
    if (std::strcmp(line, "[debug]") == 0) return Section::Debug;
    if (line[0] == '[') return Section::Unknown;
    return Section::None;
}

/**
 * @brief Process a key=value line for server section
 */
void process_server_key(const char* key, const char* value, ServerConfig& config) {
    if (std::strcmp(key, "host") == 0) {
        safe_strcpy(config.host, value, MAX_HOST_LENGTH);
    } else if (std::strcmp(key, "port") == 0) {
        config.port = parse_uint16(value);
    } else if (std::strcmp(key, "use_tls") == 0) {
        config.use_tls = parse_bool(value);
    }
}

/**
 * @brief Process a key=value line for network section
 */
void process_network_key(const char* key, const char* value, NetworkConfig& config) {
    if (std::strcmp(key, "connect_timeout") == 0) {
        config.connect_timeout_ms = parse_uint32(value);
    } else if (std::strcmp(key, "ping_interval") == 0) {
        config.ping_interval_ms = parse_uint32(value);
    } else if (std::strcmp(key, "reconnect_delay") == 0) {
        config.reconnect_delay_ms = parse_uint32(value);
    } else if (std::strcmp(key, "max_reconnect_attempts") == 0) {
        config.max_reconnect_attempts = parse_uint32(value);
    }
}

/**
 * @brief Process a key=value line for ldn section
 */
void process_ldn_key(const char* key, const char* value, LdnConfig& config) {
    if (std::strcmp(key, "enabled") == 0) {
        config.enabled = parse_bool(value);
    } else if (std::strcmp(key, "passphrase") == 0) {
        safe_strcpy(config.passphrase, value, MAX_PASSPHRASE_LENGTH);
    } else if (std::strcmp(key, "interface") == 0) {
        safe_strcpy(config.interface_name, value, MAX_INTERFACE_LENGTH);
    } else if (std::strcmp(key, "disable_p2p") == 0) {
        config.disable_p2p = parse_bool(value);
    }
}

/**
 * @brief Process a key=value line for debug section
 */
void process_debug_key(const char* key, const char* value, DebugConfig& config) {
    if (std::strcmp(key, "enabled") == 0) {
        config.enabled = parse_bool(value);
    } else if (std::strcmp(key, "level") == 0) {
        config.level = parse_uint32(value);
    } else if (std::strcmp(key, "log_to_file") == 0) {
        config.log_to_file = parse_bool(value);
    }
}

#ifdef __SWITCH__
/**
 * @brief Parse file content line by line (for ams::fs buffer-based reading)
 */
void parse_config_content(const char* content, size_t size, Config& config) {
    char line[512];
    Section current_section = Section::None;
    size_t line_pos = 0;

    for (size_t i = 0; i <= size; i++) {
        // End of line or end of content
        if (i == size || content[i] == '\n' || content[i] == '\r') {
            line[line_pos] = '\0';

            if (line_pos > 0) {
                // Remove trailing whitespace/newlines
                trim_end(line);

                // Skip empty lines
                const char* trimmed = trim_start(line);
                if (trimmed[0] != '\0') {
                    // Skip comments
                    if (trimmed[0] != ';' && trimmed[0] != '#') {
                        // Check for section header
                        Section new_section = parse_section(trimmed);
                        if (new_section != Section::None) {
                            current_section = new_section;
                        } else if (current_section != Section::None &&
                                   current_section != Section::Unknown) {
                            // Parse key=value
                            char* eq_pos = std::strchr(line, '=');
                            if (eq_pos) {
                                *eq_pos = '\0';
                                char* key = line;
                                char* value = eq_pos + 1;

                                // Trim key and value
                                const char* trimmed_key = trim_start(key);
                                trim_end(key);

                                const char* trimmed_value = trim_start(value);
                                trim_end(value);

                                // Copy trimmed key
                                char key_buf[64];
                                safe_strcpy(key_buf, trimmed_key, sizeof(key_buf) - 1);
                                trim_end(key_buf);

                                // Process based on current section
                                switch (current_section) {
                                    case Section::Server:
                                        process_server_key(key_buf, trimmed_value, config.server);
                                        break;
                                    case Section::Network:
                                        process_network_key(key_buf, trimmed_value, config.network);
                                        break;
                                    case Section::Ldn:
                                        process_ldn_key(key_buf, trimmed_value, config.ldn);
                                        break;
                                    case Section::Debug:
                                        process_debug_key(key_buf, trimmed_value, config.debug);
                                        break;
                                    default:
                                        break;
                                }
                            }
                        }
                    }
                }
            }

            line_pos = 0;
            // Skip \r if followed by \n
            if (i < size && content[i] == '\r' && i + 1 < size && content[i + 1] == '\n') {
                i++;
            }
        } else if (line_pos < sizeof(line) - 1) {
            line[line_pos++] = content[i];
        }
    }
}

/**
 * @brief Format config content into buffer for writing
 * @return Number of bytes written
 */
size_t format_config_content(char* buffer, size_t buffer_size, const Config& config) {
    size_t offset = 0;

    #define WRITE_LINE(fmt, ...) do { \
        int written = std::snprintf(buffer + offset, buffer_size - offset, fmt "\n", ##__VA_ARGS__); \
        if (written > 0 && offset + written < buffer_size) offset += written; \
    } while(0)

    WRITE_LINE("; ryu_ldn_nx Configuration");
    WRITE_LINE("; Auto-generated on first boot");
    WRITE_LINE("; Edit this file to customize settings");
    WRITE_LINE("");

    WRITE_LINE("[server]");
    WRITE_LINE("; Server hostname or IP address");
    WRITE_LINE("host = %s", config.server.host);
    WRITE_LINE("; Server port");
    WRITE_LINE("port = %u", config.server.port);
    WRITE_LINE("; Use TLS encryption (0/1)");
    WRITE_LINE("use_tls = %d", config.server.use_tls ? 1 : 0);
    WRITE_LINE("");

    WRITE_LINE("[network]");
    WRITE_LINE("; Connection timeout in milliseconds");
    WRITE_LINE("connect_timeout = %u", config.network.connect_timeout_ms);
    WRITE_LINE("; Ping interval in milliseconds");
    WRITE_LINE("ping_interval = %u", config.network.ping_interval_ms);
    WRITE_LINE("; Reconnect delay in milliseconds");
    WRITE_LINE("reconnect_delay = %u", config.network.reconnect_delay_ms);
    WRITE_LINE("; Max reconnect attempts (0 = infinite)");
    WRITE_LINE("max_reconnect_attempts = %u", config.network.max_reconnect_attempts);
    WRITE_LINE("");

    WRITE_LINE("[ldn]");
    WRITE_LINE("; Enable LDN emulation (0/1)");
    WRITE_LINE("enabled = %d", config.ldn.enabled ? 1 : 0);
    WRITE_LINE("; Room passphrase (empty = public)");
    WRITE_LINE("passphrase = %s", config.ldn.passphrase);
    WRITE_LINE("; Network interface (empty = auto)");
    WRITE_LINE("interface = %s", config.ldn.interface_name);
    WRITE_LINE("; Disable P2P proxy (0/1) - like Ryujinx MultiplayerDisableP2p");
    WRITE_LINE("disable_p2p = %d", config.ldn.disable_p2p ? 1 : 0);
    WRITE_LINE("");

    WRITE_LINE("[debug]");
    WRITE_LINE("; Enable debug logging (0/1)");
    WRITE_LINE("enabled = %d", config.debug.enabled ? 1 : 0);
    WRITE_LINE("; Log level (0=errors, 1=warnings, 2=info, 3=verbose)");
    WRITE_LINE("level = %u", config.debug.level);
    WRITE_LINE("; Log to file (0/1)");
    WRITE_LINE("log_to_file = %d", config.debug.log_to_file ? 1 : 0);

    #undef WRITE_LINE

    return offset;
}
#endif // __SWITCH__

} // anonymous namespace

// ============================================================================
// Public Functions
// ============================================================================

Config get_default_config() {
    Config config{};

    // Server defaults
    safe_strcpy(config.server.host, DEFAULT_HOST, MAX_HOST_LENGTH);
    config.server.port = DEFAULT_PORT;
    config.server.use_tls = DEFAULT_USE_TLS;

    // Network defaults
    config.network.connect_timeout_ms = DEFAULT_CONNECT_TIMEOUT_MS;
    config.network.ping_interval_ms = DEFAULT_PING_INTERVAL_MS;
    config.network.reconnect_delay_ms = DEFAULT_RECONNECT_DELAY_MS;
    config.network.max_reconnect_attempts = DEFAULT_MAX_RECONNECT_ATTEMPTS;

    // LDN defaults
    config.ldn.enabled = DEFAULT_LDN_ENABLED;
    config.ldn.passphrase[0] = '\0';
    config.ldn.interface_name[0] = '\0';
    config.ldn.disable_p2p = DEFAULT_DISABLE_P2P;

    // Debug defaults
    config.debug.enabled = DEFAULT_DEBUG_ENABLED;
    config.debug.level = DEFAULT_DEBUG_LEVEL;
    config.debug.log_to_file = DEFAULT_LOG_TO_FILE;

    return config;
}

#ifdef __SWITCH__
// =============================================================================
// Nintendo Switch / Atmosphere Implementation
// Uses ams::fs API to avoid kernel panic at boot
// =============================================================================

ConfigResult load_config(const char* path, Config& config) {
    // Check if file exists using ams::fs
    ams::fs::DirectoryEntryType entry_type;
    if (R_FAILED(ams::fs::GetEntryType(&entry_type, path))) {
        return ConfigResult::FileNotFound;
    }

    if (entry_type != ams::fs::DirectoryEntryType_File) {
        return ConfigResult::FileNotFound;
    }

    // Open file for reading
    ams::fs::FileHandle file;
    if (R_FAILED(ams::fs::OpenFile(&file, path, ams::fs::OpenMode_Read))) {
        return ConfigResult::IoError;
    }

    // Get file size
    s64 file_size;
    if (R_FAILED(ams::fs::GetFileSize(&file_size, file))) {
        ams::fs::CloseFile(file);
        return ConfigResult::IoError;
    }

    // Sanity check on file size (max 64KB)
    if (file_size <= 0 || file_size > 65536) {
        ams::fs::CloseFile(file);
        if (file_size == 0) {
            return ConfigResult::FileNotFound;
        }
        return ConfigResult::ParseError;
    }

    // Allocate buffer and read file
    char* content = new (std::nothrow) char[file_size + 1];
    if (!content) {
        ams::fs::CloseFile(file);
        return ConfigResult::IoError;
    }

    size_t bytes_read;
    ams::Result read_result = ams::fs::ReadFile(&bytes_read, file, 0, content, static_cast<size_t>(file_size));
    ams::fs::CloseFile(file);

    if (R_FAILED(read_result)) {
        delete[] content;
        return ConfigResult::IoError;
    }

    content[bytes_read] = '\0';

    // Parse content
    parse_config_content(content, bytes_read, config);

    delete[] content;
    return ConfigResult::Success;
}

ConfigResult save_config(const char* path, const Config& config) {
    // Ensure parent directory exists
    char dir_path[256];
    safe_strcpy(dir_path, path, sizeof(dir_path) - 1);

    char* last_slash = std::strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        // Use ams::fs::EnsureDirectory which creates recursively
        ams::fs::EnsureDirectory(dir_path);
    }

    // Format config content
    constexpr size_t buffer_size = 4096;
    char* buffer = new (std::nothrow) char[buffer_size];
    if (!buffer) {
        return ConfigResult::IoError;
    }

    size_t content_size = format_config_content(buffer, buffer_size, config);

    // Delete existing file if present
    ams::fs::DirectoryEntryType entry_type;
    if (R_SUCCEEDED(ams::fs::GetEntryType(&entry_type, path))) {
        ams::fs::DeleteFile(path);
    }

    // Create new file
    if (R_FAILED(ams::fs::CreateFile(path, content_size))) {
        delete[] buffer;
        return ConfigResult::IoError;
    }

    // Open file for writing
    ams::fs::FileHandle file;
    if (R_FAILED(ams::fs::OpenFile(&file, path, ams::fs::OpenMode_Write))) {
        delete[] buffer;
        return ConfigResult::IoError;
    }

    // Write content
    ams::Result write_result = ams::fs::WriteFile(file, 0, buffer, content_size, ams::fs::WriteOption::Flush);
    ams::fs::CloseFile(file);

    delete[] buffer;

    if (R_FAILED(write_result)) {
        return ConfigResult::IoError;
    }

    return ConfigResult::Success;
}

ConfigResult ensure_config_exists(const char* path) {
    // Check if file exists using ams::fs
    ams::fs::DirectoryEntryType entry_type;
    if (R_SUCCEEDED(ams::fs::GetEntryType(&entry_type, path))) {
        if (entry_type == ams::fs::DirectoryEntryType_File) {
            return ConfigResult::Success;  // File already exists
        }
    }

    // File doesn't exist, create with defaults
    Config default_config = get_default_config();
    return save_config(path, default_config);
}

#else
// =============================================================================
// PC/Test Implementation
// Uses standard C file I/O for testing on desktop platforms
// =============================================================================

ConfigResult load_config(const char* path, Config& config) {
    FILE* file = std::fopen(path, "r");
    if (!file) {
        return ConfigResult::FileNotFound;
    }

    char line[512];
    Section current_section = Section::None;

    while (std::fgets(line, sizeof(line), file)) {
        // Remove trailing whitespace/newlines
        trim_end(line);

        // Skip empty lines
        const char* trimmed = trim_start(line);
        if (trimmed[0] == '\0') {
            continue;
        }

        // Skip comments
        if (trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }

        // Check for section header
        Section new_section = parse_section(trimmed);
        if (new_section != Section::None) {
            current_section = new_section;
            continue;
        }

        // Skip if in unknown section
        if (current_section == Section::None || current_section == Section::Unknown) {
            continue;
        }

        // Parse key=value
        char* eq_pos = std::strchr(line, '=');
        if (!eq_pos) {
            continue;  // No '=' found, skip line
        }

        // Split into key and value
        *eq_pos = '\0';
        char* key = line;
        char* value = eq_pos + 1;

        // Trim key and value
        const char* trimmed_key = trim_start(key);
        trim_end(key);

        const char* trimmed_value = trim_start(value);
        trim_end(value);

        // Copy trimmed key (need mutable copy for trim_end)
        char key_buf[64];
        safe_strcpy(key_buf, trimmed_key, sizeof(key_buf) - 1);
        trim_end(key_buf);

        // Process based on current section
        switch (current_section) {
            case Section::Server:
                process_server_key(key_buf, trimmed_value, config.server);
                break;
            case Section::Network:
                process_network_key(key_buf, trimmed_value, config.network);
                break;
            case Section::Ldn:
                process_ldn_key(key_buf, trimmed_value, config.ldn);
                break;
            case Section::Debug:
                process_debug_key(key_buf, trimmed_value, config.debug);
                break;
            default:
                break;
        }
    }

    std::fclose(file);
    return ConfigResult::Success;
}

ConfigResult save_config(const char* path, const Config& config) {
    // Create parent directory if needed
    // Extract directory path from file path
    char dir_path[256];
    safe_strcpy(dir_path, path, sizeof(dir_path) - 1);

    char* last_slash = std::strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        // Create directory (mkdir -p equivalent)
        mkdir(dir_path, 0755);
    }

    FILE* file = std::fopen(path, "w");
    if (!file) {
        return ConfigResult::IoError;
    }

    std::fprintf(file, "; ryu_ldn_nx Configuration\n");
    std::fprintf(file, "; Auto-generated on first boot\n");
    std::fprintf(file, "; Edit this file to customize settings\n\n");

    std::fprintf(file, "[server]\n");
    std::fprintf(file, "; Server hostname or IP address\n");
    std::fprintf(file, "host = %s\n", config.server.host);
    std::fprintf(file, "; Server port\n");
    std::fprintf(file, "port = %u\n", config.server.port);
    std::fprintf(file, "; Use TLS encryption (0/1)\n");
    std::fprintf(file, "use_tls = %d\n\n", config.server.use_tls ? 1 : 0);

    std::fprintf(file, "[network]\n");
    std::fprintf(file, "; Connection timeout in milliseconds\n");
    std::fprintf(file, "connect_timeout = %u\n", config.network.connect_timeout_ms);
    std::fprintf(file, "; Ping interval in milliseconds\n");
    std::fprintf(file, "ping_interval = %u\n", config.network.ping_interval_ms);
    std::fprintf(file, "; Reconnect delay in milliseconds\n");
    std::fprintf(file, "reconnect_delay = %u\n", config.network.reconnect_delay_ms);
    std::fprintf(file, "; Max reconnect attempts (0 = infinite)\n");
    std::fprintf(file, "max_reconnect_attempts = %u\n\n", config.network.max_reconnect_attempts);

    std::fprintf(file, "[ldn]\n");
    std::fprintf(file, "; Enable LDN emulation (0/1)\n");
    std::fprintf(file, "enabled = %d\n", config.ldn.enabled ? 1 : 0);
    std::fprintf(file, "; Room passphrase (empty = public)\n");
    std::fprintf(file, "passphrase = %s\n", config.ldn.passphrase);
    std::fprintf(file, "; Network interface (empty = auto)\n");
    std::fprintf(file, "interface = %s\n", config.ldn.interface_name);
    std::fprintf(file, "; Disable P2P proxy (0/1) - like Ryujinx MultiplayerDisableP2p\n");
    std::fprintf(file, "disable_p2p = %d\n\n", config.ldn.disable_p2p ? 1 : 0);

    std::fprintf(file, "[debug]\n");
    std::fprintf(file, "; Enable debug logging (0/1)\n");
    std::fprintf(file, "enabled = %d\n", config.debug.enabled ? 1 : 0);
    std::fprintf(file, "; Log level (0=errors, 1=warnings, 2=info, 3=verbose)\n");
    std::fprintf(file, "level = %u\n", config.debug.level);
    std::fprintf(file, "; Log to file (0/1)\n");
    std::fprintf(file, "log_to_file = %d\n", config.debug.log_to_file ? 1 : 0);

    std::fclose(file);
    return ConfigResult::Success;
}

ConfigResult ensure_config_exists(const char* path) {
    // Try to open file to check if it exists
    FILE* file = std::fopen(path, "r");
    if (file) {
        std::fclose(file);
        return ConfigResult::Success;  // File already exists
    }

    // File doesn't exist, create with defaults
    Config default_config = get_default_config();
    return save_config(path, default_config);
}

#endif // __SWITCH__

} // namespace ryu_ldn::config
