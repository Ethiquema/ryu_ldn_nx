/**
 * @file ryu_ldn_ipc.c
 * @brief IPC client implementation for ryu_ldn_nx sysmodule
 *
 * Connects to the standalone ryu:cfg IPC service provided by the sysmodule.
 * Command IDs match ConfigCmd in config_ipc_service.hpp.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ryu_ldn_ipc.h"
#include <string.h>

/**
 * IPC Command IDs for ryu:cfg service
 *
 * These match the ConfigCmd enum in config_ipc_service.hpp.
 */
enum {
    // Sysmodule Status (0-8)
    RyuCfgCmd_GetVersion          = 0,
    RyuCfgCmd_GetConnectionStatus = 1,
    RyuCfgCmd_IsServiceActive     = 2,
    RyuCfgCmd_IsGameActive        = 3,
    RyuCfgCmd_GetLdnState         = 4,
    RyuCfgCmd_GetSessionInfo      = 5,
    RyuCfgCmd_GetLastRtt          = 6,
    RyuCfgCmd_ForceReconnect      = 7,
    RyuCfgCmd_GetActiveProcessId  = 8,

    // Sysmodule General Settings (9-14)
    RyuCfgCmd_GetDebugEnabled     = 9,
    RyuCfgCmd_SetDebugEnabled     = 10,
    RyuCfgCmd_GetDebugLevel       = 11,
    RyuCfgCmd_SetDebugLevel       = 12,
    RyuCfgCmd_SaveConfig          = 13,
    RyuCfgCmd_ReloadConfig        = 14,

    // Sysmodule Configuration Manager (15-24)
    RyuCfgCmd_GetServerAddress    = 15,
    RyuCfgCmd_SetServerAddress    = 16,
    RyuCfgCmd_GetLdnEnabled       = 17,
    RyuCfgCmd_SetLdnEnabled       = 18,
    RyuCfgCmd_GetDisableP2p       = 19,
    RyuCfgCmd_SetDisableP2p       = 20,
    RyuCfgCmd_GetUsePassphrase    = 21,
    RyuCfgCmd_SetUsePassphrase    = 22,
    RyuCfgCmd_GetPassphrase       = 23,
    RyuCfgCmd_SetPassphrase       = 24,
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

//=============================================================================
// Sysmodule Status Commands (0-8)
//=============================================================================

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

Result ryuLdnIsServiceActive(RyuLdnConfigService* s, u32* active) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_IsServiceActive, *active);
}

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

//=============================================================================
// Sysmodule General Settings Commands (9-14)
//=============================================================================

Result ryuLdnGetDebugEnabled(RyuLdnConfigService* s, u32* enabled) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetDebugEnabled, *enabled);
}

Result ryuLdnSetDebugEnabled(RyuLdnConfigService* s, u32 enabled) {
    return serviceDispatchIn(&s->s, RyuCfgCmd_SetDebugEnabled, enabled);
}

Result ryuLdnGetDebugLevel(RyuLdnConfigService* s, u32* level) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetDebugLevel, *level);
}

Result ryuLdnSetDebugLevel(RyuLdnConfigService* s, u32 level) {
    return serviceDispatchIn(&s->s, RyuCfgCmd_SetDebugLevel, level);
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
// Sysmodule Configuration Manager Commands (15-24)
//=============================================================================

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

Result ryuLdnGetLdnEnabled(RyuLdnConfigService* s, u32* enabled) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetLdnEnabled, *enabled);
}

Result ryuLdnSetLdnEnabled(RyuLdnConfigService* s, u32 enabled) {
    return serviceDispatchIn(&s->s, RyuCfgCmd_SetLdnEnabled, enabled);
}

Result ryuLdnGetDisableP2p(RyuLdnConfigService* s, u32* disabled) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetDisableP2p, *disabled);
}

Result ryuLdnSetDisableP2p(RyuLdnConfigService* s, u32 disabled) {
    return serviceDispatchIn(&s->s, RyuCfgCmd_SetDisableP2p, disabled);
}

Result ryuLdnGetUsePassphrase(RyuLdnConfigService* s, u32* enabled) {
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetUsePassphrase, *enabled);
}

Result ryuLdnSetUsePassphrase(RyuLdnConfigService* s, u32 enabled) {
    return serviceDispatchIn(&s->s, RyuCfgCmd_SetUsePassphrase, enabled);
}

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
