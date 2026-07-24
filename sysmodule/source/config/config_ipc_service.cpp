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

/**
 * @brief Validate a passphrase string for IPC SetPassphrase
 *
 * Rules:
 *   - Length (excluding null terminator) must be <= max_len
 *   - Every character must be printable ASCII (0x20 .. 0x7E)
 *
 * @param str Null-terminated passphrase buffer
 * @param max_len Maximum allowed length excluding the null terminator
 * @return true if valid, false otherwise
 */
bool ValidatePassphrase(const char* str, size_t max_len) {
    if (str == nullptr) {
        return false;
    }
    size_t i = 0;
    while (str[i] != '\0') {
        if (i >= max_len) {
            return false;  // Too long
        }
        const unsigned char c = static_cast<unsigned char>(str[i]);
        if (c < 0x20 || c > 0x7E) {
            return false;  // Non-printable
        }
        i++;
    }
    return true;
}

/**
 * @brief Validate a server host string for IPC SetServerAddress
 *
 * Rules:
 *   - Length (excluding null terminator) must be <= max_len
 *   - Must not contain shell-injection metacharacters: ; | & ` and the
 *     backtick character. These would be dangerous if the host string
 *     were ever passed to a shell (e.g., for DNS lookup helpers).
 *
 * @param str Null-terminated host buffer
 * @param max_len Maximum allowed length excluding the null terminator
 * @return true if valid, false otherwise
 */
bool ValidateHost(const char* str, size_t max_len) {
    if (str == nullptr) {
        return false;
    }
    size_t i = 0;
    while (str[i] != '\0') {
        if (i >= max_len) {
            return false;  // Too long
        }
        const char c = str[i];
        // Reject shell-injection metacharacters
        if (c == ';' || c == '|' || c == '&' || c == '`') {
            return false;
        }
        i++;
    }
    return true;
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
ams::Result ConfigService::GetVersion(RyuCfgOutVersionString out) {
    static constexpr const char* VERSION = "1.0.0";

    // Clear output buffer
    std::memset(out->buf.data(), 0, out->size());
    safe_strcpy(out->buf.data(), VERSION, out->size() - 1);

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
ams::Result ConfigService::GetPassphrase(RyuCfgOutPassphraseString out) {
    std::scoped_lock lk(g_config_mutex);

    std::memset(out->buf.data(), 0, out->size());
    safe_strcpy(out->buf.data(), g_config.ldn.passphrase, out->size() - 1);

    LOG_VERBOSE("Config IPC: GetPassphrase called");
    R_SUCCEED();
}

/**
 * @brief Set the room passphrase
 *
 * Changes the passphrase in memory. Call SaveConfig to persist.
 *
 * Validates that the passphrase is at most MAX_PASSPHRASE_LENGTH characters
 * long and contains only printable ASCII (0x20 .. 0x7E). Returns
 * `result::ResultInvalidIpcInput` if the input is rejected; in that case the
 * in-memory passphrase is left untouched.
 *
 * @param passphrase New passphrase (64 bytes, null-terminated)
 * @return Success or `result::ResultInvalidIpcInput` on validation failure
 */
ams::Result ConfigService::SetPassphrase(RyuCfgPassphraseString passphrase) {
    if (!ValidatePassphrase(passphrase.buf.data(), config::MAX_PASSPHRASE_LENGTH)) {
        LOG_WARN("Config IPC: SetPassphrase rejected (invalid length or non-printable chars)");
        R_THROW(result::ResultInvalidIpcInput());
    }

    std::scoped_lock lk(g_config_mutex);

    safe_strcpy(g_config.ldn.passphrase, passphrase.buf.data(), config::MAX_PASSPHRASE_LENGTH);

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
ams::Result ConfigService::GetServerAddress(RyuCfgOutServerAddress out) {
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
 * Validates that the host string is at most MAX_HOST_LENGTH characters long
 * and does not contain shell-injection metacharacters (`;`, `|`, `&`,
 * backtick). Returns `result::ResultInvalidIpcInput` if the input is rejected; in
 * that case the in-memory host is left untouched.
 *
 * @param address New server address structure
 * @return Success or `result::ResultInvalidIpcInput` on validation failure
 */
ams::Result ConfigService::SetServerAddress(RyuCfgInServerAddress address) {
    if (!ValidateHost(address.host, config::MAX_HOST_LENGTH)) {
        LOG_WARN("Config IPC: SetServerAddress rejected (invalid host length or shell metacharacters)");
        R_THROW(result::ResultInvalidIpcInput());
    }

    std::scoped_lock lk(g_config_mutex);

    safe_strcpy(g_config.server.host, address.host, config::MAX_HOST_LENGTH);
    g_config.server.port = address.port;

    LOG_INFO("Config IPC: SetServerAddress -> %s:%u", g_config.server.host, g_config.server.port);
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - Debug Settings
// =============================================================================

ams::Result ConfigService::GetDebugEnabled(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);
    *out = g_config.debug.enabled ? 1 : 0;
    LOG_VERBOSE("Config IPC: GetDebugEnabled -> %u", *out);
    R_SUCCEED();
}

ams::Result ConfigService::SetDebugEnabled(u32 enabled) {
    std::scoped_lock lk(g_config_mutex);
    g_config.debug.enabled = (enabled != 0);
    LOG_INFO("Config IPC: SetDebugEnabled -> %s", g_config.debug.enabled ? "true" : "false");
    R_SUCCEED();
}

ams::Result ConfigService::GetDebugLevel(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);
    *out = g_config.debug.level;
    LOG_VERBOSE("Config IPC: GetDebugLevel -> %u", *out);
    R_SUCCEED();
}

ams::Result ConfigService::SetDebugLevel(u32 level) {
    // Validate debug level range. The logger supports levels 0..4 inclusive
    // (0 = Error, 1 = Warn, 2 = Info, 3 = Verbose, 4 = Trace-equivalent).
    // Out-of-range values would corrupt the in-memory config and could lead
    // to undefined behavior in the logger's level comparisons.
    if (level > 4) {
        LOG_WARN("Config IPC: SetDebugLevel rejected (level=%u out of range 0..4)", level);
        R_THROW(result::ResultInvalidIpcInput());
    }

    std::scoped_lock lk(g_config_mutex);
    g_config.debug.level = level;
    LOG_INFO("Config IPC: SetDebugLevel -> %u", g_config.debug.level);
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - Configuration Persistence
// =============================================================================

ams::Result ConfigService::SaveConfig(RyuCfgOutConfigResult out) {
    std::scoped_lock lk(g_config_mutex);
    config::ConfigResult result = config::save_config(config::CONFIG_PATH, g_config);
    *out = static_cast<ConfigResult>(result);
    if (result == config::ConfigResult::Success) {
        LOG_INFO("Config IPC: SaveConfig -> Success");
    } else {
        LOG_WARN("Config IPC: SaveConfig -> %s", config::config_result_to_string(result));
    }
    R_SUCCEED();
}

ams::Result ConfigService::ReloadConfig(ams::sf::Out<ConfigResult> out) {
    std::scoped_lock lk(g_config_mutex);
    g_config = config::get_default_config();
    config::ConfigResult result = config::load_config(config::CONFIG_PATH, g_config);
    *out = static_cast<ConfigResult>(result);
    if (result == config::ConfigResult::Success) {
        LOG_INFO("Config IPC: ReloadConfig -> Success");
    } else {
        LOG_WARN("Config IPC: ReloadConfig -> %s (using defaults)", config::config_result_to_string(result));
    }
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - Passphrase Filtering
// =============================================================================

ams::Result ConfigService::GetUsePassphrase(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);
    *out = g_config.ldn.use_passphrase ? 1 : 0;
    LOG_VERBOSE("Config IPC: GetUsePassphrase -> %u", *out);
    R_SUCCEED();
}

ams::Result ConfigService::SetUsePassphrase(u32 enabled) {
    std::scoped_lock lk(g_config_mutex);
    g_config.ldn.use_passphrase = (enabled != 0);
    LOG_INFO("Config IPC: SetUsePassphrase -> %s", g_config.ldn.use_passphrase ? "true" : "false");
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - Runtime LDN State
// =============================================================================

ams::Result ConfigService::IsGameActive(ams::sf::Out<u32> out) {
    auto& state = ams::mitm::ldn::SharedState::GetInstance();
    *out = state.IsGameActive() ? 1 : 0;
    LOG_VERBOSE("Config IPC: IsGameActive -> %u", *out);
    R_SUCCEED();
}

ams::Result ConfigService::GetLdnState(ams::sf::Out<u32> out) {
    auto& state = ams::mitm::ldn::SharedState::GetInstance();
    *out = static_cast<u32>(state.GetLdnState());
    LOG_VERBOSE("Config IPC: GetLdnState -> %u", *out);
    R_SUCCEED();
}

ams::Result ConfigService::GetSessionInfo(RyuCfgOutSessionInfo out) {
    auto& state = ams::mitm::ldn::SharedState::GetInstance();
    u8 node_count, max_nodes, local_node_id;
    bool is_host;
    state.GetSessionInfo(node_count, max_nodes, local_node_id, is_host);

    std::memset(&(*out), 0, sizeof(SessionInfoIpc));
    out->node_count = node_count;
    out->max_nodes = max_nodes;
    out->local_node_id = local_node_id;
    out->is_host = is_host ? 1 : 0;

    LOG_VERBOSE("Config IPC: GetSessionInfo -> nodes=%u/%u local=%u host=%u",
                out->node_count, out->max_nodes, out->local_node_id, out->is_host);
    R_SUCCEED();
}

ams::Result ConfigService::GetLastRtt(ams::sf::Out<u32> out) {
    auto& state = ams::mitm::ldn::SharedState::GetInstance();
    *out = state.GetLastRtt();
    LOG_VERBOSE("Config IPC: GetLastRtt -> %u ms", *out);
    R_SUCCEED();
}

ams::Result ConfigService::ForceReconnect() {
    auto& state = ams::mitm::ldn::SharedState::GetInstance();
    state.RequestReconnect();
    LOG_INFO("Config IPC: ForceReconnect requested");
    R_SUCCEED();
}

ams::Result ConfigService::GetActiveProcessId(ams::sf::Out<u64> out) {
    auto& state = ams::mitm::ldn::SharedState::GetInstance();
    *out = state.GetActiveProcessId();
    LOG_VERBOSE("Config IPC: GetActiveProcessId -> 0x%lx", *out);
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - P2P Proxy Control
// =============================================================================

ams::Result ConfigService::GetDisableP2p(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);
    *out = g_config.ldn.disable_p2p ? 1 : 0;
    LOG_VERBOSE("Config IPC: GetDisableP2p -> %u", *out);
    R_SUCCEED();
}

ams::Result ConfigService::SetDisableP2p(u32 disabled) {
    std::scoped_lock lk(g_config_mutex);
    g_config.ldn.disable_p2p = (disabled != 0);
    LOG_INFO("Config IPC: SetDisableP2p -> %s", g_config.ldn.disable_p2p ? "true" : "false");
    R_SUCCEED();
}

} // namespace ryu_ldn::ipc
