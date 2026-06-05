/**
 * @file main_dashboard_gui.hpp
 * @brief Main dashboard overlay screen (connection + passphrase + nav).
 *
 * Displays:
 * - Connection section: status, LDN state, session info
 * - Passphrase section: display + edit button
 * - Navigation: advanced settings button
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */
#pragma once

#include <tesla.hpp>

class MainDashboardGui : public tsl::Gui {
public:
    MainDashboardGui();
    virtual tsl::elm::Element* createUI() override;
    virtual void update() override;
    virtual bool handleInput(u64 keysDown, u64 keysHeld,
                             const HidTouchState& touchPos,
                             HidAnalogStickState joyStickPosLeft,
                             HidAnalogStickState joyStickPosRight) override;

private:
    void BuildConnectionSection(tsl::elm::List* list);
    void BuildPassphraseSection(tsl::elm::List* list);
    void BuildNavSection(tsl::elm::List* list);

    void RefreshConnectionValues();
    void RefreshPassphrase();

    tsl::elm::ListItemV2* m_statusItem = nullptr;
    tsl::elm::ListItemV2* m_ldnStateItem = nullptr;
    tsl::elm::MiniListItem* m_sessionInfoItem = nullptr;
    tsl::elm::MiniListItem* m_passphraseItem = nullptr;

    u32 m_refreshCounter = 0;
};
