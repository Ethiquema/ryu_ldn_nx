/**
 * @file ryu_ldn_ipc.h
 * @brief IPC client for ryu_ldn_nx sysmodule
 *
 * Provides functions to communicate with the ryu_ldn_nx sysmodule
 * for retrieving status information and changing configuration.
 *
 * ## Service Name
 * The sysmodule exposes a standalone IPC service: ryu:cfg
 *
 * ## IPC Command IDs (ryu:cfg service)
 *
 * Command IDs match ConfigCmd in config_ipc_service.hpp.
 * Get/Set pairs: Get even, Set odd.
 *
 * | ID | Command            | Description                       |
 * |----|--------------------|-----------------------------------|
 * | 0  | GetVersion         | Get sysmodule version string      |
 * | 1  | GetConnectionStatus| Get current connection state      |
 * | 2  | GetPassphrase      | Get room passphrase               |
 * | 3  | SetPassphrase      | Set room passphrase               |
 * | 4  | GetServerAddress   | Get server host and port          |
 * | 5  | SetServerAddress   | Set server host and port          |
 * | 6  | GetLdnEnabled      | Check if LDN emulation is on      |
 * | 7  | SetLdnEnabled      | Toggle LDN emulation              |
 * | 8  | GetDebugEnabled    | Check debug logging state         |
 * | 9  | SetDebugEnabled    | Toggle debug logging              |
 * | 10 | GetDebugLevel      | Get log verbosity (0-3)           |
 * | 11 | SetDebugLevel      | Set log verbosity                 |
 * | 12 | SaveConfig         | Persist config to SD card         |
 * | 13 | ReloadConfig       | Reload config from SD card        |
 * | 14 | IsServiceActive    | Ping to check service is running  |
 * | 16 | GetUsePassphrase   | Check passphrase filtering state  |
 * | 17 | SetUsePassphrase   | Toggle passphrase filtering       |
 * | 18 | IsGameActive       | Check if game is using LDN        |
 * | 19 | GetLdnState        | Get current LDN CommState (0-6)   |
 * | 20 | GetSessionInfo     | Get session info struct           |
 * | 21 | GetLastRtt         | Get last measured RTT (ms)        |
 * | 22 | ForceReconnect     | Request MITM to reconnect         |
 * | 23 | GetActiveProcessId | Get PID of active game (debug)     |
 * | 24 | GetDisableP2p      | Check P2P proxy disabled state    |
 * | 25 | SetDisableP2p      | Set P2P proxy disabled state      | 
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
 * @brief LDN communication state
 *
 * Mirrors the CommState enum from the sysmodule.
 */
typedef enum {
    RyuLdnState_None = 0,               ///< Not initialized
    RyuLdnState_Initialized = 1,        ///< Initialized, ready to open AP or Station
    RyuLdnState_AccessPoint = 2,        ///< Access point mode, ready to create network
    RyuLdnState_AccessPointCreated = 3, ///< Network created, accepting connections
    RyuLdnState_Station = 4,            ///< Station mode, ready to scan/connect
    RyuLdnState_StationConnected = 5,   ///< Connected to a network
    RyuLdnState_Error = 6,              ///< Error state
} RyuLdnState;

/**
 * @brief Session information structure
 *
 * Contains runtime information about the current LDN session.
 */
typedef struct {
    u8 node_count;      ///< Current number of nodes in session
    u8 max_nodes;       ///< Maximum nodes allowed in session
    u8 local_node_id;   ///< This node's ID in the session
    u8 is_host;         ///< 1 if this node is the host, 0 otherwise
    u8 reserved[4];     ///< Reserved for future use
} RyuLdnSessionInfo;

/**
 * @brief Configuration service handle
 */
typedef struct {
    Service s;
} RyuLdnConfigService;

/**
 * @brief Configuration operation result
 */
typedef enum {
    RyuLdnConfigResult_Success = 0,
    RyuLdnConfigResult_FileNotFound = 1,
    RyuLdnConfigResult_ParseError = 2,
    RyuLdnConfigResult_IoError = 3,
    RyuLdnConfigResult_InvalidValue = 4,
} RyuLdnConfigResult;

/**
 * @brief Initialize connection to ryu:cfg service
 *
 * This opens a connection to the standalone ryu:cfg service.
 * Call ryuLdnExit() when done.
 *
 * @return Result code (0 on success)
 */
Result ryuLdnInitialize(void);

/**
 * @brief Close connection to ryu:cfg service
 */
void ryuLdnExit(void);

/**
 * @brief Get the configuration service handle
 *
 * @return Pointer to internal service (valid after ryuLdnInitialize)
 */
RyuLdnConfigService* ryuLdnGetService(void);

/**
 * @brief Check if sysmodule is active
 *
 * @param s Configuration service
 * @param active Output: 1 if active, 0 otherwise
 * @return Result code
 */
Result ryuLdnIsServiceActive(RyuLdnConfigService* s, u32* active);

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

//=============================================================================
// Configuration Commands
//=============================================================================

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
 * @brief Get passphrase filtering state
 *
 * When use_passphrase is true, LDN rooms are filtered by the
 * passphrase — only players with the same passphrase can see
 * and join each other's sessions.
 *
 * @param s Configuration service
 * @param enabled Output: 1 if passphrase filtering is enabled, 0 if disabled
 * @return Result code
 */
Result ryuLdnGetUsePassphrase(RyuLdnConfigService* s, u32* enabled);

/**
 * @brief Set passphrase filtering state
 *
 * @param s Configuration service
 * @param enabled 1 to enable passphrase filtering, 0 to disable
 * @return Result code
 */
Result ryuLdnSetUsePassphrase(RyuLdnConfigService* s, u32 enabled);

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

//=============================================================================
// Runtime LDN State Commands (18-23)
//=============================================================================

/**
 * @brief Check if a game is actively using LDN
 *
 * @param s Configuration service
 * @param active Output: 1 if game is active, 0 otherwise
 * @return Result code
 */
Result ryuLdnIsGameActive(RyuLdnConfigService* s, u32* active);

/**
 * @brief Get current LDN communication state
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
 * @param info Output session info structure
 * @return Result code
 */
Result ryuLdnGetSessionInfo(RyuLdnConfigService* s, RyuLdnSessionInfo* info);

/**
 * @brief Get last measured round-trip time
 *
 * @param s Configuration service
 * @param rtt_ms Output RTT in milliseconds
 * @return Result code
 */
Result ryuLdnGetLastRtt(RyuLdnConfigService* s, u32* rtt_ms);

/**
 * @brief Request the MITM service to reconnect
 *
 * @param s Configuration service
 * @return Result code
 */
Result ryuLdnForceReconnect(RyuLdnConfigService* s);

/**
 * @brief Get process ID of the active game
 *
 * @param s Configuration service
 * @param pid Output process ID (0 if no game active)
 * @return Result code
 */
Result ryuLdnGetActiveProcessId(RyuLdnConfigService* s, u64* pid);

//=============================================================================
// P2P Proxy Control Commands (24-25)
//=============================================================================

/**
 * @brief Get P2P proxy disabled state
 *
 * When P2P is disabled, all traffic goes through the relay server.
 * P2P proxy is disabled by default (disable_p2p = 1).
 *
 * @param s Configuration service
 * @param disabled Output: 1 if P2P proxy is disabled, 0 if enabled
 * @return Result code
 */
Result ryuLdnGetDisableP2p(RyuLdnConfigService* s, u32* disabled);

/**
 * @brief Set P2P proxy disabled state
 *
 * @param s Configuration service
 * @param disabled 1 to disable P2P proxy, 0 to enable
 * @return Result code
 */
Result ryuLdnSetDisableP2p(RyuLdnConfigService* s, u32 disabled);

/**
 * @brief Convert LDN state to human-readable string
 *
 * @param state LDN state value
 * @return Static string describing the state
 */
const char* ryuLdnStateToString(RyuLdnState state);

#ifdef __cplusplus
}
#endif
