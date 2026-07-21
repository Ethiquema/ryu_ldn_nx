/**
 * @file config.hpp
 * @brief Configuration Manager for ryu_ldn_nx
 *
 * This module handles loading and parsing of INI configuration files.
 * It provides all runtime settings for the sysmodule including server
 * connection details, LDN settings, and debug options.
 *
 * ## Design Principles
 *
 * 1. **No Dynamic Allocation**: All strings use fixed-size buffers suitable
 *    for embedded/sysmodule use on Nintendo Switch.
 *
 * 2. **Safe Defaults**: If config file is missing or malformed, sensible
 *    defaults are used so the sysmodule can still function.
 *
 * 3. **Simple INI Format**: Standard INI syntax with [sections] and key=value
 *    pairs. Comments start with ; or #.
 *
 * ## Configuration File Location
 *
 * On Nintendo Switch: `/config/ryu_ldn_nx/config.ini`
 *
 * ## INI File Format
 *
 * ```ini
 * ; Comment line
 * [section]
 * key = value
 * another_key = another value
 * ```
 *
 * ## Supported Sections
 *
 * - `[server]`: Server hostname and port
 * - `[ldn]`: LDN enable/disable, passphrase
 * - `[debug]`: Logging configuration
 *
 * ## Usage Example
 *
 * @code
 * #include "config/config.hpp"
 *
 * using namespace ryu_ldn::config;
 *
 * // Get defaults first
 * Config config = get_default_config();
 *
 * // Try to load from file (keeps defaults if file missing)
 * ConfigResult result = load_config("/config/ryu_ldn_nx/config.ini", config);
 *
 * if (result == ConfigResult::Success) {
 *     printf("Loaded config, server: %s:%d\n", config.server.host, config.server.port);
 * } else if (result == ConfigResult::FileNotFound) {
 *     printf("Using default config\n");
 * }
 * @endcode
 *
 * @see config/ryu_ldn_nx/config.ini.example for full configuration reference
 * @see Epic 2, Story 2.1 for requirements
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace ryu_ldn::config {

// =============================================================================
// Constants
// =============================================================================

/**
 * @brief Maximum length of server hostname/IP (excluding null terminator)
 *
 * 128 characters is sufficient for domain names (max 253 chars in DNS,
 * but practical limit is much lower) and IPv4/IPv6 addresses.
 */
constexpr size_t MAX_HOST_LENGTH = 128;

/**
 * @brief Maximum length of room passphrase (excluding null terminator)
 *
 * Matches the protocol's PassphraseMessage limit of 64 bytes.
 */
constexpr size_t MAX_PASSPHRASE_LENGTH = 64;

/**
 * @brief Default configuration file path on SD card
 *
 * This is the standard location for ryu_ldn_nx config.
 * The "sdmc:" prefix refers to the mounted SD card in Atmosphere.
 */
constexpr const char* CONFIG_PATH = "sdmc:/config/ryu_ldn_nx/config.ini";

/**
 * @brief Configuration directory path on SD card
 */
constexpr const char* CONFIG_DIR = "sdmc:/config/ryu_ldn_nx";

/**
 * @brief Log file path on SD card
 *
 * Debug logs are written here when debug logging is enabled.
 */
constexpr const char* LOG_PATH = "sdmc:/config/ryu_ldn_nx/ryu_ldn_nx.log";

// -----------------------------------------------------------------------------
// Default Values - Server
// -----------------------------------------------------------------------------

/** @brief Default server hostname (Temporary private RyuLDN server for development and testing) */
constexpr const char* DEFAULT_HOST = "ryuldnnx.ddns.net";

/** @brief Default server port */
constexpr uint16_t DEFAULT_PORT = 30456;

// -----------------------------------------------------------------------------
// Default Values - LDN
// -----------------------------------------------------------------------------

/** @brief Default LDN enabled state */
constexpr bool DEFAULT_LDN_ENABLED = true;
/** @brief Default use_passphrase state (false = public rooms, no filtering) */
constexpr bool DEFAULT_USE_PASSPHRASE = false;

/** @brief Default P2P proxy disabled state (matches config.ini.example: disable_p2p = 1) */
constexpr bool DEFAULT_DISABLE_P2P = true;

// -----------------------------------------------------------------------------
// Default Values - Debug
// -----------------------------------------------------------------------------

/** @brief Default debug logging state */
constexpr bool DEFAULT_DEBUG_ENABLED = false;

/** @brief Default debug log level (1 = warnings) */
constexpr uint32_t DEFAULT_DEBUG_LEVEL = 1;


// =============================================================================
// Result Codes
// =============================================================================

/**
 * @brief Result codes for configuration operations
 */
enum class ConfigResult {
    Success = 0,       ///< Configuration loaded successfully
    FileNotFound,      ///< Configuration file does not exist
    ParseError,        ///< File exists but contains syntax errors
    IoError            ///< File I/O error (permissions, disk full, etc.)
};

// =============================================================================
// Configuration Structures
// =============================================================================

/**
 * @brief Server connection settings
 *
 * Configuration for connecting to the ryu_ldn server.
 * Corresponds to the [server] section in config.ini.
 *
 * ## INI Keys
 * - `host`: Server hostname or IP address
 * - `port`: Server port number
 */
struct ServerConfig {
    char host[MAX_HOST_LENGTH + 1];  ///< Server hostname/IP (null-terminated)
    uint16_t port;                    ///< Server port number
};

/**
 * @brief LDN emulation settings
 *
 * Configuration for LDN (Local Wireless) emulation behavior.
 * Corresponds to the [ldn] section in config.ini.
 *
 * ## INI Keys
 * - `enabled`: Enable/disable LDN emulation (0/1)
 * - `passphrase`: Passphrase for private rooms (max 64 chars)
 * - `use_passphrase`: Enable passphrase filtering (0/1) - false = public rooms
 * - `disable_p2p`: Disable P2P proxy (0/1) - like Ryujinx MultiplayerDisableP2p
 */
struct LdnConfig {
    bool enabled;                                    ///< Enable LDN emulation
    char passphrase[MAX_PASSPHRASE_LENGTH + 1];      ///< Room passphrase (null-terminated)
    bool use_passphrase;                             ///< Enable passphrase filtering (false = public rooms)
    bool disable_p2p;                                ///< Disable P2P proxy (like Ryujinx)
};

/**
 * @brief Debug and logging settings
 *
 * Configuration for debugging and logging behavior.
 * Corresponds to the [debug] section in config.ini.
 *
 * ## INI Keys
 * - `enabled`: Enable debug logging (0/1)
 * - `level`: Log verbosity (0=errors, 1=warnings, 2=info, 3=verbose)
 *
 * When debug logging is enabled, logs are written to both console
 * and the log file on the SD card. No separate log_to_file flag
 * is needed — enabling debug implies file logging.
 *
 * ## Log Levels
 * - 0: Errors only (critical issues)
 * - 1: Warnings (potential problems)
 * - 2: Info (normal operation events)
 * - 3: Verbose (detailed debugging)
 */
struct DebugConfig {
    bool enabled;       ///< Enable debug logging (also enables file logging)
    uint32_t level;     ///< Log level (0-3)
};

/**
 * @brief Complete configuration
 *
 * Aggregates all configuration sections into a single structure.
 * Use get_default_config() to initialize with defaults, then
 * load_config() to override with file settings.
 */
struct Config {
    ServerConfig server;    ///< Server connection settings
    LdnConfig ldn;          ///< LDN emulation settings
    DebugConfig debug;      ///< Debug/logging settings
};

// =============================================================================
// Functions
// =============================================================================

/**
 * @brief Get configuration with all default values
 *
 * Returns a Config struct populated with sensible defaults.
 * Use this as a starting point before calling load_config().
 *
 * @return Config struct with default values on production release
 *
 * ## Default Values
 * - server.host: "ryuldnnx.ddns.net"
 * - server.port: 30456
 * - ldn.enabled: true
 * - ldn.passphrase: "" (empty)
 * - ldn.use_passphrase: false (public rooms)
 * - ldn.disable_p2p: false
 * - debug.enabled: false
 * - debug.level: 1 (warnings)
 *
 * ## Bool Parsing
 * Accepts: 0, f, F, n, N → false; anything else → true (1, true, yes, etc.)
 */
Config get_default_config();

/**
 * @brief Load configuration from INI file
 *
 * Parses an INI file and populates the config structure.
 * Unknown sections and keys are silently ignored.
 * If file doesn't exist, config is unchanged (use defaults).
 *
 * @param path Absolute path to configuration file
 * @param[in,out] config Configuration to populate (should be initialized first)
 * @return ConfigResult indicating success or failure type
 *
 * ## Typical Usage
 * @code
 * Config config = get_default_config();
 * load_config("/config/ryu_ldn_nx/config.ini", config);
 * // config now has file values, or defaults if file missing
 * @endcode
 *
 * ## Error Handling
 * - FileNotFound: File doesn't exist - config unchanged
 * - ParseError: File has syntax errors - partial config may be loaded
 * - IoError: Read error - config unchanged
 */
/// @gdb{tag="CONFIG:PARSE", msg="Parsing config content"}
ConfigResult load_config(const char* path, Config& config);

/**
 * @brief Save configuration to INI file
 *
 * Writes the config structure to an INI file.
 * Creates parent directories if they don't exist.
 *
 * @param path Absolute path to configuration file
 * @param config Configuration to save
 * @return ConfigResult indicating success or failure type
 */
ConfigResult save_config(const char* path, const Config& config);

/**
 * @brief Ensure configuration file exists, create with defaults if not
 *
 * Checks if config file exists. If not, creates it with default values.
 * This should be called on sysmodule startup.
 *
 * @param path Absolute path to configuration file
 * @return ConfigResult indicating success or failure type
 */
ConfigResult ensure_config_exists(const char* path);

/**
 * @brief Convert ConfigResult to human-readable string
 *
 * @param result ConfigResult enum value
 * @return Static string describing the result
 */
inline const char* config_result_to_string(ConfigResult result) {
    switch (result) {
        case ConfigResult::Success:      return "Success";
        case ConfigResult::FileNotFound: return "FileNotFound";
        case ConfigResult::ParseError:   return "ParseError";
        case ConfigResult::IoError:      return "IoError";
        default:                         return "Unknown";
    }
}

} // namespace ryu_ldn::config
