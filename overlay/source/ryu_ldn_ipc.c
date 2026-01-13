/**
 * @file ryu_ldn_ipc.c
 * @brief IPC client implementation for ryu_ldn_nx sysmodule
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ryu_ldn_ipc.h"
#include <string.h>

/**
 * IPC Command IDs for ryu_ldn_nx custom interface
 *
 * These commands are added to the ldn:u MITM service and are
 * not part of Nintendo's original interface.
 */
enum {
    // Basic commands (65000-65010)
    RyuLdnCmd_GetConfigService    = 65000,
    RyuLdnCmd_GetVersion          = 65001,
    RyuLdnCmd_GetConnectionStatus = 65002,
    RyuLdnCmd_GetLdnState         = 65003,
    RyuLdnCmd_GetSessionInfo      = 65004,
    RyuLdnCmd_GetServerAddress    = 65005,
    RyuLdnCmd_SetServerAddress    = 65006,
    RyuLdnCmd_GetDebugEnabled     = 65007,
    RyuLdnCmd_SetDebugEnabled     = 65008,
    RyuLdnCmd_ForceReconnect      = 65009,
    RyuLdnCmd_GetLastRtt          = 65010,

    // Extended config commands (65011-65030)
    RyuLdnCmd_GetPassphrase           = 65011,
    RyuLdnCmd_SetPassphrase           = 65012,
    RyuLdnCmd_GetLdnEnabled           = 65013,
    RyuLdnCmd_SetLdnEnabled           = 65014,
    RyuLdnCmd_GetUseTls               = 65015,
    RyuLdnCmd_SetUseTls               = 65016,
    RyuLdnCmd_GetConnectTimeout       = 65017,
    RyuLdnCmd_SetConnectTimeout       = 65018,
    RyuLdnCmd_GetPingInterval         = 65019,
    RyuLdnCmd_SetPingInterval         = 65020,
    RyuLdnCmd_GetReconnectDelay       = 65021,
    RyuLdnCmd_SetReconnectDelay       = 65022,
    RyuLdnCmd_GetMaxReconnectAttempts = 65023,
    RyuLdnCmd_SetMaxReconnectAttempts = 65024,
    RyuLdnCmd_GetDebugLevel           = 65025,
    RyuLdnCmd_SetDebugLevel           = 65026,
    RyuLdnCmd_GetLogToFile            = 65027,
    RyuLdnCmd_SetLogToFile            = 65028,
    RyuLdnCmd_SaveConfig              = 65029,
    RyuLdnCmd_ReloadConfig            = 65030,
};

Result ryuLdnGetConfigFromService(Service* ldn_srv, RyuLdnConfigService* out) {
    return serviceDispatch(ldn_srv, RyuLdnCmd_GetConfigService,
        .out_num_objects = 1,
        .out_objects = &out->s
    );
}

Result ryuLdnGetVersion(RyuLdnConfigService* s, char* version) {
    char version_buf[32];
    Result rc = serviceDispatchOut(&s->s, RyuLdnCmd_GetVersion, version_buf);
    if (R_SUCCEEDED(rc)) {
        memcpy(version, version_buf, 32);
        version[31] = '\0';  // Ensure null termination
    }
    return rc;
}

Result ryuLdnGetConnectionStatus(RyuLdnConfigService* s, RyuLdnConnectionStatus* status) {
    u32 out_status;
    Result rc = serviceDispatchOut(&s->s, RyuLdnCmd_GetConnectionStatus, out_status);
    if (R_SUCCEEDED(rc)) {
        *status = (RyuLdnConnectionStatus)out_status;
    }
    return rc;
}

Result ryuLdnGetLdnState(RyuLdnConfigService* s, RyuLdnState* state) {
    u32 out_state;
    Result rc = serviceDispatchOut(&s->s, RyuLdnCmd_GetLdnState, out_state);
    if (R_SUCCEEDED(rc)) {
        *state = (RyuLdnState)out_state;
    }
    return rc;
}

Result ryuLdnGetSessionInfo(RyuLdnConfigService* s, RyuLdnSessionInfo* info) {
    return serviceDispatchOut(&s->s, RyuLdnCmd_GetSessionInfo, *info);
}

Result ryuLdnGetServerAddress(RyuLdnConfigService* s, char* host, u16* port) {
    struct {
        char host[64];
        u16 port;
        u16 padding;
    } out;

    Result rc = serviceDispatchOut(&s->s, RyuLdnCmd_GetServerAddress, out);
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

    return serviceDispatchIn(&s->s, RyuLdnCmd_SetServerAddress, in);
}

Result ryuLdnGetDebugEnabled(RyuLdnConfigService* s, u32* enabled) {
    return serviceDispatchOut(&s->s, RyuLdnCmd_GetDebugEnabled, *enabled);
}

Result ryuLdnSetDebugEnabled(RyuLdnConfigService* s, u32 enabled) {
    return serviceDispatchIn(&s->s, RyuLdnCmd_SetDebugEnabled, enabled);
}

Result ryuLdnForceReconnect(RyuLdnConfigService* s) {
    return serviceDispatch(&s->s, RyuLdnCmd_ForceReconnect);
}

Result ryuLdnGetLastRtt(RyuLdnConfigService* s, u32* rtt_ms) {
    return serviceDispatchOut(&s->s, RyuLdnCmd_GetLastRtt, *rtt_ms);
}

//=============================================================================
// Extended Configuration Commands (65011-65030)
//=============================================================================

Result ryuLdnGetPassphrase(RyuLdnConfigService* s, char* passphrase) {
    char passphrase_buf[64];
    Result rc = serviceDispatchOut(&s->s, RyuLdnCmd_GetPassphrase, passphrase_buf);
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
    return serviceDispatchIn(&s->s, RyuLdnCmd_SetPassphrase, passphrase_buf);
}

Result ryuLdnGetLdnEnabled(RyuLdnConfigService* s, u32* enabled) {
    return serviceDispatchOut(&s->s, RyuLdnCmd_GetLdnEnabled, *enabled);
}

Result ryuLdnSetLdnEnabled(RyuLdnConfigService* s, u32 enabled) {
    return serviceDispatchIn(&s->s, RyuLdnCmd_SetLdnEnabled, enabled);
}

Result ryuLdnGetUseTls(RyuLdnConfigService* s, u32* enabled) {
    return serviceDispatchOut(&s->s, RyuLdnCmd_GetUseTls, *enabled);
}

Result ryuLdnSetUseTls(RyuLdnConfigService* s, u32 enabled) {
    return serviceDispatchIn(&s->s, RyuLdnCmd_SetUseTls, enabled);
}

Result ryuLdnGetConnectTimeout(RyuLdnConfigService* s, u32* timeout_ms) {
    return serviceDispatchOut(&s->s, RyuLdnCmd_GetConnectTimeout, *timeout_ms);
}

Result ryuLdnSetConnectTimeout(RyuLdnConfigService* s, u32 timeout_ms) {
    return serviceDispatchIn(&s->s, RyuLdnCmd_SetConnectTimeout, timeout_ms);
}

Result ryuLdnGetPingInterval(RyuLdnConfigService* s, u32* interval_ms) {
    return serviceDispatchOut(&s->s, RyuLdnCmd_GetPingInterval, *interval_ms);
}

Result ryuLdnSetPingInterval(RyuLdnConfigService* s, u32 interval_ms) {
    return serviceDispatchIn(&s->s, RyuLdnCmd_SetPingInterval, interval_ms);
}

Result ryuLdnGetReconnectDelay(RyuLdnConfigService* s, u32* delay_ms) {
    return serviceDispatchOut(&s->s, RyuLdnCmd_GetReconnectDelay, *delay_ms);
}

Result ryuLdnSetReconnectDelay(RyuLdnConfigService* s, u32 delay_ms) {
    return serviceDispatchIn(&s->s, RyuLdnCmd_SetReconnectDelay, delay_ms);
}

Result ryuLdnGetMaxReconnectAttempts(RyuLdnConfigService* s, u32* attempts) {
    return serviceDispatchOut(&s->s, RyuLdnCmd_GetMaxReconnectAttempts, *attempts);
}

Result ryuLdnSetMaxReconnectAttempts(RyuLdnConfigService* s, u32 attempts) {
    return serviceDispatchIn(&s->s, RyuLdnCmd_SetMaxReconnectAttempts, attempts);
}

Result ryuLdnGetDebugLevel(RyuLdnConfigService* s, u32* level) {
    return serviceDispatchOut(&s->s, RyuLdnCmd_GetDebugLevel, *level);
}

Result ryuLdnSetDebugLevel(RyuLdnConfigService* s, u32 level) {
    return serviceDispatchIn(&s->s, RyuLdnCmd_SetDebugLevel, level);
}

Result ryuLdnGetLogToFile(RyuLdnConfigService* s, u32* enabled) {
    return serviceDispatchOut(&s->s, RyuLdnCmd_GetLogToFile, *enabled);
}

Result ryuLdnSetLogToFile(RyuLdnConfigService* s, u32 enabled) {
    return serviceDispatchIn(&s->s, RyuLdnCmd_SetLogToFile, enabled);
}

Result ryuLdnSaveConfig(RyuLdnConfigService* s, RyuLdnConfigResult* result) {
    u32 out_result;
    Result rc = serviceDispatchOut(&s->s, RyuLdnCmd_SaveConfig, out_result);
    if (R_SUCCEEDED(rc)) {
        *result = (RyuLdnConfigResult)out_result;
    }
    return rc;
}

Result ryuLdnReloadConfig(RyuLdnConfigService* s, RyuLdnConfigResult* result) {
    u32 out_result;
    Result rc = serviceDispatchOut(&s->s, RyuLdnCmd_ReloadConfig, out_result);
    if (R_SUCCEEDED(rc)) {
        *result = (RyuLdnConfigResult)out_result;
    }
    return rc;
}
