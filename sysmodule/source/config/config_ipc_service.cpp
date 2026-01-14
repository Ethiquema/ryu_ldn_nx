/**
 * @file config_ipc_service.cpp
 * @brief Standalone IPC service implementation for configuration (ryu:cfg)
 *
 * This file implements the ryu:cfg IPC service which allows the Tesla overlay
 * to communicate with the sysmodule independently of ldn:u MITM service.
 *
 * ## Architecture
 *
 * The ryu:cfg service is registered as a standalone service that runs alongside
 * the ldn:u MITM service. This allows:
 * - Overlay to always connect (even when no game is running)
 * - Configuration changes without requiring game restart
 * - Real-time status monitoring
 *
 * ## Thread Safety
 *
 * All configuration access is protected by g_config_mutex. The mutex is held
 * for the duration of each IPC call to ensure consistent reads/writes.
 *
 * ## IPC Protocol
 *
 * Commands are defined in config_ipc_service.hpp with the following conventions:
 * - Get* commands: Read configuration values (no side effects)
 * - Set* commands: Write configuration values (in-memory only until SaveConfig)
 * - SaveConfig: Persist current configuration to SD card
 * - ReloadConfig: Discard in-memory changes and reload from SD card
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "config_ipc_service.hpp"
#include "config.hpp"
#include "../debug/log.hpp"
#include "../ldn/ldn_shared_state.hpp"
#include <cstring>

namespace ryu_ldn::ipc {

// =============================================================================
// Global Configuration State
// =============================================================================

/**
 * @brief Global configuration instance shared between MITM and IPC services
 *
 * This configuration is loaded once at startup and can be modified via IPC.
 * Changes are only persisted when SaveConfig is called.
 */
config::Config g_config;

/**
 * @brief Mutex protecting g_config from concurrent access
 *
 * Must be held when reading or writing any field of g_config.
 */
ams::os::SdkMutex g_config_mutex;

/**
 * @brief Initialize global configuration from file
 *
 * Called once during sysmodule startup. Loads defaults first, then overwrites
 * with values from config.ini if it exists.
 *
 * Thread-safe: Acquires g_config_mutex.
 */
void InitializeConfig() {
    std::scoped_lock lk(g_config_mutex);

    // Load defaults first
    g_config = config::get_default_config();

    // Load from file (overwriting defaults with file values)
    config::load_config(config::CONFIG_PATH, g_config);

    LOG_INFO("Config IPC: Global config initialized");
}

// =============================================================================
// Internal Utilities
// =============================================================================

namespace {

/**
 * @brief Safe string copy with null-termination guarantee
 *
 * Copies up to max_len characters from src to dest, always null-terminating.
 * Unlike strncpy, this guarantees null-termination even if src is longer
 * than max_len.
 *
 * @param dest Destination buffer
 * @param src Source string (null-terminated)
 * @param max_len Maximum characters to copy (excluding null terminator)
 */
void safe_strcpy(char* dest, const char* src, size_t max_len) {
    size_t i = 0;
    while (i < max_len && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

} // anonymous namespace

// =============================================================================
// ConfigService Implementation - Version & Status
// =============================================================================

/**
 * @brief Get the sysmodule version string
 *
 * Returns the current version of the ryu_ldn_nx sysmodule.
 * Format: "MAJOR.MINOR.PATCH" (e.g., "1.0.0")
 *
 * @param out Output buffer for version string (32 bytes, null-terminated)
 * @return Always succeeds
 */
ams::Result ConfigService::GetVersion(ams::sf::Out<std::array<char, 32>> out) {
    static constexpr const char* VERSION = "1.0.0";

    // Clear output buffer
    std::memset(out->data(), 0, out->size());
    safe_strcpy(out->data(), VERSION, out->size() - 1);

    LOG_VERBOSE("Config IPC: GetVersion called -> %s", VERSION);
    R_SUCCEED();
}

/**
 * @brief Get the current connection status
 *
 * Returns status code indicating the sysmodule's operational state.
 *
 * Status codes:
 * - 0: Service running and ready
 * - 1: Connecting to server (future)
 * - 2: Connected (future)
 * - 3: Connection error (future)
 *
 * @param out Output status code
 * @return Always succeeds
 */
ams::Result ConfigService::GetConnectionStatus(ams::sf::Out<u32> out) {
    // Currently always returns 0 (ready)
    // Future: could track actual network connection state
    *out = 0;

    LOG_VERBOSE("Config IPC: GetConnectionStatus -> 0 (ready)");
    R_SUCCEED();
}

/**
 * @brief Check if the IPC service is active
 *
 * Simple ping to verify the service is responding. If this call succeeds,
 * the sysmodule is loaded and the IPC service is operational.
 *
 * @param out Output: 1 = active, 0 = inactive (never returned)
 * @return Always succeeds
 */
ams::Result ConfigService::IsServiceActive(ams::sf::Out<u32> out) {
    // If we're executing this, the service is active
    *out = 1;

    LOG_VERBOSE("Config IPC: IsServiceActive -> 1");
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - LDN Settings
// =============================================================================

/**
 * @brief Get the current room passphrase
 *
 * Returns the passphrase used to filter LDN rooms. Empty string means
 * public/no filtering.
 *
 * @param out Output buffer for passphrase (64 bytes, null-terminated)
 * @return Always succeeds
 */
ams::Result ConfigService::GetPassphrase(ams::sf::Out<std::array<char, 64>> out) {
    std::scoped_lock lk(g_config_mutex);

    std::memset(out->data(), 0, out->size());
    safe_strcpy(out->data(), g_config.ldn.passphrase, out->size() - 1);

    LOG_VERBOSE("Config IPC: GetPassphrase called");
    R_SUCCEED();
}

/**
 * @brief Set the room passphrase
 *
 * Changes the passphrase in memory. Call SaveConfig to persist.
 *
 * @param passphrase New passphrase (64 bytes, null-terminated)
 * @return Always succeeds
 */
ams::Result ConfigService::SetPassphrase(std::array<char, 64> passphrase) {
    std::scoped_lock lk(g_config_mutex);

    safe_strcpy(g_config.ldn.passphrase, passphrase.data(), config::MAX_PASSPHRASE_LENGTH);

    LOG_INFO("Config IPC: SetPassphrase -> '%s'", g_config.ldn.passphrase);
    R_SUCCEED();
}

/**
 * @brief Check if LDN emulation is enabled
 *
 * When disabled, the sysmodule does not intercept LDN calls.
 *
 * @param out Output: 1 = enabled, 0 = disabled
 * @return Always succeeds
 */
ams::Result ConfigService::GetLdnEnabled(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);

    *out = g_config.ldn.enabled ? 1 : 0;

    LOG_VERBOSE("Config IPC: GetLdnEnabled -> %u", *out);
    R_SUCCEED();
}

/**
 * @brief Enable or disable LDN emulation
 *
 * Changes the setting in memory. Call SaveConfig to persist.
 *
 * @param enabled 1 = enable, 0 = disable
 * @return Always succeeds
 */
ams::Result ConfigService::SetLdnEnabled(u32 enabled) {
    std::scoped_lock lk(g_config_mutex);

    g_config.ldn.enabled = (enabled != 0);

    LOG_INFO("Config IPC: SetLdnEnabled -> %s", g_config.ldn.enabled ? "true" : "false");
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - Server Settings
// =============================================================================

/**
 * @brief Get the server address (host and port)
 *
 * Returns the Ryujinx LDN server address currently configured.
 *
 * @param out Output structure with host (64 bytes) and port (u16)
 * @return Always succeeds
 */
ams::Result ConfigService::GetServerAddress(ams::sf::Out<ServerAddressIpc> out) {
    std::scoped_lock lk(g_config_mutex);

    std::memset(&(*out), 0, sizeof(ServerAddressIpc));
    safe_strcpy(out->host, g_config.server.host, sizeof(out->host) - 1);
    out->port = g_config.server.port;

    LOG_VERBOSE("Config IPC: GetServerAddress -> %s:%u", out->host, out->port);
    R_SUCCEED();
}

/**
 * @brief Set the server address (host and port)
 *
 * Changes the server address in memory. Call SaveConfig to persist.
 * Requires restart/reconnect to take effect.
 *
 * @param address New server address structure
 * @return Always succeeds
 */
ams::Result ConfigService::SetServerAddress(ServerAddressIpc address) {
    std::scoped_lock lk(g_config_mutex);

    safe_strcpy(g_config.server.host, address.host, config::MAX_HOST_LENGTH);
    g_config.server.port = address.port;

    LOG_INFO("Config IPC: SetServerAddress -> %s:%u", g_config.server.host, g_config.server.port);
    R_SUCCEED();
}

/**
 * @brief Check if TLS is enabled for server connection
 *
 * @param out Output: 1 = TLS enabled, 0 = plaintext
 * @return Always succeeds
 */
ams::Result ConfigService::GetUseTls(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);

    *out = g_config.server.use_tls ? 1 : 0;

    LOG_VERBOSE("Config IPC: GetUseTls -> %u", *out);
    R_SUCCEED();
}

/**
 * @brief Enable or disable TLS for server connection
 *
 * Changes the setting in memory. Call SaveConfig to persist.
 * Requires restart/reconnect to take effect.
 *
 * @param enabled 1 = enable TLS, 0 = disable
 * @return Always succeeds
 */
ams::Result ConfigService::SetUseTls(u32 enabled) {
    std::scoped_lock lk(g_config_mutex);

    g_config.server.use_tls = (enabled != 0);

    LOG_INFO("Config IPC: SetUseTls -> %s", g_config.server.use_tls ? "true" : "false");
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - Debug Settings
// =============================================================================

/**
 * @brief Check if debug logging is enabled
 *
 * @param out Output: 1 = enabled, 0 = disabled
 * @return Always succeeds
 */
ams::Result ConfigService::GetDebugEnabled(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);

    *out = g_config.debug.enabled ? 1 : 0;

    LOG_VERBOSE("Config IPC: GetDebugEnabled -> %u", *out);
    R_SUCCEED();
}

/**
 * @brief Enable or disable debug logging
 *
 * @param enabled 1 = enable, 0 = disable
 * @return Always succeeds
 */
ams::Result ConfigService::SetDebugEnabled(u32 enabled) {
    std::scoped_lock lk(g_config_mutex);

    g_config.debug.enabled = (enabled != 0);

    LOG_INFO("Config IPC: SetDebugEnabled -> %s", g_config.debug.enabled ? "true" : "false");
    R_SUCCEED();
}

/**
 * @brief Get the current debug log level
 *
 * Log levels:
 * - 0: Error only
 * - 1: Warning and above
 * - 2: Info and above
 * - 3: Verbose (all messages)
 *
 * @param out Output log level (0-3)
 * @return Always succeeds
 */
ams::Result ConfigService::GetDebugLevel(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);

    *out = g_config.debug.level;

    LOG_VERBOSE("Config IPC: GetDebugLevel -> %u", *out);
    R_SUCCEED();
}

/**
 * @brief Set the debug log level
 *
 * @param level New log level (0-3)
 * @return Always succeeds
 */
ams::Result ConfigService::SetDebugLevel(u32 level) {
    std::scoped_lock lk(g_config_mutex);

    g_config.debug.level = level;

    LOG_INFO("Config IPC: SetDebugLevel -> %u", level);
    R_SUCCEED();
}

/**
 * @brief Check if file logging is enabled
 *
 * When enabled, logs are written to SD card at /config/ryu_ldn_nx/ryu_ldn_nx.log
 *
 * @param out Output: 1 = enabled, 0 = disabled
 * @return Always succeeds
 */
ams::Result ConfigService::GetLogToFile(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);

    *out = g_config.debug.log_to_file ? 1 : 0;

    LOG_VERBOSE("Config IPC: GetLogToFile -> %u", *out);
    R_SUCCEED();
}

/**
 * @brief Enable or disable file logging
 *
 * @param enabled 1 = enable, 0 = disable
 * @return Always succeeds
 */
ams::Result ConfigService::SetLogToFile(u32 enabled) {
    std::scoped_lock lk(g_config_mutex);

    g_config.debug.log_to_file = (enabled != 0);

    LOG_INFO("Config IPC: SetLogToFile -> %s", g_config.debug.log_to_file ? "true" : "false");
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - Network Timeouts
// =============================================================================

/**
 * @brief Get the connection timeout in milliseconds
 *
 * Maximum time to wait when establishing connection to server.
 *
 * @param out Output timeout in milliseconds
 * @return Always succeeds
 */
ams::Result ConfigService::GetConnectTimeout(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);

    *out = g_config.network.connect_timeout_ms;

    LOG_VERBOSE("Config IPC: GetConnectTimeout -> %u ms", *out);
    R_SUCCEED();
}

/**
 * @brief Set the connection timeout in milliseconds
 *
 * @param timeout_ms New timeout value
 * @return Always succeeds
 */
ams::Result ConfigService::SetConnectTimeout(u32 timeout_ms) {
    std::scoped_lock lk(g_config_mutex);

    g_config.network.connect_timeout_ms = timeout_ms;

    LOG_INFO("Config IPC: SetConnectTimeout -> %u ms", timeout_ms);
    R_SUCCEED();
}

/**
 * @brief Get the ping interval in milliseconds
 *
 * How often to send keepalive pings to the server.
 *
 * @param out Output interval in milliseconds
 * @return Always succeeds
 */
ams::Result ConfigService::GetPingInterval(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);

    *out = g_config.network.ping_interval_ms;

    LOG_VERBOSE("Config IPC: GetPingInterval -> %u ms", *out);
    R_SUCCEED();
}

/**
 * @brief Set the ping interval in milliseconds
 *
 * @param interval_ms New interval value
 * @return Always succeeds
 */
ams::Result ConfigService::SetPingInterval(u32 interval_ms) {
    std::scoped_lock lk(g_config_mutex);

    g_config.network.ping_interval_ms = interval_ms;

    LOG_INFO("Config IPC: SetPingInterval -> %u ms", interval_ms);
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - File Operations
// =============================================================================

/**
 * @brief Save current configuration to SD card
 *
 * Writes the current in-memory configuration to /config/ryu_ldn_nx/config.ini.
 * This persists any changes made via Set* commands.
 *
 * @param out Output result code:
 *            - Success (0): Config saved successfully
 *            - FileNotFound (1): Unexpected (directory creation failed)
 *            - ParseError (2): Not applicable for save
 *            - IoError (3): Write failed
 *            - InvalidValue (4): Not applicable for save
 * @return Always succeeds (check out for actual result)
 */
ams::Result ConfigService::SaveConfig(ams::sf::Out<ConfigResult> out) {
    std::scoped_lock lk(g_config_mutex);

    config::ConfigResult result = config::save_config(config::CONFIG_PATH, g_config);
    *out = static_cast<ConfigResult>(static_cast<u32>(result));

    LOG_INFO("Config IPC: SaveConfig -> result=%u", static_cast<u32>(*out));
    R_SUCCEED();
}

/**
 * @brief Reload configuration from SD card
 *
 * Discards any unsaved in-memory changes and reloads from config.ini.
 * Useful to revert changes or pick up external modifications.
 *
 * @param out Output result code:
 *            - Success (0): Config reloaded successfully
 *            - FileNotFound (1): Config file doesn't exist
 *            - ParseError (2): Config file has syntax errors
 *            - IoError (3): Read failed
 *            - InvalidValue (4): Invalid config value
 * @return Always succeeds (check out for actual result)
 */
ams::Result ConfigService::ReloadConfig(ams::sf::Out<ConfigResult> out) {
    std::scoped_lock lk(g_config_mutex);

    // Reset to defaults first (ensures clean state)
    g_config = config::get_default_config();

    // Load from file (overwrites defaults)
    config::ConfigResult result = config::load_config(config::CONFIG_PATH, g_config);
    *out = static_cast<ConfigResult>(static_cast<u32>(result));

    LOG_INFO("Config IPC: ReloadConfig -> result=%u", static_cast<u32>(*out));
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - Runtime LDN State
// =============================================================================

/**
 * @brief Check if a game is actively using LDN
 *
 * Returns 1 if a game has initialized the LDN service (ldn:u),
 * 0 otherwise. Used by overlay to determine what UI to show.
 *
 * @param out Output: 1 = game active, 0 = no game
 * @return Always succeeds
 */
ams::Result ConfigService::IsGameActive(ams::sf::Out<u32> out) {
    auto& shared_state = ams::mitm::ldn::SharedState::GetInstance();
    *out = shared_state.IsGameActive() ? 1 : 0;

    LOG_VERBOSE("Config IPC: IsGameActive -> %u", *out);
    R_SUCCEED();
}

/**
 * @brief Get the current LDN communication state
 *
 * Returns the CommState enum value representing current LDN state:
 * - 0: None (not initialized)
 * - 1: Initialized
 * - 2: AccessPoint
 * - 3: AccessPointCreated
 * - 4: Station
 * - 5: StationConnected
 * - 6: Error
 *
 * @param out Output state value (0-6)
 * @return Always succeeds
 */
ams::Result ConfigService::GetLdnState(ams::sf::Out<u32> out) {
    auto& shared_state = ams::mitm::ldn::SharedState::GetInstance();
    *out = static_cast<u32>(shared_state.GetLdnState());

    LOG_VERBOSE("Config IPC: GetLdnState -> %u", *out);
    R_SUCCEED();
}

/**
 * @brief Get session information
 *
 * Returns current session info: node count, max nodes, local node ID,
 * and whether this node is the host.
 *
 * @param out Output SessionInfoIpc structure
 * @return Always succeeds
 */
ams::Result ConfigService::GetSessionInfo(ams::sf::Out<SessionInfoIpc> out) {
    auto& shared_state = ams::mitm::ldn::SharedState::GetInstance();

    // Get session info from SharedState
    ams::mitm::ldn::SessionInfo info = shared_state.GetSessionInfoStruct();

    // Copy to IPC structure
    out->node_count = info.node_count;
    out->max_nodes = info.max_nodes;
    out->local_node_id = info.local_node_id;
    out->is_host = info.is_host;
    std::memset(out->reserved, 0, sizeof(out->reserved));

    LOG_VERBOSE("Config IPC: GetSessionInfo -> nodes=%u/%u, local=%u, host=%u",
                out->node_count, out->max_nodes, out->local_node_id, out->is_host);
    R_SUCCEED();
}

/**
 * @brief Get last measured RTT
 *
 * Returns the last round-trip time measurement in milliseconds.
 * 0 means no RTT has been measured yet.
 *
 * @param out Output RTT in milliseconds
 * @return Always succeeds
 */
ams::Result ConfigService::GetLastRtt(ams::sf::Out<u32> out) {
    auto& shared_state = ams::mitm::ldn::SharedState::GetInstance();
    *out = shared_state.GetLastRtt();

    LOG_VERBOSE("Config IPC: GetLastRtt -> %u ms", *out);
    R_SUCCEED();
}

/**
 * @brief Request reconnection
 *
 * Sets a flag that the MITM service will check to trigger a reconnect.
 * Useful when network conditions change or connection is lost.
 *
 * @return Always succeeds
 */
ams::Result ConfigService::ForceReconnect() {
    auto& shared_state = ams::mitm::ldn::SharedState::GetInstance();
    shared_state.RequestReconnect();

    LOG_INFO("Config IPC: ForceReconnect requested");
    R_SUCCEED();
}

/**
 * @brief Get the process ID of the active game
 *
 * Returns the process ID of the game currently using LDN.
 * Useful for debugging and logging.
 *
 * @param out Output process ID (0 if no game active)
 * @return Always succeeds
 */
ams::Result ConfigService::GetActiveProcessId(ams::sf::Out<u64> out) {
    auto& shared_state = ams::mitm::ldn::SharedState::GetInstance();
    *out = shared_state.GetActiveProcessId();

    LOG_VERBOSE("Config IPC: GetActiveProcessId -> 0x%lX", *out);
    R_SUCCEED();
}

} // namespace ryu_ldn::ipc
