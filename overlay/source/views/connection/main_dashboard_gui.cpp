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
#include <ctime>

// Initialize random seed for passphrase generation
namespace {
    struct RandomSeeder { RandomSeeder() { srand(time(nullptr)); } };
    RandomSeeder g_seeder;
}

MainDashboardGui::MainDashboardGui() = default;

tsl::elm::Element* MainDashboardGui::createUI() {
    auto frame = new tsl::elm::OverlayFrame("ryu_ldn_nx", OverlayState::Instance().GetVersion());
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
    BuildPassphraseSection(list);
    BuildNavSection(list);


    frame->setContent(list);
    return frame;
}

void MainDashboardGui::BuildConnectionSection(tsl::elm::List* list) {
    list->addItem(new tsl::elm::CategoryHeader("Connection"));
    m_statusItem = new tsl::elm::ListItemV2("Status", "N/A");
    list->addItem(m_statusItem);
    m_ldnStateItem = new tsl::elm::ListItemV2("LDN State", "None");
    list->addItem(m_ldnStateItem);
    m_sessionInfoItem = new tsl::elm::MiniListItem("Session", "N/A");
    list->addItem(m_sessionInfoItem);
}

void MainDashboardGui::BuildPassphraseSection(tsl::elm::List* list) {
    list->addItem(new tsl::elm::CategoryHeader("Passphrase  (X=Random Y=Clear)"));
    m_passphraseItem = new tsl::elm::MiniListItem("Passphrase", "---");
    list->addItem(m_passphraseItem);
    auto editItem = new tsl::elm::ListItem("Edit Passphrase");
    editItem->setValue(">");
    editItem->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) { tsl::changeTo<PassphraseEditorGui>(); return true; }
        return false;
    });
    list->addItem(editItem);
}

void MainDashboardGui::BuildNavSection(tsl::elm::List* list) {
    auto advancedItem = new tsl::elm::ListItem("Advanced Settings");
    advancedItem->setValue(">");
    advancedItem->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) { tsl::changeTo<AdvancedSettingsGui>(); return true; }
        return false;
    });
    list->addItem(advancedItem);
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
        m_statusItem->setValueColorOverride(ryu_ldn::overlay::StatusColor(derivedStatus));
    }
    if (m_ldnStateItem) {
        RyuLdnState state;
        if (R_SUCCEEDED(ryuLdnGetLdnState(svc, &state))) {
            m_ldnStateItem->setValue(ryuLdnStateToString(state));
            m_ldnStateItem->setValueColorOverride(ryu_ldn::overlay::LdnStateColor(state));
        }
    }
    if (m_sessionInfoItem) {
        RyuLdnSessionInfo info;
        if (R_SUCCEEDED(ryuLdnGetSessionInfo(svc, &info))) {
            if (info.node_count == 0) m_sessionInfoItem->setValue("Not in session");
            else {
                char buf[48];
                snprintf(buf, sizeof(buf), "%d/%d (%s)",
                         info.node_count, info.max_nodes,
                         info.is_host ? "Host" : "Client");
                m_sessionInfoItem->setValue(buf);
            }
        }
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
    }
}

void MainDashboardGui::update() {
    m_refreshCounter++;
    if (m_refreshCounter >= 60) {
        m_refreshCounter = 0;
        RefreshConnectionValues();
        RefreshPassphrase();
    }
    // Auto-save display removed per user request
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
            if (R_SUCCEEDED(rc)) OverlayState::Instance().MarkDirty();
            RefreshPassphrase();
        }
        return true;
    }
    return false;
}
