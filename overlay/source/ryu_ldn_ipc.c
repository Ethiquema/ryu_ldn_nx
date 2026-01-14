/**
 * @file ryu_ldn_ipc.c
 * @brief IPC client implementation for ryu_ldn_nx sysmodule
 *
 * Connects to the standalone ryu:cfg IPC service provided by the sysmodule.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ryu_ldn_ipc.h"
#include <string.h>

/**
 * IPC Command IDs for ryu:cfg service
 *
 * These are the command IDs for the standalone configuration service.
 */
enum {
    // Configuration commands (0-22)
    RyuCfgCmd_GetVersion          = 0,
    RyuCfgCmd_GetConnectionStatus = 1,
    RyuCfgCmd_GetPassphrase       = 2,
    RyuCfgCmd_SetPassphrase       = 3,
    RyuCfgCmd_GetServerAddress    = 4,
    RyuCfgCmd_SetServerAddress    = 5,
    RyuCfgCmd_GetLdnEnabled       = 6,
    RyuCfgCmd_SetLdnEnabled       = 7,
    RyuCfgCmd_GetUseTls           = 8,
    RyuCfgCmd_SetUseTls           = 9,
    RyuCfgCmd_GetDebugEnabled     = 10,
    RyuCfgCmd_SetDebugEnabled     = 11,
    RyuCfgCmd_GetDebugLevel       = 12,
    RyuCfgCmd_SetDebugLevel       = 13,
    RyuCfgCmd_GetLogToFile        = 14,
    RyuCfgCmd_SetLogToFile        = 15,
    RyuCfgCmd_SaveConfig          = 16,
    RyuCfgCmd_ReloadConfig        = 17,
    RyuCfgCmd_GetConnectTimeout   = 18,
    RyuCfgCmd_SetConnectTimeout   = 19,
    RyuCfgCmd_GetPingInterval     = 20,
    RyuCfgCmd_SetPingInterval     = 21,
    RyuCfgCmd_IsServiceActive     = 22,

    // Runtime LDN state commands (23-28)
    RyuCfgCmd_IsGameActive        = 23,
    RyuCfgCmd_GetLdnState         = 24,
    RyuCfgCmd_GetSessionInfo      = 25,
    RyuCfgCmd_GetLastRtt          = 26,
    RyuCfgCmd_ForceReconnect      = 27,
    RyuCfgCmd_GetActiveProcessId  = 28,
};

/// Global service handle
static RyuLdnConfigService g_ryuCfgService;
static bool g_ryuCfgInitialized = false;

Result ryuLdnInitialize(void) {
    if (g_ryuCfgInitialized) {
        return 0;  // Already initialized
    }

    Result rc = smGetService(&g_ryuCfgService.s, "ryu:cfg");
    if (R_SUCCEEDED(rc)) {
        g_ryuCfgInitialized = true;
    }
    return rc;
}

void ryuLdnExit(void) {
    if (g_ryuCfgInitialized) {
        serviceClose(&g_ryuCfgService.s);
        g_ryuCfgInitialized = false;
    }
}

RyuLdnConfigService* ryuLdnGetService(void) {
    return g_ryuCfgInitialized ? &g_ryuCfgService : NULL;
}

Result ryuLdnIsServiceActive(RyuLdnConfigService* s, u32* active) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_IsServiceActive, *active);
}

Result ryuLdnGetVersion(RyuLdnConfigService* s, char* version) {
    char version_buf[32];
    Result rc = serviceDispatchOut(&s->s, RyuCfgCmd_GetVersion, version_buf);
    if (R_SUCCEEDED(rc)) {
        memcpy(version, version_buf, 32);
        version[31] = '\0';  // Ensure null termination
    }
    return rc;
}

Result ryuLdnGetConnectionStatus(RyuLdnConfigService* s, RyuLdnConnectionStatus* status) {
    u32 out_status;
    Result rc = serviceDispatchOut(&s->s, RyuCfgCmd_GetConnectionStatus, out_status);
    if (R_SUCCEEDED(rc)) {
        *status = (RyuLdnConnectionStatus)out_status;
    }
    return rc;
}

Result ryuLdnGetServerAddress(RyuLdnConfigService* s, char* host, u16* port) {
    struct {
        char host[64];
        u16 port;
        u16 padding;
    } out;

    Result rc = serviceDispatchOut(&s->s, RyuCfgCmd_GetServerAddress, out);
    if (R_SUCCEEDED(rc)) {
        memcpy(host, out.host, 64);
        host[63] = '\0';
        *port = out.port;
    }
    return rc;
}

Result ryuLdnSetServerAddress(RyuLdnConfigService* s, const char* host, u16 port) {
    struct {
        char host[64];
        u16 port;
        u16 padding;
    } in;

    memset(&in, 0, sizeof(in));
    strncpy(in.host, host, 63);
    in.port = port;

    return serviceDispatchIn(&s->s, RyuCfgCmd_SetServerAddress, in);
}

Result ryuLdnGetDebugEnabled(RyuLdnConfigService* s, u32* enabled) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetDebugEnabled, *enabled);
}

Result ryuLdnSetDebugEnabled(RyuLdnConfigService* s, u32 enabled) {
    return serviceDispatchIn(&s->s, RyuCfgCmd_SetDebugEnabled, enabled);
}

//=============================================================================
// Configuration Commands
//=============================================================================

Result ryuLdnGetPassphrase(RyuLdnConfigService* s, char* passphrase) {
    char passphrase_buf[64];
    Result rc = serviceDispatchOut(&s->s, RyuCfgCmd_GetPassphrase, passphrase_buf);
    if (R_SUCCEEDED(rc)) {
        memcpy(passphrase, passphrase_buf, 64);
        passphrase[63] = '\0';
    }
    return rc;
}

Result ryuLdnSetPassphrase(RyuLdnConfigService* s, const char* passphrase) {
    char passphrase_buf[64];
    memset(passphrase_buf, 0, sizeof(passphrase_buf));
    if (passphrase != NULL) {
        strncpy(passphrase_buf, passphrase, 63);
    }
    return serviceDispatchIn(&s->s, RyuCfgCmd_SetPassphrase, passphrase_buf);
}

Result ryuLdnGetLdnEnabled(RyuLdnConfigService* s, u32* enabled) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetLdnEnabled, *enabled);
}

Result ryuLdnSetLdnEnabled(RyuLdnConfigService* s, u32 enabled) {
    return serviceDispatchIn(&s->s, RyuCfgCmd_SetLdnEnabled, enabled);
}

Result ryuLdnGetUseTls(RyuLdnConfigService* s, u32* enabled) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetUseTls, *enabled);
}

Result ryuLdnSetUseTls(RyuLdnConfigService* s, u32 enabled) {
    return serviceDispatchIn(&s->s, RyuCfgCmd_SetUseTls, enabled);
}

Result ryuLdnGetConnectTimeout(RyuLdnConfigService* s, u32* timeout_ms) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetConnectTimeout, *timeout_ms);
}

Result ryuLdnSetConnectTimeout(RyuLdnConfigService* s, u32 timeout_ms) {
    return serviceDispatchIn(&s->s, RyuCfgCmd_SetConnectTimeout, timeout_ms);
}

Result ryuLdnGetPingInterval(RyuLdnConfigService* s, u32* interval_ms) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetPingInterval, *interval_ms);
}

Result ryuLdnSetPingInterval(RyuLdnConfigService* s, u32 interval_ms) {
    return serviceDispatchIn(&s->s, RyuCfgCmd_SetPingInterval, interval_ms);
}

Result ryuLdnGetDebugLevel(RyuLdnConfigService* s, u32* level) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetDebugLevel, *level);
}

Result ryuLdnSetDebugLevel(RyuLdnConfigService* s, u32 level) {
    return serviceDispatchIn(&s->s, RyuCfgCmd_SetDebugLevel, level);
}

Result ryuLdnGetLogToFile(RyuLdnConfigService* s, u32* enabled) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetLogToFile, *enabled);
}

Result ryuLdnSetLogToFile(RyuLdnConfigService* s, u32 enabled) {
    return serviceDispatchIn(&s->s, RyuCfgCmd_SetLogToFile, enabled);
}

Result ryuLdnSaveConfig(RyuLdnConfigService* s, RyuLdnConfigResult* result) {
    u32 out_result;
    Result rc = serviceDispatchOut(&s->s, RyuCfgCmd_SaveConfig, out_result);
    if (R_SUCCEEDED(rc)) {
        *result = (RyuLdnConfigResult)out_result;
    }
    return rc;
}

Result ryuLdnReloadConfig(RyuLdnConfigService* s, RyuLdnConfigResult* result) {
    u32 out_result;
    Result rc = serviceDispatchOut(&s->s, RyuCfgCmd_ReloadConfig, out_result);
    if (R_SUCCEEDED(rc)) {
        *result = (RyuLdnConfigResult)out_result;
    }
    return rc;
}

//=============================================================================
// Runtime LDN State Commands (23-28)
//=============================================================================

Result ryuLdnIsGameActive(RyuLdnConfigService* s, u32* active) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_IsGameActive, *active);
}

Result ryuLdnGetLdnState(RyuLdnConfigService* s, RyuLdnState* state) {
    u32 out_state;
    Result rc = serviceDispatchOut(&s->s, RyuCfgCmd_GetLdnState, out_state);
    if (R_SUCCEEDED(rc)) {
        *state = (RyuLdnState)out_state;
    }
    return rc;
}

Result ryuLdnGetSessionInfo(RyuLdnConfigService* s, RyuLdnSessionInfo* info) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetSessionInfo, *info);
}

Result ryuLdnGetLastRtt(RyuLdnConfigService* s, u32* rtt_ms) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetLastRtt, *rtt_ms);
}

Result ryuLdnForceReconnect(RyuLdnConfigService* s) {
    return serviceDispatch(&s->s, RyuCfgCmd_ForceReconnect);
}

Result ryuLdnGetActiveProcessId(RyuLdnConfigService* s, u64* pid) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetActiveProcessId, *pid);
}

const char* ryuLdnStateToString(RyuLdnState state) {
    switch (state) {
        case RyuLdnState_None:               return "None";
        case RyuLdnState_Initialized:        return "Initialized";
        case RyuLdnState_AccessPoint:        return "Access Point";
        case RyuLdnState_AccessPointCreated: return "AP Created";
        case RyuLdnState_Station:            return "Station";
        case RyuLdnState_StationConnected:   return "Connected";
        case RyuLdnState_Error:              return "Error";
        default:                             return "Unknown";
    }
}
