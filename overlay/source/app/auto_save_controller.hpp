/**
 * @file auto_save_controller.hpp
 * @brief Auto-save logic extracted from the main dashboard.
 *
 * Manages two counters:
 * - m_autoSaveCounter: counts frames while dirty, triggers save after
 *   AutoSaveDelayFrames (60 frames ~ 1 s at 60 fps).
 * - m_saveConfirmCounter: counts frames after successful save to show
 *   "Saved!" indicator for ConfirmDurationFrames (120 frames ~ 2 s).
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */
#pragma once

#include <string>

#include <switch.h>

#include "ryu_ldn_ipc.h"
#include "app/overlay_state.hpp"

/** @brief Persist config via IPC. Returns true on success. */
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

class AutoSaveController {
public:
    /** @brief Call once per frame. Marks OverlayState as saved if auto-save fires. */
    void Update(bool isDirty) {
        if (isDirty) {
            // Skip auto-save if LDN session is active (prevents memory bugs)
            if (!IsLdnSessionActive()) {
                m_autoSaveCounter++;
                if (m_autoSaveCounter >= AutoSaveDelayFrames) {
                    m_autoSaveCounter = 0;
                    if (DoSaveConfig()) {
                        m_saveConfirmCounter = ConfirmDurationFrames;
                    }
                }
            } else {
                m_autoSaveCounter = 0;
            }
        } else {
            m_autoSaveCounter = 0;
        }
        if (m_saveConfirmCounter > 0) {
            m_saveConfirmCounter--;
        }
    }

    /** @brief Force an immediate save attempt. */
    void ForceSave() {
        m_autoSaveCounter = 0;
        if (DoSaveConfig()) {
            m_saveConfirmCounter = ConfirmDurationFrames;
        }
    }

    /** @brief Reset confirm counter (e.g. after reload). */
    void ResetConfirm() {
        m_saveConfirmCounter = 0;
    }

    /** @brief Whether the "Saved!" flash indicator should be visible. */
    bool IsConfirmVisible() const {
        return m_saveConfirmCounter > 0;
    }

    /** @brief Get the text for the config dirty indicator. */
    std::string GetIndicatorText() const {
        bool isDirty = OverlayState::Instance().IsDirty();
        if (m_saveConfirmCounter > 0) {
            return "Saved!";
        }
        return isDirty ? "\u26A0 Unsaved changes" : "Saved";
    }

private:
    /** @brief Check if an LDN session is currently active. */
    bool IsLdnSessionActive() const {
        RyuLdnConfigService* svc = ryuLdnGetService();
        if (!svc) return false;
        u32 gameActive;
        if (R_FAILED(ryuLdnIsGameActive(svc, &gameActive))) return false;
        return gameActive != 0;
    }
    u32 m_autoSaveCounter = 0;
    u32 m_saveConfirmCounter = 0;
    static constexpr u32 AutoSaveDelayFrames = 60; // 1 s at 60 fps
    static constexpr u32 ConfirmDurationFrames = 120;
};
