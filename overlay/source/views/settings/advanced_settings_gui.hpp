/**
 * @file advanced_settings_gui.hpp
 * @brief Advanced settings overlay screen (server, LDN, debug).
 *
 * Shows settings removed from the main dashboard:
 * - Server address display (config file only)
 * - LDN enabled toggle
 * - Debug enabled toggle + debug level bar
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */
#pragma once

#include <tesla.hpp>

class AdvancedSettingsGui : public tsl::Gui {
public:
    AdvancedSettingsGui();
    virtual tsl::elm::Element* createUI() override;
    virtual void update() override;
    virtual bool handleInput(u64 keysDown, u64 keysHeld,
                             const HidTouchState& touchPos,
                             HidAnalogStickState joyStickPosLeft,
                             HidAnalogStickState joyStickPosRight) override;
    ~AdvancedSettingsGui();

private:
    void BuildServerSection(tsl::elm::List* list);
    void BuildLdnSection(tsl::elm::List* list);
    void BuildDebugSection(tsl::elm::List* list);
    void RefreshServerAddress();

    tsl::elm::MiniListItem* m_serverItem = nullptr;
    tsl::elm::ToggleListItem* m_p2pToggle = nullptr;
    tsl::elm::ToggleListItem* m_debugToggle = nullptr;
    tsl::elm::NamedStepTrackBar* m_debugLevelBar = nullptr;
    u32 m_refreshCounter = 0;
};
