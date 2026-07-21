/**
 * @file config_actions.hpp
 * @brief Standalone config save/reload helper functions.
 *
 * Extracted from auto_save_controller.hpp to avoid dependency on the
 * AutoSaveController class. Used by AdvancedSettingsGui.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */
#pragma once

#include <switch.h>

#include "ryu_ldn_ipc.h"
#include "app/overlay_state.hpp"

/** @brief Save config via IPC. Returns true on success. */
inline bool DoSaveConfig() {
    if (OverlayState::Instance().GetStatus() != OverlayState::InitStatus::Loaded) return false;
    RyuLdnConfigService* svc = ryuLdnGetService();
    if (!svc) return false;
    RyuLdnConfigResult result;
    Result rc = ryuLdnSaveConfig(svc, &result);
    if (R_SUCCEEDED(rc) && result == RyuLdnConfigResult_Success) {
        OverlayState::Instance().MarkSaved();
        return true;
    }
    return false;
}

/** @brief Reload config from disk via IPC. Returns true on success. */
inline bool DoReloadConfig() {
    if (OverlayState::Instance().GetStatus() != OverlayState::InitStatus::Loaded) return false;
    RyuLdnConfigService* svc = ryuLdnGetService();
    if (!svc) return false;
    RyuLdnConfigResult result;
    Result rc = ryuLdnReloadConfig(svc, &result);
    return R_SUCCEEDED(rc) && result == RyuLdnConfigResult_Success;
}
