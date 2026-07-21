/**
 * @file advanced_settings_gui.cpp
 * @brief Implementation of the advanced settings overlay screen.
 *
 * Contains server info, LDN settings, and debug toggles
 * that were previously on the main dashboard.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "views/settings/advanced_settings_gui.hpp"
#include "app/overlay_state.hpp"

#include "ryu_ldn_ipc.h"

AdvancedSettingsGui::AdvancedSettingsGui() {
}

AdvancedSettingsGui::~AdvancedSettingsGui() {
}

tsl::elm::Element* AdvancedSettingsGui::createUI() {
    auto frame = new tsl::elm::OverlayFrame("Advanced Settings", OverlayState::Instance().GetVersion());
    auto list = new tsl::elm::List();

    if (OverlayState::Instance().GetStatus() == OverlayState::InitStatus::Error) {
        list->addItem(new tsl::elm::ListItem("ryu_ldn_nx not loaded"));
        frame->setContent(list);
        return frame;
    }
    if (OverlayState::Instance().GetStatus() == OverlayState::InitStatus::Uninit) {
        list->addItem(new tsl::elm::ListItem("Initializing..."));
        frame->setContent(list);
        return frame;
    }

    BuildServerSection(list);
    BuildLdnSection(list);
    BuildDebugSection(list);

    frame->setContent(list);
    return frame;
}

void AdvancedSettingsGui::BuildServerSection(tsl::elm::List* list) {
    list->addItem(new tsl::elm::CategoryHeader("Server : Edit config.ini for change address"));
    m_serverItem = new tsl::elm::MiniListItem("Server", "N/A");
    list->addItem(m_serverItem);
}

void AdvancedSettingsGui::BuildLdnSection(tsl::elm::List* list) {
    list->addItem(new tsl::elm::CategoryHeader("P2P Proxy : Warning disable it increase latency"));
    // P2P toggle (inverted: ON = P2P enabled, i.e. disable_p2p=0)
    m_p2pToggle = new tsl::elm::ToggleListItem("P2P Enabled", true);
    RyuLdnConfigService* svc = ryuLdnGetService();
    if (svc) {
        u32 p2pDisabled;
        if (R_SUCCEEDED(ryuLdnGetDisableP2p(svc, &p2pDisabled))) {
            m_p2pToggle->setState(p2pDisabled == 0); // ON when NOT disabled
        }
    }
    m_p2pToggle->setStateChangedListener([this](bool enabled) {
        RyuLdnConfigService* svc2 = ryuLdnGetService();
        if (svc2) {
            // Inverted: enabled=true -> disable_p2p=0, enabled=false -> disable_p2p=1
            Result rc = ryuLdnSetDisableP2p(svc2, enabled ? 0 : 1);
            if (R_FAILED(rc)) {
                m_p2pToggle->setState(!enabled);
                return;
            }
            OverlayState::Instance().MarkDirty();
        }
    });
    list->addItem(m_p2pToggle);
}

void AdvancedSettingsGui::BuildDebugSection(tsl::elm::List* list) {
    list->addItem(new tsl::elm::CategoryHeader("Debug"));
    m_debugToggle = new tsl::elm::ToggleListItem("Debug Enabled", false);
    RyuLdnConfigService* svc = ryuLdnGetService();
    if (svc) {
        u32 debugEnabled;
        if (R_SUCCEEDED(ryuLdnGetDebugEnabled(svc, &debugEnabled))) m_debugToggle->setState(debugEnabled != 0);
    }
    m_debugToggle->setStateChangedListener([this](bool enabled) {
        RyuLdnConfigService* svc = ryuLdnGetService();
        if (svc) {
            Result rc = ryuLdnSetDebugEnabled(svc, enabled ? 1 : 0);
            if (R_FAILED(rc)) {
                m_debugToggle->setState(!enabled);
                return;
            }
            OverlayState::Instance().MarkDirty();
        }
    });
    list->addItem(m_debugToggle);
    m_debugLevelBar = new tsl::elm::NamedStepTrackBar("\uE142",
        {"Error", "Warning", "Info", "Verbose"}, true, "Debug Level");
    if (svc) {
        u32 level;
        if (R_SUCCEEDED(ryuLdnGetDebugLevel(svc, &level)) && level <= 3)
            m_debugLevelBar->setProgress(static_cast<u8>(level));
    }
    m_debugLevelBar->setValueChangedListener([](u8 progress) {
        RyuLdnConfigService* svc = ryuLdnGetService();
        if (svc) { ryuLdnSetDebugLevel(svc, static_cast<u32>(progress)); OverlayState::Instance().MarkDirty(); }
    });
    list->addItem(m_debugLevelBar);
}


void AdvancedSettingsGui::RefreshServerAddress() {
    if (OverlayState::Instance().GetStatus() != OverlayState::InitStatus::Loaded) return;
    RyuLdnConfigService* svc = ryuLdnGetService();
    if (!svc || !m_serverItem) return;
    char host[64];
    u16 port;
    if (R_SUCCEEDED(ryuLdnGetServerAddress(svc, host, &port))) {
        char buf[96];
        snprintf(buf, sizeof(buf), "%s:%u", host, port);
        m_serverItem->setValue(buf);
        // Color: green if connected, grey if disconnected
        RyuLdnConnectionStatus status;
        if (R_SUCCEEDED(ryuLdnGetConnectionStatus(svc, &status)) &&
            (status == RyuLdnStatus_Connected || status == RyuLdnStatus_Ready)) {
            m_serverItem->setValueColor(tsl::RGB888("00FF00"));
        } else {
            m_serverItem->setValueColor(tsl::RGB888("888888"));
        }
    }
}

void AdvancedSettingsGui::update() {
    m_refreshCounter++;
    if (m_refreshCounter >= 60) {
        m_refreshCounter = 0;
        // Always refresh server address (read-only display, not affected by edit locks)
        RefreshServerAddress();
    }
}

bool AdvancedSettingsGui::handleInput(u64 keysDown, u64 keysHeld,
                                       const HidTouchState& touchPos,
                                       HidAnalogStickState joyStickPosLeft,
                                       HidAnalogStickState joyStickPosRight) {
    if (keysDown & HidNpadButton_R) { RefreshServerAddress(); return true; }
    return false;
}
