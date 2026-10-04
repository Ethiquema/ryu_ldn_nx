/**
 * @file ryu_ldn_ipc.c
 * @brief IPC client implementation for ryu_ldn_nx sysmodule
 *
 * Connects to the standalone ryu:cfg IPC service provided by the sysmodule.
 * Command IDs match ConfigCmd in config_ipc_service.hpp.
 *
 * ## Defensive measures
 *
 * - **NULL service guard (M6)**: every public function that takes a
 *   `RyuLdnConfigService*` parameter returns `RyuLdn_ResultInvalidArg`
 *   immediately when the pointer is NULL. This prevents a NULL-deref crash
 *   in `serviceDispatch*` if the overlay code forgot to call
 *   `ryuLdnInitialize()` first or if the service handle could not be
 *   obtained.
 * - **Zero-initialized output buffers (M5)**: every local buffer passed to
 *   `serviceDispatchOut` is `memset` to zero before the dispatch call. The
 *   IPC server may only partially fill the buffer (e.g., a shorter version
 *   string, a struct with reserved padding); without zeroing, the unfilled
 *   bytes would carry uninitialized stack data back to the caller, which
 *   could then leak into the overlay UI or be memcpy'd into a larger
 *   structure. Zeroing guarantees deterministic, leak-free output.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ryu_ldn_ipc.h"
#include <stdio.h>
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

/**
 * @brief Result code returned when a public IPC function is called with a
 *        NULL service pointer.
 *
 * Uses libnx module 1 (Kernel) description 2 (InvalidArg). Any non-zero
 * value is sufficient — the overlay checks success via R_SUCCEEDED(rc),
 * which treats any non-zero Result as failure.
 */
#define RyuLdn_ResultInvalidArg MAKERESULT(1, 2)

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
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    char version_buf[32];
    memset(version_buf, 0, sizeof(version_buf));
    Result rc = serviceDispatchOut(&s->s, RyuCfgCmd_GetVersion, version_buf);
    if (R_SUCCEEDED(rc)) {
        memcpy(version, version_buf, 32);
        version[31] = '\0';  // Ensure null termination
    }
    return rc;
}

Result ryuLdnGetConnectionStatus(RyuLdnConfigService* s, RyuLdnConnectionStatus* status) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    u32 out_status = 0;
    Result rc = serviceDispatchOut(&s->s, RyuCfgCmd_GetConnectionStatus, out_status);
    if (R_SUCCEEDED(rc)) {
        *status = (RyuLdnConnectionStatus)out_status;
    }
    return rc;
}

Result ryuLdnIsServiceActive(RyuLdnConfigService* s, u32* active) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    *active = 0;
    return serviceDispatchOut(&s->s, RyuCfgCmd_IsServiceActive, *active);
}

Result ryuLdnIsGameActive(RyuLdnConfigService* s, u32* active) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    *active = 0;
    return serviceDispatchOut(&s->s, RyuCfgCmd_IsGameActive, *active);
}

Result ryuLdnGetLdnState(RyuLdnConfigService* s, RyuLdnState* state) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    u32 out_state = 0;
    Result rc = serviceDispatchOut(&s->s, RyuCfgCmd_GetLdnState, out_state);
    if (R_SUCCEEDED(rc)) {
        *state = (RyuLdnState)out_state;
    }
    return rc;
}

Result ryuLdnGetSessionInfo(RyuLdnConfigService* s, RyuLdnSessionInfo* info) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    memset(info, 0, sizeof(*info));
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetSessionInfo, *info);
}

Result ryuLdnGetLastRtt(RyuLdnConfigService* s, u32* rtt_ms) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    *rtt_ms = 0;
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetLastRtt, *rtt_ms);
}

Result ryuLdnForceReconnect(RyuLdnConfigService* s) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    return serviceDispatch(&s->s, RyuCfgCmd_ForceReconnect);
}

Result ryuLdnGetActiveProcessId(RyuLdnConfigService* s, u64* pid) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    *pid = 0;
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetActiveProcessId, *pid);
}

//=============================================================================
// Sysmodule General Settings Commands (9-14)
//=============================================================================

Result ryuLdnGetDebugEnabled(RyuLdnConfigService* s, u32* enabled) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    *enabled = 0;
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetDebugEnabled, *enabled);
}

Result ryuLdnSetDebugEnabled(RyuLdnConfigService* s, u32 enabled) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    return serviceDispatchIn(&s->s, RyuCfgCmd_SetDebugEnabled, enabled);
}

Result ryuLdnGetDebugLevel(RyuLdnConfigService* s, u32* level) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    *level = 0;
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetDebugLevel, *level);
}

Result ryuLdnSetDebugLevel(RyuLdnConfigService* s, u32 level) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    return serviceDispatchIn(&s->s, RyuCfgCmd_SetDebugLevel, level);
}

Result ryuLdnSaveConfig(RyuLdnConfigService* s, RyuLdnConfigResult* result) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    u32 out_result = 0;
    Result rc = serviceDispatchOut(&s->s, RyuCfgCmd_SaveConfig, out_result);
    if (R_SUCCEEDED(rc)) {
        *result = (RyuLdnConfigResult)out_result;
    }
    return rc;
}

Result ryuLdnReloadConfig(RyuLdnConfigService* s, RyuLdnConfigResult* result) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    u32 out_result = 0;
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
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    struct {
        char host[64];
        u16 port;
        u16 padding;
    } out;

    memset(&out, 0, sizeof(out));
    Result rc = serviceDispatchOut(&s->s, RyuCfgCmd_GetServerAddress, out);
    if (R_SUCCEEDED(rc)) {
        memcpy(host, out.host, 64);
        host[63] = '\0';
        *port = out.port;
    }
    return rc;
}

Result ryuLdnSetServerAddress(RyuLdnConfigService* s, const char* host, u16 port) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    struct {
        char host[64];
        u16 port;
        u16 padding;
    } in;

    memset(&in, 0, sizeof(in));
    if (host == NULL) {
        return RyuLdn_ResultInvalidArg;
    }
    snprintf(in.host, sizeof(in.host), "%s", host);
    in.port = port;

    return serviceDispatchIn(&s->s, RyuCfgCmd_SetServerAddress, in);
}

Result ryuLdnGetLdnEnabled(RyuLdnConfigService* s, u32* enabled) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    *enabled = 0;
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetLdnEnabled, *enabled);
}

Result ryuLdnSetLdnEnabled(RyuLdnConfigService* s, u32 enabled) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    return serviceDispatchIn(&s->s, RyuCfgCmd_SetLdnEnabled, enabled);
}

Result ryuLdnGetDisableP2p(RyuLdnConfigService* s, u32* disabled) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    *disabled = 0;
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetDisableP2p, *disabled);
}

Result ryuLdnSetDisableP2p(RyuLdnConfigService* s, u32 disabled) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    return serviceDispatchIn(&s->s, RyuCfgCmd_SetDisableP2p, disabled);
}

Result ryuLdnGetUsePassphrase(RyuLdnConfigService* s, u32* enabled) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    *enabled = 0;
    return serviceDispatchOut(&s->s, RyuCfgCmd_GetUsePassphrase, *enabled);
}

Result ryuLdnSetUsePassphrase(RyuLdnConfigService* s, u32 enabled) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    return serviceDispatchIn(&s->s, RyuCfgCmd_SetUsePassphrase, enabled);
}

Result ryuLdnGetPassphrase(RyuLdnConfigService* s, char* passphrase) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    char passphrase_buf[64];
    memset(passphrase_buf, 0, sizeof(passphrase_buf));
    Result rc = serviceDispatchOut(&s->s, RyuCfgCmd_GetPassphrase, passphrase_buf);
    if (R_SUCCEEDED(rc)) {
        memcpy(passphrase, passphrase_buf, 64);
        passphrase[63] = '\0';
    }
    return rc;
}

Result ryuLdnSetPassphrase(RyuLdnConfigService* s, const char* passphrase) {
    if (!s) {
        return RyuLdn_ResultInvalidArg;
    }

    char passphrase_buf[64];
    memset(passphrase_buf, 0, sizeof(passphrase_buf));
    if (passphrase == NULL) {
        return RyuLdn_ResultInvalidArg;
    }
    snprintf(passphrase_buf, sizeof(passphrase_buf), "%s", passphrase);
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