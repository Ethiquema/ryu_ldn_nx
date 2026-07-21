/**
 * @file overlay_state.hpp
 * @brief Singleton holding the overlay's global state (init status, version, dirty flag).
 *
 * Replaces the old global variables g_initState, g_version, g_dirty with
 * a typesafe singleton. Thread safety is NOT guaranteed — the overlay
 * runs on a single thread.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */
#pragma once

#include <cstring>

class OverlayState {
public:
    enum class InitStatus { Uninit, Error, Loaded };

    static OverlayState& Instance() {
        static OverlayState s_instance;
        return s_instance;
    }

    InitStatus GetStatus() const { return m_status; }
    void SetStatus(InitStatus s) { m_status = s; }

    const char* GetVersion() const { return m_version; }
    void SetVersion(const char* v) {
        if (v) {
            strncpy(m_version, v, sizeof(m_version) - 1);
            m_version[sizeof(m_version) - 1] = '\0';
        } else {
            strncpy(m_version, "Unknown", sizeof(m_version) - 1);
            m_version[sizeof(m_version) - 1] = '\0';
        }
    }

    bool IsDirty() const { return m_dirty; }
    void MarkDirty() { m_dirty = true; }
    void MarkSaved() { m_dirty = false; }
    /** Acquire an update lock. Call when entering an editor that modifies values locally. */
    void AcquireUpdateLock() { m_updateLockCount++; }
    /** Release an update lock. Call when leaving the editor (apply, back, cancel). */
    void ReleaseUpdateLock() {
        if (m_updateLockCount > 0) m_updateLockCount--;
    }
    /** Whether update locks are active. */
    bool IsUpdateLocked() const { return m_updateLockCount > 0; }

private:
    OverlayState() = default;
    InitStatus m_status = InitStatus::Uninit;
    /** Counter of active update locks. update() functions should skip IPC sync when > 0. */
    u8 m_updateLockCount = 0;
    char m_version[32] = "Unknown";
    bool m_dirty = false;
};
