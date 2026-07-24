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
#include <cerrno>

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
 * @brief Parse boolean value (strict: "true"/"false"/"1"/"0", case-insensitive)
 *
 * Returns false for any value that is not one of the four accepted forms.
 * The previous implementation accepted "f/F/n/N" prefixes and defaulted to
 * true for anything else, which silently turned typos and garbage into
 * "enabled" — a dangerous default for security-relevant flags such as
 * `disable_p2p` or `use_passphrase`.
 *
 * @param value Null-terminated string to parse (may be nullptr)
 * @return parsed boolean; false if the value is unrecognized or nullptr
 */
bool parse_bool(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return false;
    }

    // Accept only "true"/"false"/"1"/"0" (case-insensitive). We do a small
    // inline case-insensitive compare to stay portable across devkitPro
    // (Switch) and the host g++ test build without pulling in <strings.h>.
    auto ieq = [](char a, char b) {
        // ASCII-only lowercasing; fine for the literal tokens "true"/"false".
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        return a == b;
    };
    auto ieqstr = [&](const char* a, const char* b) {
        while (*a && *b) {
            if (!ieq(*a, *b)) return false;
            a++; b++;
        }
        return *a == '\0' && *b == '\0';
    };

    if (std::strcmp(value, "1") == 0 || ieqstr(value, "true")) {
        return true;
    }
    if (std::strcmp(value, "0") == 0 || ieqstr(value, "false")) {
        return false;
    }
    // Anything else (yes, no, on, off, True-ish, 2, ...) is a parse error.
    // Defaulting to false is the safe choice: it never silently enables a
    // feature the user did not intend to enable.
    return false;
}

/**
 * @brief Parse unsigned 32-bit integer from a config value string.
 *
 * @note Errors are handled by returning 0 (the same default-on-failure
 *       convention used by parse_bool in this file): a null/empty input or
 *       a leading '-' (negative value) is rejected and the function returns
 *       0 instead of letting strtoul silently wrap "-1" into ULONG_MAX and
 *       then truncate to UINT32_MAX. This prevents a config typo like
 *       `port = -1` from turning into 65535 or `level = -5` from becoming
 *       UINT32_MAX.
 *
 *       In addition, the parse now rejects trailing garbage and overflow:
 *       - `"123abc"` previously parsed as `123` (strtoul stops at the first
 *         non-digit and silently drops the rest). It now returns 0 because
 *         the whole input is not a clean integer.
 *       - `"4294967296"` (2^32) previously wrapped/truncated silently. It
 *         now returns 0 because strtoul sets errno=ERANGE and the result
 *         does not fit in uint32_t.
 *       This makes malformed config values loud (default applied) rather
 *       than quietly producing a wrong-but-plausible number.
 */
uint32_t parse_uint32(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    // Reject negative input. strtoul would happily accept "-1" and return
    // ULONG_MAX (wrapped), which is never what a config value means here.
    if (value[0] == '-') {
        return 0;
    }

    // Use endptr to detect trailing characters and errno to detect overflow.
    // strtoul skips leading whitespace itself; trailing whitespace is also
    // rejected here so that "123 " is treated as malformed (the INI parser
    // already trims values before calling us, so any trailing whitespace
    // would indicate a parser bug rather than a user intent).
    errno = 0;
    char* endptr = nullptr;
    unsigned long parsed = std::strtoul(value, &endptr, 10);

    // Overflow: strtoul returned ULONG_MAX and set errno. For platforms
    // where unsigned long is 64-bit, also reject values that exceed the
    // uint32_t range even without ERANGE (defensive: ULONG_MAX on a 32-bit
    // unsigned long triggers ERANGE, but on a 64-bit unsigned long only
    // values >= 2^64 set ERANGE — a value like 0x1_0000_0000 fits in
    // unsigned long but not in uint32_t).
    if (errno == ERANGE || parsed > 0xFFFFFFFFUL) {
        errno = 0;
        return 0;
    }

    // Trailing garbage: the whole input must be consumed. If endptr does
    // not point at the terminating NUL, characters such as "abc" in
    // "123abc" were left unparsed.
    if (endptr == value || *endptr != '\0') {
        return 0;
    }

    errno = 0;
    return static_cast<uint32_t>(parsed);
}

// Section identifiers
enum class Section {
    None,
    Server,
    Ldn,
    Debug,
    Unknown
};

/**
 * @brief Identify section from header line
 */
Section parse_section(const char* line) {
    if (std::strcmp(line, "[server]") == 0) return Section::Server;
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
        // Inlined parse_uint16 (single call site — LINT-12).
        // Same error convention as parse_uint32: null/empty or negative
        // input returns 0. Guards `port = -1` from wrapping to 65535.
        if (value != nullptr && value[0] != '\0' && value[0] != '-') {
            config.port = static_cast<uint16_t>(std::strtoul(value, nullptr, 10));
        } else {
            config.port = 0;
        }
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
    } else if (std::strcmp(key, "use_passphrase") == 0) {
        config.use_passphrase = parse_bool(value);
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
    }
}

#ifdef __SWITCH__
/**
 * @brief Parse file content line by line (for ams::fs buffer-based reading)
 */
/// @gdb{tag="CONFIG:PARSE", msg="Parsing config content"}
void parse_config_content(const char* content, size_t size, Config& config) {
    char line[512];
    Section current_section = Section::None;
    size_t line_pos = 0;

    size_t i = 0;
    while (i <= size) {
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
        i++;
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
    WRITE_LINE("");

    WRITE_LINE("");

    WRITE_LINE("[ldn]");
    WRITE_LINE("; Enable LDN emulation (0/1)");
    WRITE_LINE("enabled = %d", config.ldn.enabled ? 1 : 0);
    WRITE_LINE("; Enable passphrase filtering (0/1)");
    WRITE_LINE("use_passphrase = %d", config.ldn.use_passphrase ? 1 : 0);
    WRITE_LINE("; Room passphrase (empty = public)");
    WRITE_LINE("passphrase = %s", config.ldn.passphrase);
    WRITE_LINE("; Disable P2P proxy (0/1) - like Ryujinx MultiplayerDisableP2p");
    WRITE_LINE("disable_p2p = %d", config.ldn.disable_p2p ? 1 : 0);
    WRITE_LINE("");

    WRITE_LINE("[debug]");
    WRITE_LINE("; Enable debug logging (0/1)");
    WRITE_LINE("enabled = %d", config.debug.enabled ? 1 : 0);
    WRITE_LINE("; Log level (0=errors, 1=warnings, 2=info, 3=verbose)");
    WRITE_LINE("level = %u", config.debug.level);

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

    // LDN defaults
    config.ldn.enabled = DEFAULT_LDN_ENABLED;
    config.ldn.passphrase[0] = '\0';
    config.ldn.use_passphrase = DEFAULT_USE_PASSPHRASE;
    config.ldn.disable_p2p = DEFAULT_DISABLE_P2P;

    // Debug defaults
    config.debug.enabled = DEFAULT_DEBUG_ENABLED;
    config.debug.level = DEFAULT_DEBUG_LEVEL;

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

    // Atomic write via temp file + rename to avoid TOCTOU window where the
    // config file is missing between DeleteFile and CreateFile succeeds.
    // If any intermediate step fails, the original (if any) is left untouched.
    char tmp_path[256];
    // Reserve room for the ".tmp" suffix (4 chars) so strcat cannot overflow.
    safe_strcpy(tmp_path, path, sizeof(tmp_path) - 1 - 4);
    std::strcat(tmp_path, ".tmp");

    // If a stale temp file from a previous failed run exists, delete it so
    // CreateFile does not fail with FileExists.
    ams::fs::DirectoryEntryType tmp_entry_type;
    if (R_SUCCEEDED(ams::fs::GetEntryType(&tmp_entry_type, tmp_path))) {
        ams::fs::DeleteFile(tmp_path);
    }

    // Create temp file
    if (R_FAILED(ams::fs::CreateFile(tmp_path, content_size))) {
        delete[] buffer;
        return ConfigResult::IoError;
    }

    // Open temp file for writing
    ams::fs::FileHandle tmp_file;
    if (R_FAILED(ams::fs::OpenFile(&tmp_file, tmp_path, ams::fs::OpenMode_Write))) {
        ams::fs::DeleteFile(tmp_path);
        delete[] buffer;
        return ConfigResult::IoError;
    }

    // Write content
    ams::Result write_result = ams::fs::WriteFile(tmp_file, 0, buffer, content_size, ams::fs::WriteOption::Flush);
    ams::fs::CloseFile(tmp_file);

    delete[] buffer;

    if (R_FAILED(write_result)) {
        ams::fs::DeleteFile(tmp_path);
        return ConfigResult::IoError;
    }

    // Atomically replace the original file. RenameFile overwrites the
    // destination on the SD card filesystem (FAT-like semantics).
    if (R_FAILED(ams::fs::RenameFile(tmp_path, path))) {
        ams::fs::DeleteFile(tmp_path);
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

    // The Switch SD card uses FAT32/exFAT which has no POSIX permission
    // model — fopen("w") creates with default FAT attributes and chmod is
    // a no-op on this filesystem.  // codeql[cpp/world-writable-file-creation]
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


    std::fprintf(file, "[ldn]\n");
    std::fprintf(file, "; Enable LDN emulation (0/1)\n");
    std::fprintf(file, "enabled = %d\n", config.ldn.enabled ? 1 : 0);
    std::fprintf(file, "; Enable passphrase filtering (0/1)\n");
    std::fprintf(file, "use_passphrase = %d\n", config.ldn.use_passphrase ? 1 : 0);
    std::fprintf(file, "; Room passphrase (empty = public)\n");
    std::fprintf(file, "passphrase = %s\n", config.ldn.passphrase);
    std::fprintf(file, "; Disable P2P proxy (0/1) - like Ryujinx MultiplayerDisableP2p\n");
    std::fprintf(file, "disable_p2p = %d\n\n", config.ldn.disable_p2p ? 1 : 0);

    std::fprintf(file, "[debug]\n");
    std::fprintf(file, "; Enable debug logging (0/1)\n");
    std::fprintf(file, "enabled = %d\n", config.debug.enabled ? 1 : 0);
    std::fprintf(file, "; Log level (0=errors, 1=warnings, 2=info, 3=verbose)\n");
    std::fprintf(file, "level = %u\n", config.debug.level);

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
