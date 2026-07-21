/**
 * @file passphrase_editor_gui.hpp
 * @brief LDN passphrase editor overlay screen - compact horizontal layout.
 *
 * Displays 8 hex characters on a single row with a cursor indicator.
 * ←/→ moves the cursor between positions, ↑/↓ increments/decrements
 * the hex value at the current cursor position (0-9 a-f, wrapping).
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */
#pragma once

#include <tesla.hpp>

class PassphraseEditorGui : public tsl::Gui {
public:
    PassphraseEditorGui();
    virtual tsl::elm::Element* createUI() override;
    virtual void update() override;
    virtual bool handleInput(u64 keysDown, u64 keysHeld,
                             const HidTouchState& touchPos,
                             HidAnalogStickState joyStickPosLeft,
                             HidAnalogStickState joyStickPosRight) override;
    ~PassphraseEditorGui();

private:
    /** Hex value (0-15) for each of the 8 characters. */
    u8 m_charIndices[8] = {0};
    /** Currently selected cursor position (0-7). */
    u8 m_cursorPos = 0;
    /** Frame counter for periodic refresh. */
    u32 m_updateCounter = 0;

    /** Preview line showing the hex passphrase value. */
    tsl::elm::MiniListItem* m_previewItem = nullptr;
    /** Display line showing the 8 hex chars with cursor highlighted. */
    tsl::elm::ListItem* m_charsItem = nullptr;
    /** Cursor indicator line showing spaces and a caret at the selected position. */
    tsl::elm::MiniListItem* m_cursorItem = nullptr;

    static constexpr const char* HEX_CHARS = "0123456789abcdef";

    /** Build the hex string from m_charIndices. */
    std::string BuildHexString();
    /** Build the display string with brackets around the selected char. */
    std::string BuildCursorDisplay();
    /** Build the cursor indicator string with spaces and a caret. */
    std::string BuildCursorIndicator();
    /** Refresh all UI elements to reflect current state. */
    void RefreshDisplay();
    /** Apply the passphrase and go back. */
    void ApplyPassphrase();
};
