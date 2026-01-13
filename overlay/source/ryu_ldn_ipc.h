/**
 * @file ryu_ldn_ipc.h
 * @brief IPC client for ryu_ldn_nx sysmodule
 *
 * Provides functions to communicate with the ryu_ldn_nx sysmodule
 * for retrieving status information and changing configuration.
 *
 * ## IPC Command IDs
 * The sysmodule exposes custom commands on the ldn:u service:
 * - 65000: GetConfigService - Get configuration service handle
 * - 65001: GetVersion - Get sysmodule version string
 * - 65002: GetConnectionStatus - Get current connection state
 * - 65003: GetNetworkInfo - Get current session info
 * - 65004: GetServerAddress - Get configured server address
 * - 65005: SetServerAddress - Change server address
 * - 65006: GetDebugEnabled - Check if debug logging is on
 * - 65007: SetDebugEnabled - Enable/disable debug logging
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

#ifdef __cplusplus
}
#endif
