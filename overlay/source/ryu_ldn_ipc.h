/**
 * @file ryu_ldn_ipc.h
 * @brief IPC client for ryu_ldn_nx sysmodule
 *
 * Provides functions to communicate with the ryu_ldn_nx sysmodule
 * for retrieving status information and changing configuration.
 *
 * ## IPC Command IDs
 * The sysmodule exposes custom commands on the ldn:u service:
 *
 * ### Basic Commands (65000-65010)
 * - 65000: GetConfigService - Get configuration service handle
 * - 65001: GetVersion - Get sysmodule version string
 * - 65002: GetConnectionStatus - Get current connection state
 * - 65003: GetLdnState - Get current LDN state
 * - 65004: GetSessionInfo - Get current session info
 * - 65005: GetServerAddress - Get configured server address
 * - 65006: SetServerAddress - Change server address
 * - 65007: GetDebugEnabled - Check if debug logging is on
 * - 65008: SetDebugEnabled - Enable/disable debug logging
 * - 65009: ForceReconnect - Force reconnection to server
 * - 65010: GetLastRtt - Get last RTT in ms
 *
 * ### Extended Config Commands (65011-65030)
 * - 65011: GetPassphrase / 65012: SetPassphrase
 * - 65013: GetLdnEnabled / 65014: SetLdnEnabled
 * - 65015: GetUseTls / 65016: SetUseTls
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
#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Connection status enumeration
 */
typedef enum {
    RyuLdnStatus_Disconnected = 0,   ///< Not connected to server
    RyuLdnStatus_Connecting = 1,     ///< Connection in progress
    RyuLdnStatus_Connected = 2,      ///< Connected, handshake pending
    RyuLdnStatus_Ready = 3,          ///< Fully connected and ready
    RyuLdnStatus_Error = 4,          ///< Connection error
} RyuLdnConnectionStatus;

/**
 * @brief LDN state enumeration (matches Nintendo's states)
 */
typedef enum {
    RyuLdnState_None = 0,
    RyuLdnState_Initialized = 1,
    RyuLdnState_AccessPoint = 2,
    RyuLdnState_AccessPointCreated = 3,
    RyuLdnState_Station = 4,
    RyuLdnState_StationConnected = 5,
    RyuLdnState_Error = 6,
} RyuLdnState;

/**
 * @brief Session information structure
 */
typedef struct {
    u8 node_count;           ///< Number of players in session
    u8 node_count_max;       ///< Maximum players allowed
    u8 local_node_id;        ///< Our node ID in the session
    u8 is_host;              ///< 1 if we are the host, 0 if client
    u32 session_duration_ms; ///< Time since session started (ms)
    char game_name[64];      ///< Game name (if available)
} RyuLdnSessionInfo;

/**
 * @brief Configuration service handle
 */
typedef struct {
    Service s;
} RyuLdnConfigService;

/**
 * @brief Get configuration service from ldn:u
 *
 * @param ldn_srv Pointer to ldn:u service
 * @param out Output configuration service
 * @return Result code
 */
Result ryuLdnGetConfigFromService(Service* ldn_srv, RyuLdnConfigService* out);

/**
 * @brief Get sysmodule version string
 *
 * @param s Configuration service
 * @param version Output buffer (at least 32 bytes)
 * @return Result code
 */
Result ryuLdnGetVersion(RyuLdnConfigService* s, char* version);

/**
 * @brief Get current connection status
 *
 * @param s Configuration service
 * @param status Output status
 * @return Result code
 */
Result ryuLdnGetConnectionStatus(RyuLdnConfigService* s, RyuLdnConnectionStatus* status);

/**
 * @brief Get current LDN state
 *
 * @param s Configuration service
 * @param state Output LDN state
 * @return Result code
 */
Result ryuLdnGetLdnState(RyuLdnConfigService* s, RyuLdnState* state);

/**
 * @brief Get session information
 *
 * @param s Configuration service
 * @param info Output session info
 * @return Result code
 */
Result ryuLdnGetSessionInfo(RyuLdnConfigService* s, RyuLdnSessionInfo* info);

/**
 * @brief Get configured server address
 *
 * @param s Configuration service
 * @param host Output host buffer (at least 64 bytes)
 * @param port Output port
 * @return Result code
 */
Result ryuLdnGetServerAddress(RyuLdnConfigService* s, char* host, u16* port);

/**
 * @brief Set server address
 *
 * @param s Configuration service
 * @param host Server hostname
 * @param port Server port
 * @return Result code
 */
Result ryuLdnSetServerAddress(RyuLdnConfigService* s, const char* host, u16 port);

/**
 * @brief Get debug logging state
 *
 * @param s Configuration service
 * @param enabled Output: 1 if enabled, 0 if disabled
 * @return Result code
 */
Result ryuLdnGetDebugEnabled(RyuLdnConfigService* s, u32* enabled);

/**
 * @brief Enable/disable debug logging
 *
 * @param s Configuration service
 * @param enabled 1 to enable, 0 to disable
 * @return Result code
 */
Result ryuLdnSetDebugEnabled(RyuLdnConfigService* s, u32 enabled);

/**
 * @brief Force reconnection to server
 *
 * @param s Configuration service
 * @return Result code
 */
Result ryuLdnForceReconnect(RyuLdnConfigService* s);

/**
 * @brief Get last RTT (round-trip time) in milliseconds
 *
 * @param s Configuration service
 * @param rtt_ms Output RTT in ms
 * @return Result code
 */
Result ryuLdnGetLastRtt(RyuLdnConfigService* s, u32* rtt_ms);

//=============================================================================
// Extended Configuration Commands (65011-65030)
//=============================================================================

/**
 * @brief Config operation result
 */
typedef enum {
    RyuLdnConfigResult_Success = 0,
    RyuLdnConfigResult_FileNotFound = 1,
    RyuLdnConfigResult_ParseError = 2,
    RyuLdnConfigResult_IoError = 3,
    RyuLdnConfigResult_InvalidValue = 4,
} RyuLdnConfigResult;

/**
 * @brief Get passphrase
 *
 * @param s Configuration service
 * @param passphrase Output buffer (at least 64 bytes)
 * @return Result code
 */
Result ryuLdnGetPassphrase(RyuLdnConfigService* s, char* passphrase);

/**
 * @brief Set passphrase
 *
 * @param s Configuration service
 * @param passphrase Passphrase string (max 63 chars)
 * @return Result code
 */
Result ryuLdnSetPassphrase(RyuLdnConfigService* s, const char* passphrase);

/**
 * @brief Get LDN enabled state
 *
 * @param s Configuration service
 * @param enabled Output: 1 if enabled, 0 if disabled
 * @return Result code
 */
Result ryuLdnGetLdnEnabled(RyuLdnConfigService* s, u32* enabled);

/**
 * @brief Set LDN enabled state
 *
 * @param s Configuration service
 * @param enabled 1 to enable, 0 to disable
 * @return Result code
 */
Result ryuLdnSetLdnEnabled(RyuLdnConfigService* s, u32 enabled);

/**
 * @brief Get TLS enabled state
 *
 * @param s Configuration service
 * @param enabled Output: 1 if enabled, 0 if disabled
 * @return Result code
 */
Result ryuLdnGetUseTls(RyuLdnConfigService* s, u32* enabled);

/**
 * @brief Set TLS enabled state
 *
 * @param s Configuration service
 * @param enabled 1 to enable, 0 to disable
 * @return Result code
 */
Result ryuLdnSetUseTls(RyuLdnConfigService* s, u32 enabled);

/**
 * @brief Get connect timeout in milliseconds
 *
 * @param s Configuration service
 * @param timeout_ms Output timeout value
 * @return Result code
 */
Result ryuLdnGetConnectTimeout(RyuLdnConfigService* s, u32* timeout_ms);

/**
 * @brief Set connect timeout in milliseconds
 *
 * @param s Configuration service
 * @param timeout_ms Timeout value
 * @return Result code
 */
Result ryuLdnSetConnectTimeout(RyuLdnConfigService* s, u32 timeout_ms);

/**
 * @brief Get ping interval in milliseconds
 *
 * @param s Configuration service
 * @param interval_ms Output interval value
 * @return Result code
 */
Result ryuLdnGetPingInterval(RyuLdnConfigService* s, u32* interval_ms);

/**
 * @brief Set ping interval in milliseconds
 *
 * @param s Configuration service
 * @param interval_ms Interval value
 * @return Result code
 */
Result ryuLdnSetPingInterval(RyuLdnConfigService* s, u32 interval_ms);

/**
 * @brief Get reconnect delay in milliseconds
 *
 * @param s Configuration service
 * @param delay_ms Output delay value
 * @return Result code
 */
Result ryuLdnGetReconnectDelay(RyuLdnConfigService* s, u32* delay_ms);

/**
 * @brief Set reconnect delay in milliseconds
 *
 * @param s Configuration service
 * @param delay_ms Delay value
 * @return Result code
 */
Result ryuLdnSetReconnectDelay(RyuLdnConfigService* s, u32 delay_ms);

/**
 * @brief Get max reconnect attempts
 *
 * @param s Configuration service
 * @param attempts Output attempts value
 * @return Result code
 */
Result ryuLdnGetMaxReconnectAttempts(RyuLdnConfigService* s, u32* attempts);

/**
 * @brief Set max reconnect attempts
 *
 * @param s Configuration service
 * @param attempts Attempts value (0 = unlimited)
 * @return Result code
 */
Result ryuLdnSetMaxReconnectAttempts(RyuLdnConfigService* s, u32 attempts);

/**
 * @brief Get debug level (0-3)
 *
 * @param s Configuration service
 * @param level Output level (0=Error, 1=Warn, 2=Info, 3=Verbose)
 * @return Result code
 */
Result ryuLdnGetDebugLevel(RyuLdnConfigService* s, u32* level);

/**
 * @brief Set debug level (0-3)
 *
 * @param s Configuration service
 * @param level Level (0=Error, 1=Warn, 2=Info, 3=Verbose)
 * @return Result code
 */
Result ryuLdnSetDebugLevel(RyuLdnConfigService* s, u32 level);

/**
 * @brief Get log to file state
 *
 * @param s Configuration service
 * @param enabled Output: 1 if enabled, 0 if disabled
 * @return Result code
 */
Result ryuLdnGetLogToFile(RyuLdnConfigService* s, u32* enabled);

/**
 * @brief Set log to file state
 *
 * @param s Configuration service
 * @param enabled 1 to enable, 0 to disable
 * @return Result code
 */
Result ryuLdnSetLogToFile(RyuLdnConfigService* s, u32 enabled);

/**
 * @brief Save configuration to file
 *
 * @param s Configuration service
 * @param result Output operation result
 * @return Result code
 */
Result ryuLdnSaveConfig(RyuLdnConfigService* s, RyuLdnConfigResult* result);

/**
 * @brief Reload configuration from file
 *
 * @param s Configuration service
 * @param result Output operation result
 * @return Result code
 */
Result ryuLdnReloadConfig(RyuLdnConfigService* s, RyuLdnConfigResult* result);

#ifdef __cplusplus
}
#endif
