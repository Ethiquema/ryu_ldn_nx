/**
 * @file ldn_config_service.hpp
 * @brief Configuration IPC service for Tesla overlay
 *
 * Provides a custom IPC interface exposed through the ldn:u MITM service
 * for the Tesla overlay to query status and change configuration.
 *
 * ## IPC Interface
 * The service is obtained via command 65000 on ldn:u and exposes:
 *
 * ### Basic Commands (65001-65010)
 * - 65001: GetVersion - Returns sysmodule version string
 * - 65002: GetConnectionStatus - Returns server connection state
 * - 65003: GetLdnState - Returns current LDN state
 * - 65004: GetSessionInfo - Returns current session information
 * - 65005: GetServerAddress - Returns configured server address
 * - 65006: SetServerAddress - Changes server address
 * - 65007: GetDebugEnabled - Returns debug logging state
 * - 65008: SetDebugEnabled - Enables/disables debug logging
 * - 65009: ForceReconnect - Forces server reconnection
 * - 65010: GetLastRtt - Returns last ping RTT
 *
 * ### Extended Config Commands (65011-65030)
 * - 65011: GetPassphrase / 65012: SetPassphrase
 * - 65013: GetLdnEnabled / 65014: SetLdnEnabled
 * - 65017: GetConnectTimeout / 65018: SetConnectTimeout
 * - 65019: GetPingInterval / 65020: SetPingInterval
 * - 65021: GetReconnectDelay / 65022: SetReconnectDelay
 * - 65023: GetMaxReconnectAttempts / 65024: SetMaxReconnectAttempts
 * - 65025: GetDebugLevel / 65026: SetDebugLevel
 * - 65027: GetLogToFile / 65028: SetLogToFile
 * - 65029: SaveConfig - Save current config to file
 * - 65030: ReloadConfig - Reload config from file
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include "ldn_types.hpp"

namespace ams::mitm::ldn {

// Forward declarations
class LdnICommunication;

/**
 * @brief Connection status for overlay
 */
enum class ConnectionStatus : u32 {
    Disconnected = 0,   ///< Not connected to server
    Connecting = 1,     ///< Connection in progress
    Connected = 2,      ///< Connected, handshake pending
    Ready = 3,          ///< Fully connected and ready
    Error = 4,          ///< Connection error
};

// SessionInfo is defined in ldn_types.hpp

/**
 * @brief Server address structure
 */
struct ServerAddress {
    char host[64];
    u16 port;
    u16 padding;
};
static_assert(sizeof(ServerAddress) == 68);

/**
 * @brief Passphrase structure for IPC
 */
struct Passphrase {
    char passphrase[64];
};
static_assert(sizeof(Passphrase) == 64);

/**
 * @brief Network settings structure for batch retrieval
 */
struct NetworkSettings {
    u32 connect_timeout_ms;
    u32 ping_interval_ms;
    u32 reconnect_delay_ms;
    u32 max_reconnect_attempts;
};
static_assert(sizeof(NetworkSettings) == 16);

/**
 * @brief Debug settings structure for batch retrieval
 */
struct DebugSettings {
    u32 enabled;      ///< Bool as u32
    u32 level;        ///< 0-3
    u32 log_to_file;  ///< Bool as u32
    u32 reserved;
};
static_assert(sizeof(DebugSettings) == 16);

/**
 * @brief Config operation result
 */
enum class ConfigResult : u32 {
    Success = 0,
    FileNotFound = 1,
    ParseError = 2,
    IoError = 3,
    InvalidValue = 4,
};

/**
 * @brief Configuration service for Tesla overlay
 *
 * This service is created per-session when the overlay requests it
 * via command 65000 on the ldn:u MITM service.
 */
class LdnConfigService {
public:
    /**
     * @brief Constructor
     *
     * @param communication Pointer to parent LdnICommunication service
     */
    /// @gdb{tag="LDN:LIFECYCLE", msg="ConfigService constructor"}
    explicit LdnConfigService(LdnICommunication* communication);

    /**
     * @brief Get sysmodule version string
     *
     * @param out Output buffer for version string (32 bytes)
     * @return Always succeeds
     */
    /// @gdb{tag="LDN:LIFECYCLE", msg="Version queried"}
    Result GetVersion(sf::Out<std::array<char, 32>> out);

    /**
     * @brief Get current connection status
     *
     * @param out Output connection status
     * @return Always succeeds
     */
    /// @gdb{tag="LDN:LIFECYCLE", msg="Connection status queried"}
    Result GetConnectionStatus(sf::Out<ConnectionStatus> out);

    /**
     * @brief Get current LDN state
     *
     * @param out Output LDN state (as u32)
     * @return Always succeeds
     */
    /// @gdb{tag="LDN:LIFECYCLE", msg="LDN state queried"}
    Result GetLdnState(sf::Out<u32> out);

    /**
     * @brief Get session information
     *
     * @param out Output session info structure
     * @return Always succeeds
     */
    /// @gdb{tag="LDN:LIFECYCLE", msg="Session info queried"}
    Result GetSessionInfo(sf::Out<SessionInfo> out);

    /**
     * @brief Get server address
     *
     * @param out Output server address structure
     * @return Always succeeds
     */
    /// @gdb{tag="LDN:CONFIG", msg="GetServerAddress"}
    Result GetServerAddress(sf::Out<ServerAddress> out);

    /**
     * @brief Set server address
     *
     * @param address New server address
     * @return ResultSuccess on success
     */
    /// @gdb{tag="LDN:CONFIG", msg="SetServerAddress"}
    Result SetServerAddress(const ServerAddress &address);

    /**
     * @brief Get debug logging state
     *
     * @param out Output: 1 if enabled, 0 if disabled
     * @return Always succeeds
     */
    /// @gdb{tag="LDN:CONFIG", msg="GetDebugEnabled"}
    Result GetDebugEnabled(sf::Out<u32> out);

    /**
     * @brief Set debug logging state
     *
     * @param enabled 1 to enable, 0 to disable
     * @return Always succeeds
     */
    /// @gdb{tag="LDN:CONFIG", msg="SetDebugEnabled"}
    Result SetDebugEnabled(u32 enabled);

    /**
     * @brief Force server reconnection
     *
     * @return ResultSuccess on success
     */
    /// @gdb{tag="LDN:OPS", msg="ForceReconnect"}
    Result ForceReconnect();

    /**
     * @brief Get last RTT in milliseconds
     *
     * @param out Output RTT in ms
     * @return Always succeeds
     */
    /// @gdb{tag="LDN:OPS", msg="GetLastRtt"}
    Result GetLastRtt(sf::Out<u32> out);

    // =========================================================================
    // Extended Configuration Commands (65011-65030)
    // =========================================================================

    /**
     * @brief Get passphrase (65011)
     */
    /// @gdb{tag="LDN:CONFIG", msg="GetPassphrase"}
    Result GetPassphrase(sf::Out<Passphrase> out);

    /**
     * @brief Set passphrase (65012)
     */
    /// @gdb{tag="LDN:CONFIG", msg="SetPassphrase"}
    Result SetPassphrase(Passphrase passphrase);

    /**
     * @brief Get LDN enabled state (65013)
     */
    /// @gdb{tag="LDN:CONFIG", msg="GetLdnEnabled"}
    Result GetLdnEnabled(sf::Out<u32> out);

    /**
     * @brief Set LDN enabled state (65014)
     */
    /// @gdb{tag="LDN:CONFIG", msg="SetLdnEnabled"}
    Result SetLdnEnabled(u32 enabled);

    /**
     * @brief Get connect timeout in ms (65017)
     */
    /// @gdb{tag="LDN:CONFIG", msg="GetConnectTimeout"}
    Result GetConnectTimeout(sf::Out<u32> out);

    /**
     * @brief Set connect timeout in ms (65018)
     */
    /// @gdb{tag="LDN:CONFIG", msg="SetConnectTimeout"}
    Result SetConnectTimeout(u32 timeout_ms);

    /**
     * @brief Get ping interval in ms (65019)
     */
    /// @gdb{tag="LDN:CONFIG", msg="GetPingInterval"}
    Result GetPingInterval(sf::Out<u32> out);

    /**
     * @brief Set ping interval in ms (65020)
     */
    /// @gdb{tag="LDN:CONFIG", msg="SetPingInterval"}
    Result SetPingInterval(u32 interval_ms);

    /**
     * @brief Get reconnect delay in ms (65021)
     */
    /// @gdb{tag="LDN:CONFIG", msg="GetReconnectDelay"}
    Result GetReconnectDelay(sf::Out<u32> out);

    /**
     * @brief Set reconnect delay in ms (65022)
     */
    /// @gdb{tag="LDN:CONFIG", msg="SetReconnectDelay"}
    Result SetReconnectDelay(u32 delay_ms);

    /**
     * @brief Get max reconnect attempts (65023)
     */
    /// @gdb{tag="LDN:CONFIG", msg="GetMaxReconnectAttempts"}
    Result GetMaxReconnectAttempts(sf::Out<u32> out);

    /**
     * @brief Set max reconnect attempts (65024)
     */
    /// @gdb{tag="LDN:CONFIG", msg="SetMaxReconnectAttempts"}
    Result SetMaxReconnectAttempts(u32 attempts);

    /**
     * @brief Get debug level 0-3 (65025)
     */
    /// @gdb{tag="LDN:CONFIG", msg="GetDebugLevel"}
    Result GetDebugLevel(sf::Out<u32> out);

    /**
     * @brief Set debug level 0-3 (65026)
     */
    /// @gdb{tag="LDN:CONFIG", msg="SetDebugLevel"}
    Result SetDebugLevel(u32 level);

    /**
     * @brief Get log to file state (65027)
     */
    /// @gdb{tag="LDN:CONFIG", msg="GetLogToFile"}
    Result GetLogToFile(sf::Out<u32> out);

    /**
     * @brief Set log to file state (65028)
     */
    /// @gdb{tag="LDN:CONFIG", msg="SetLogToFile"}
    Result SetLogToFile(u32 enabled);

    /**
     * @brief Save config to file (65029)
     *
     * @param out Result of save operation
     */
    /// @gdb{tag="LDN:CONFIG", msg="SaveConfig"}
    Result SaveConfig(sf::Out<ConfigResult> out);

    /**
     * @brief Reload config from file (65030)
     *
     * @param out Result of reload operation
     */
    /// @gdb{tag="LDN:CONFIG", msg="ReloadConfig"}
    Result ReloadConfig(sf::Out<ConfigResult> out);

private:
    LdnICommunication* m_communication;
};

} // namespace ams::mitm::ldn
