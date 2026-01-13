/**
 * @file config.hpp
 * @brief Configuration Manager for ryu_ldn_nx
 *
 * This module handles loading and parsing of INI configuration files.
 * It provides all runtime settings for the sysmodule including server
 * connection details, network timeouts, and debug options.
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
 * - `[server]`: Server hostname, port, TLS settings
 * - `[network]`: Timeouts, reconnect behavior
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
 * @brief Maximum length of network interface name (excluding null terminator)
 *
 * Linux interface names are typically max 15 chars (IFNAMSIZ).
 */
constexpr size_t MAX_INTERFACE_LENGTH = 32;

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

// -----------------------------------------------------------------------------
// Default Values - Server
// -----------------------------------------------------------------------------

/** @brief Default server hostname (official Ryujinx LDN server) */
constexpr const char* DEFAULT_HOST = "ldn.ryujinx.app";

/** @brief Default server port */
constexpr uint16_t DEFAULT_PORT = 30456;

/** @brief Default TLS setting (recommended for security) */
constexpr bool DEFAULT_USE_TLS = true;

// -----------------------------------------------------------------------------
// Default Values - Network
// -----------------------------------------------------------------------------

/** @brief Default connection timeout (5 seconds) */
constexpr uint32_t DEFAULT_CONNECT_TIMEOUT_MS = 5000;

/** @brief Default ping/keepalive interval (10 seconds) */
constexpr uint32_t DEFAULT_PING_INTERVAL_MS = 10000;

/** @brief Default initial reconnect delay (3 seconds) */
constexpr uint32_t DEFAULT_RECONNECT_DELAY_MS = 3000;

/** @brief Default maximum reconnection attempts (0 = infinite) */
constexpr uint32_t DEFAULT_MAX_RECONNECT_ATTEMPTS = 5;

// -----------------------------------------------------------------------------
// Default Values - LDN
// -----------------------------------------------------------------------------

/** @brief Default LDN enabled state */
constexpr bool DEFAULT_LDN_ENABLED = true;

// -----------------------------------------------------------------------------
// Default Values - Debug
// -----------------------------------------------------------------------------

/** @brief Default debug logging state */
constexpr bool DEFAULT_DEBUG_ENABLED = false;

/** @brief Default debug log level (1 = warnings) */
constexpr uint32_t DEFAULT_DEBUG_LEVEL = 1;

/** @brief Default file logging state */
constexpr bool DEFAULT_LOG_TO_FILE = false;

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
 * - `use_tls`: Enable TLS encryption (0/1)
 */
struct ServerConfig {
    char host[MAX_HOST_LENGTH + 1];  ///< Server hostname/IP (null-terminated)
    uint16_t port;                    ///< Server port number
    bool use_tls;                     ///< Use TLS/SSL encryption
};

/**
 * @brief Network behavior settings
 *
 * Configuration for network timeouts and reconnection behavior.
 * Corresponds to the [network] section in config.ini.
 *
 * ## INI Keys
 * - `connect_timeout`: Connection timeout in milliseconds
 * - `ping_interval`: Keepalive ping interval in milliseconds
 * - `reconnect_delay`: Initial delay before reconnection attempt
 * - `max_reconnect_attempts`: Maximum reconnect attempts (0 = infinite)
 */
struct NetworkConfig {
    uint32_t connect_timeout_ms;       ///< TCP connection timeout
    uint32_t ping_interval_ms;         ///< Keepalive ping interval
    uint32_t reconnect_delay_ms;       ///< Initial reconnect delay
    uint32_t max_reconnect_attempts;   ///< Max reconnect attempts (0 = infinite)
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
 * - `interface`: Preferred network interface (empty = auto)
 */
struct LdnConfig {
    bool enabled;                                    ///< Enable LDN emulation
    char passphrase[MAX_PASSPHRASE_LENGTH + 1];      ///< Room passphrase (null-terminated)
    char interface_name[MAX_INTERFACE_LENGTH + 1];   ///< Network interface (null-terminated)
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
 * - `log_to_file`: Also write logs to file (0/1)
 *
 * ## Log Levels
 * - 0: Errors only (critical issues)
 * - 1: Warnings (potential problems)
 * - 2: Info (normal operation events)
 * - 3: Verbose (detailed debugging)
 */
struct DebugConfig {
    bool enabled;       ///< Enable debug logging
    uint32_t level;     ///< Log level (0-3)
    bool log_to_file;   ///< Write logs to file
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
    NetworkConfig network;  ///< Network behavior settings
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
 * @return Config struct with default values
 *
 * ## Default Values
 * - server.host: "ldn.ryujinx.app"
 * - server.port: 30456
 * - server.use_tls: true
 * - network.connect_timeout_ms: 5000
 * - network.ping_interval_ms: 10000
 * - network.reconnect_delay_ms: 3000
 * - network.max_reconnect_attempts: 5
 * - ldn.enabled: true
 * - ldn.passphrase: "" (empty)
 * - debug.enabled: false
 * - debug.level: 1 (warnings)
 * - debug.log_to_file: false
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
