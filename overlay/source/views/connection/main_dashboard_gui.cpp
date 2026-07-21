/**
 * @file main_dashboard_gui.cpp
 * @brief Implementation of the main dashboard overlay screen.
 *
 * Simplified dashboard showing only connection info, passphrase,
 * and a navigation button to advanced settings.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "views/connection/main_dashboard_gui.hpp"
#include "views/passphrase/passphrase_editor_gui.hpp"
#include "views/settings/advanced_settings_gui.hpp"
#include "utils/formatters.hpp"
#include "app/overlay_state.hpp"

#include "ryu_ldn_ipc.h"
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

// Initialize random seed for passphrase generation
namespace {
    struct RandomSeeder { RandomSeeder() { srand(time(nullptr)); } };
    RandomSeeder g_seeder;
}

MainDashboardGui::MainDashboardGui() = default;

tsl::elm::Element* MainDashboardGui::createUI() {
    auto frame = new tsl::elm::OverlayFrame("ryu_ldn_nx", OverlayState::Instance().GetVersion());
    m_frame = frame;
    auto list = new tsl::elm::List();

    if (OverlayState::Instance().GetStatus() == OverlayState::InitStatus::Error) {
        list->addItem(new tsl::elm::ListItem("ryu_ldn_nx not loaded"));
        list->addItem(new tsl::elm::ListItem("Check sysmodule installation"));
        frame->setContent(list);
        return frame;
    }
    if (OverlayState::Instance().GetStatus() == OverlayState::InitStatus::Uninit) {
        list->addItem(new tsl::elm::ListItem("Initializing..."));
        frame->setContent(list);
        return frame;
    }

    BuildConnectionSection(list);
    BuildLdnSection(list);
    BuildPassphraseSection(list);
    BuildNavSection(list);


    frame->setContent(list);
    return frame;
}

void MainDashboardGui::BuildConnectionSection(tsl::elm::List* list) {
    list->addItem(new tsl::elm::CategoryHeader("Connection"));
    m_statusItem = new tsl::elm::MiniListItem("Status", "N/A");
    list->addItem(m_statusItem);
    m_passphraseItem = new tsl::elm::MiniListItem("Passphrase", "---");
    list->addItem(m_passphraseItem);
}

void MainDashboardGui::BuildPassphraseSection(tsl::elm::List* list) {
    list->addItem(new tsl::elm::CategoryHeader("Passphrase (X=Random Y=Clear)"));
    // Use Passphrase toggle (copied from AdvancedSettingsGui::BuildLdnSection)
    bool passphraseEnabled = false;
    bool hasPassphrase = false;
    RyuLdnConfigService* ldnSvc = ryuLdnGetService();
    if (ldnSvc) {
        u32 usePassphrase = 0;
        if (R_SUCCEEDED(ryuLdnGetUsePassphrase(ldnSvc, &usePassphrase)))
            passphraseEnabled = (usePassphrase != 0);
        char passphrase[64] = {0};
        if (R_SUCCEEDED(ryuLdnGetPassphrase(ldnSvc, passphrase)))
            hasPassphrase = (strlen(passphrase) > 0);
    }
    if (!hasPassphrase) {
        passphraseEnabled = false;
    }
    m_passphraseEnabledToggle = new tsl::elm::ToggleListItem("Use Passphrase", passphraseEnabled);
    if (!hasPassphrase) {
        m_passphraseEnabledToggle->isLocked = true;
    }
    m_passphraseEnabledToggle->setStateChangedListener([this](bool enabled) {
        RyuLdnConfigService* svc = ryuLdnGetService();
        if (svc) {
            // Prevent enabling the toggle when no passphrase is set.
            // The isLocked flag only blocks the visual toggle, not the
            // callback, so without this check the IPC call would store
            // use_passphrase=1 in the sysmodule. When the passphrase later
            // becomes non-empty, update() unlocks and syncs to that stored
            // ON value, causing the toggle to flip on automatically.
            if (enabled) {
                char passphrase[64] = {0};
                if (R_SUCCEEDED(ryuLdnGetPassphrase(svc, passphrase)) && strlen(passphrase) == 0) {
                    m_passphraseEnabledToggle->setState(false);
                    return;
                }
            }
            Result rc = ryuLdnSetUsePassphrase(svc, enabled ? 1 : 0);
            if (R_FAILED(rc)) {
                m_passphraseEnabledToggle->setState(!enabled);
                return;
            }
            OverlayState::Instance().MarkDirty();
        }
    });
    list->addItem(m_passphraseEnabledToggle);
    auto editItem = new tsl::elm::MiniListItem("Edit Passphrase");
    editItem->setValue(">");
    editItem->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) { tsl::changeTo<PassphraseEditorGui>(); return true; }
        return false;
    });
    list->addItem(editItem);
}

void MainDashboardGui::BuildNavSection(tsl::elm::List* list) {
    list->addItem(new tsl::elm::CategoryHeader("Advanced Settings"));
    auto advancedItem = new tsl::elm::MiniListItem("Open Advanced Settings");
    advancedItem->setValue(">");
    advancedItem->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) { tsl::changeTo<AdvancedSettingsGui>(); return true; }
        return false;
    });
    list->addItem(advancedItem);
}

void MainDashboardGui::BuildLdnSection(tsl::elm::List* list) {
    list->addItem(new tsl::elm::CategoryHeader("LDN Settings"));

    // LDN Enabled toggle (copied from AdvancedSettingsGui::BuildLdnSection)
    m_ldnToggle = new tsl::elm::ToggleListItem("LDN Enabled", true);
    RyuLdnConfigService* svc = ryuLdnGetService();
    if (svc) {
        u32 ldnEnabled;
        if (R_SUCCEEDED(ryuLdnGetLdnEnabled(svc, &ldnEnabled))) m_ldnToggle->setState(ldnEnabled != 0);
    }
    m_ldnToggle->setStateChangedListener([this](bool enabled) {
        RyuLdnConfigService* svc = ryuLdnGetService();
        if (svc) {
            Result rc = ryuLdnSetLdnEnabled(svc, enabled ? 1 : 0);
            if (R_FAILED(rc)) {
                m_ldnToggle->setState(!enabled);
                return;
            }
            OverlayState::Instance().MarkDirty();
        }
    });
    list->addItem(m_ldnToggle);
}

void MainDashboardGui::RefreshConnectionValues() {
    if (OverlayState::Instance().GetStatus() != OverlayState::InitStatus::Loaded) return;
    RyuLdnConfigService* svc = ryuLdnGetService();
    if (!svc) return;

    if (m_statusItem) {
        // Connection Status is derived from LDN State (ryu:cfg GetConnectionStatus is hardcoded)
        RyuLdnState ldnState;
        RyuLdnConnectionStatus derivedStatus = RyuLdnStatus_Disconnected;
        if (R_SUCCEEDED(ryuLdnGetLdnState(svc, &ldnState))) {
            switch (ldnState) {
                case RyuLdnState_AccessPointCreated:
                case RyuLdnState_StationConnected:
                    derivedStatus = RyuLdnStatus_Ready;
                    break;
                case RyuLdnState_AccessPoint:
                case RyuLdnState_Station:
                    derivedStatus = RyuLdnStatus_Connected;
                    break;
                case RyuLdnState_Initialized:
                    derivedStatus = RyuLdnStatus_Connecting;
                    break;
                default:
                    derivedStatus = RyuLdnStatus_Disconnected;
                    break;
            }
        }
        m_statusItem->setValue(ryu_ldn::overlay::ConnectionStatusToString(derivedStatus));
        m_statusItem->setValueColor(ryu_ldn::overlay::StatusColor(derivedStatus));
    }
    // Build compact session info for subtitle: "2/8" or "2/8 (Host)"
    {
        std::string info;
        char buf[64];

        RyuLdnSessionInfo sessionInfo;
        if (R_SUCCEEDED(ryuLdnGetSessionInfo(svc, &sessionInfo))) {
            if (sessionInfo.node_count > 0) {
                snprintf(buf, sizeof(buf), "%d/%d%s",
                         sessionInfo.node_count, sessionInfo.max_nodes,
                         sessionInfo.is_host ? " (Host)" : "");
            } else {
                snprintf(buf, sizeof(buf), "--");
            }
            info = buf;
        }

        m_cachedInfoSubtitle = info;
    }
}

void MainDashboardGui::RefreshPassphrase() {
    if (OverlayState::Instance().GetStatus() != OverlayState::InitStatus::Loaded) return;
    RyuLdnConfigService* svc = ryuLdnGetService();
    if (!svc || !m_passphraseItem) return;
    char passphrase[64];
    if (R_SUCCEEDED(ryuLdnGetPassphrase(svc, passphrase))) {
        char display[32];
        ryu_ldn::overlay::FormatPassphraseDisplay(passphrase, display, sizeof(display));
        m_passphraseItem->setValue(display);
        if (strlen(passphrase) > 0) m_passphraseItem->setValueColor(tsl::RGB888("00FF00"));
        else                        m_passphraseItem->setValueColor(tsl::RGB888("888888"));
    }
}

void MainDashboardGui::update() {
    m_refreshCounter++;
    if (m_refreshCounter >= 60) {
        m_refreshCounter = 0;
        RefreshConnectionValues();
        RefreshPassphrase();
    }
    m_autoSaveController.Update(OverlayState::Instance().IsDirty());

    // Dynamic subtitle: connection status + save indicator
    if (m_frame) {
        RyuLdnConfigService* svc = ryuLdnGetService();
        RyuLdnConnectionStatus currentStatus = RyuLdnStatus_Disconnected;
        if (svc) {
            RyuLdnState ldnState;
            if (R_SUCCEEDED(ryuLdnGetLdnState(svc, &ldnState))) {
                switch (ldnState) {
                    case RyuLdnState_AccessPointCreated:
                    case RyuLdnState_StationConnected:
                        currentStatus = RyuLdnStatus_Ready;
                        break;
                    case RyuLdnState_AccessPoint:
                    case RyuLdnState_Station:
                        currentStatus = RyuLdnStatus_Connected;
                        break;
                    case RyuLdnState_Initialized:
                        currentStatus = RyuLdnStatus_Connecting;
                        break;
                    case RyuLdnState_Error:
                        currentStatus = RyuLdnStatus_Error;
                        break;
                    default:
                        currentStatus = RyuLdnStatus_Disconnected;
                        break;
                }
            }
        }

        // Build subtitle: status dot + save indicator + info line
        std::string subtitle;
        switch (currentStatus) {
            case RyuLdnStatus_Ready:
            case RyuLdnStatus_Connected:
                subtitle = "\u25CF Connected";
                break;
            case RyuLdnStatus_Connecting:
                subtitle = "\u25CF Connecting";
                break;
            case RyuLdnStatus_Error:
                subtitle = "\u25CF Error";
                break;
            default:
                subtitle = "\u25CF Disconnected";
                break;
        }
        std::string indicator = m_autoSaveController.GetIndicatorText();
        if (!indicator.empty()) {
            subtitle += " \u2014 ";
            subtitle += indicator;
        }
        if (!m_cachedInfoSubtitle.empty()) {
            subtitle += " | ";
            subtitle += m_cachedInfoSubtitle;
        }
        m_frame->setSubtitle(subtitle);
    }

    // Refresh passphrase toggle: lock/unlock and sync state when passphrase changes
    if (m_passphraseEnabledToggle) {
        RyuLdnConfigService* svc = ryuLdnGetService();
        if (svc) {
            char passphrase[64] = {0};
            bool hasPassphrase = false;
            if (R_SUCCEEDED(ryuLdnGetPassphrase(svc, passphrase)))
                hasPassphrase = (strlen(passphrase) > 0);
            if (!hasPassphrase) {
                m_passphraseEnabledToggle->setState(false);
                m_passphraseEnabledToggle->isLocked = true;
            } else {
                m_passphraseEnabledToggle->isLocked = false;
                u32 usePassphrase = 0;
                if (R_SUCCEEDED(ryuLdnGetUsePassphrase(svc, &usePassphrase)))
                    m_passphraseEnabledToggle->setState(usePassphrase != 0);
            }
        }
    }
}

bool MainDashboardGui::handleInput(u64 keysDown, u64 keysHeld,
                                    const HidTouchState& touchPos,
                                    HidAnalogStickState joyStickPosLeft,
                                    HidAnalogStickState joyStickPosRight) {
    if (keysDown & HidNpadButton_R) { RefreshConnectionValues(); RefreshPassphrase(); return true; }
    // X = Generate random passphrase
    if (keysDown & HidNpadButton_X) {
        RyuLdnConfigService* svc = ryuLdnGetService();
        if (svc) {
            char newPass[32];
            // Generate Ryujinx-XXXXXXXX with random hex
            newPass[0] = 'R'; newPass[1] = 'y'; newPass[2] = 'u'; newPass[3] = 'j';
            newPass[4] = 'i'; newPass[5] = 'n'; newPass[6] = 'x'; newPass[7] = '-';
            static const char* hex = "0123456789abcdef";
            for (int i = 0; i < 8; i++) {
                newPass[8 + i] = hex[rand() % 16];
            }
            newPass[16] = '\0';
            Result rc = ryuLdnSetPassphrase(svc, newPass);
            if (R_SUCCEEDED(rc)) OverlayState::Instance().MarkDirty();
            RefreshPassphrase();
        }
        return true;
    }
    // Y = Clear passphrase
    if (keysDown & HidNpadButton_Y) {
        RyuLdnConfigService* svc = ryuLdnGetService();
        if (svc) {
            Result rc = ryuLdnSetPassphrase(svc, "");
            ryuLdnSetUsePassphrase(svc, 0);
            if (R_SUCCEEDED(rc)) OverlayState::Instance().MarkDirty();
            RefreshPassphrase();
        }
        return true;
    }
    return false;
}
