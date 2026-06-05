/**
 * @file passphrase_editor_gui.cpp
 * @brief Implementation of the LDN passphrase editor overlay screen.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "views/passphrase/passphrase_editor_gui.hpp"
#include "app/overlay_state.hpp"
#include "ryu_ldn_ipc.h"
#include <cstring>
#include <string>

PassphraseEditorGui::PassphraseEditorGui() {
    for (int i = 0; i < 8; i++) m_charIndices[i] = 0;
    if (OverlayState::Instance().GetStatus() == OverlayState::InitStatus::Loaded) {
        RyuLdnConfigService* svc = ryuLdnGetService();
        if (svc) {
            char passphrase[64];
            if (R_SUCCEEDED(ryuLdnGetPassphrase(svc, passphrase))) {
                if (strlen(passphrase) == 16 && strncmp(passphrase, "Ryujinx-", 8) == 0) {
                    const char* hex = HEX_CHARS;
                    for (int i = 0; i < 8; i++) {
                        char c = passphrase[8 + i];
                        const char* pos = strchr(hex, c);
                        if (pos) m_charIndices[i] = static_cast<u8>(pos - hex);
                    }
                }
            }
        }
    }
    OverlayState::Instance().AcquireUpdateLock();
}

PassphraseEditorGui::~PassphraseEditorGui() {
    OverlayState::Instance().ReleaseUpdateLock();
}

tsl::elm::Element* PassphraseEditorGui::createUI() {
    auto frame = new tsl::elm::OverlayFrame("Edit Passphrase", "Hex: 0-9, a-f");
    auto list = new tsl::elm::List();
    if (OverlayState::Instance().GetStatus() != OverlayState::InitStatus::Loaded) {
        list->addItem(new tsl::elm::ListItem("Not available"));
        frame->setContent(list);
        return frame;
    }
    list->addItem(new tsl::elm::CategoryHeader("Current"));
    m_previewItem = new tsl::elm::MiniListItem("Passphrase", BuildHexString());
    list->addItem(m_previewItem);

    list->addItem(new tsl::elm::CategoryHeader("Edit (L/R move, U/D change)"));

    // Line showing the 8 chars with cursor highlighted
    m_charsItem = new tsl::elm::ListItem(BuildCursorDisplay());
    list->addItem(m_charsItem);

    // Caret line — spaces with ^ under the selected position
    m_cursorItem = new tsl::elm::MiniListItem("", BuildCursorIndicator());
    list->addItem(m_cursorItem);

    list->addItem(new tsl::elm::CategoryHeader("Actions"));

    list->addItem(new tsl::elm::DummyListItem());

    auto applyItem = new tsl::elm::ListItem("Apply");
    applyItem->setValue("Press A / +");
    applyItem->setClickListener([this](u64 keys) {
        if (keys & HidNpadButton_A) { ApplyPassphrase(); return true; }
        return false;
    });
    list->addItem(applyItem);
    frame->setContent(list);
    return frame;
}

void PassphraseEditorGui::update() {
    m_updateCounter++;
    if (m_updateCounter % 60 != 0) return;
    // Skip live sync while an editor holds the update lock
    if (OverlayState::Instance().IsUpdateLocked()) return;
    if (OverlayState::Instance().GetStatus() != OverlayState::InitStatus::Loaded) return;
    RyuLdnConfigService* svc = ryuLdnGetService();
    if (!svc) return;
    char passphrase[64];
    if (R_SUCCEEDED(ryuLdnGetPassphrase(svc, passphrase))) {
        if (strlen(passphrase) == 16 && strncmp(passphrase, "Ryujinx-", 8) == 0) {
            const char* hex = HEX_CHARS;
            for (int i = 0; i < 8; i++) {
                char c = passphrase[8 + i];
                const char* pos = strchr(hex, c);
                u8 newIndex = pos ? static_cast<u8>(pos - hex) : 0;
                if (m_charIndices[i] != newIndex) {
                    m_charIndices[i] = newIndex;
                }
            }
            RefreshDisplay();
        }
    }
}

bool PassphraseEditorGui::handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos,
                                       HidAnalogStickState joyStickPosLeft,
                                       HidAnalogStickState joyStickPosRight) {
    // Left/Right: move cursor
    if (keysDown & HidNpadButton_Left) {
        if (m_cursorPos > 0) m_cursorPos--;
        else m_cursorPos = 7;  // wrap around
        RefreshDisplay();
        return true;
    }
    if (keysDown & HidNpadButton_Right) {
        if (m_cursorPos < 7) m_cursorPos++;
        else m_cursorPos = 0;  // wrap around
        RefreshDisplay();
        return true;
    }
    // Up/Down: change hex value with wrap
    if (keysDown & HidNpadButton_Up) {
        m_charIndices[m_cursorPos] = (m_charIndices[m_cursorPos] + 1) % 16;
        RefreshDisplay();
        return true;
    }
    if (keysDown & HidNpadButton_Down) {
        m_charIndices[m_cursorPos] = (m_charIndices[m_cursorPos] == 0) ? 15 : (m_charIndices[m_cursorPos] - 1);
        RefreshDisplay();
        return true;
    }
    // Plus = Apply
    if (keysDown & HidNpadButton_Plus) { ApplyPassphrase(); return true; }
    return false;
}

std::string PassphraseEditorGui::BuildHexString() {
    char buf[9];
    for (int i = 0; i < 8; i++) buf[i] = HEX_CHARS[m_charIndices[i]];
    buf[8] = 0;
    return std::string(buf);
}

std::string PassphraseEditorGui::BuildCursorDisplay() {
    // Example: "[a] 3 f 7 9 b 2 c" with the selected char in brackets
    std::string result;
    for (int i = 0; i < 8; i++) {
        if (i == m_cursorPos) result += '[';
        result += HEX_CHARS[m_charIndices[i]];
        if (i == m_cursorPos) result += ']';
        if (i < 7) result += ' ';
    }
    return result;
}

std::string PassphraseEditorGui::BuildCursorIndicator() {
    // Spaces with a ^ under the selected position
    // Each char takes 2 chars ("X "), the bracket adds 1 before for '['
    std::string result;
    for (int i = 0; i < m_cursorPos; i++) {
        result += "   ";  // "X] " or "[X " — 3 chars per position
    }
    result += " ^ ";  // caret under the selected position
    return result;
}

void PassphraseEditorGui::RefreshDisplay() {
    if (m_previewItem) m_previewItem->setValue(BuildHexString());
    if (m_charsItem) m_charsItem->setText(BuildCursorDisplay());
    if (m_cursorItem) m_cursorItem->setText(BuildCursorIndicator());
}

void PassphraseEditorGui::ApplyPassphrase() {
    RyuLdnConfigService* svc = ryuLdnGetService();
    if (!svc) return;
    char full[32];
    snprintf(full, sizeof(full), "Ryujinx-%s", BuildHexString().c_str());
    Result rc = ryuLdnSetPassphrase(svc, full);
    if (R_SUCCEEDED(rc)) OverlayState::Instance().MarkDirty();
    OverlayState::Instance().ReleaseUpdateLock();
    tsl::goBack();
}
