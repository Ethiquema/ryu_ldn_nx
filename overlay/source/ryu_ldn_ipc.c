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
