/**
 * @file main.cpp
 * @brief ryu_ldn_nx Tesla Overlay
 *
 * Provides a user interface for:
 * - Viewing connection status to ryu_ldn server
 * - Viewing current session information
 * - Changing server address
 * - Toggling debug mode
 *
 * ## Usage
 * 1. Press L+DDOWN+RSTICK to open Tesla Menu
 * 2. Select "ryu_ldn_nx" from the overlay list
 * 3. View status and configure options
 *
 * ## Requirements
 * - Tesla Menu (nx-ovlloader) installed
 * - ryu_ldn_nx sysmodule running
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include "ryu_ldn_ipc.h"

//=============================================================================
// Global State
//=============================================================================

/// Initialization state
enum class InitState {
    Uninit,     ///< Not yet initialized
    Error,      ///< Failed to connect to sysmodule
    Loaded,     ///< Successfully connected
};

/// Global state
InitState g_initState = InitState::Uninit;
char g_version[32] = "Unknown";

//=============================================================================
// Helper Functions
//=============================================================================

/**
 * @brief Convert connection status to string
 */
const char* ConnectionStatusToString(RyuLdnConnectionStatus status) {
    switch (status) {
        case RyuLdnStatus_Disconnected: return "Disconnected";
        case RyuLdnStatus_Connecting:   return "Connecting...";
        case RyuLdnStatus_Connected:    return "Connected";
        case RyuLdnStatus_Ready:        return "Ready";
        case RyuLdnStatus_Error:        return "Error";
        default:                        return "Unknown";
    }
}

/**
 * @brief Get color for connection status
 */
tsl::Color StatusColor(RyuLdnConnectionStatus status) {
    switch (status) {
        case RyuLdnStatus_Ready:        return tsl::Color(0x0, 0xF, 0x0, 0xF);  // Green
        case RyuLdnStatus_Connected:    return tsl::Color(0x0, 0xF, 0x0, 0xF);  // Green
        case RyuLdnStatus_Connecting:   return tsl::Color(0xF, 0xF, 0x0, 0xF);  // Yellow
        case RyuLdnStatus_Disconnected: return tsl::Color(0x8, 0x8, 0x8, 0xF);  // Gray
        case RyuLdnStatus_Error:        return tsl::Color(0xF, 0x0, 0x0, 0xF);  // Red
        default:                        return tsl::Color(0xF, 0xF, 0xF, 0xF);  // White
    }
}

/**
 * @brief Format debug level for display
 */
const char* DebugLevelToString(u32 level) {
    switch (level) {
        case 0: return "Error";
        case 1: return "Warning";
        case 2: return "Info";
        case 3: return "Verbose";
        default: return "Unknown";
    }
}

/**
 * @brief Format timeout for display
 */
void FormatTimeout(u32 timeout_ms, char* buf, size_t bufSize) {
    if (timeout_ms < 1000) {
        snprintf(buf, bufSize, "%u ms", timeout_ms);
    } else {
        snprintf(buf, bufSize, "%.1f s", timeout_ms / 1000.0);
    }
}

/**
 * @brief Format passphrase for display
 *
 * Shows only the hex part (without "Ryujinx-" prefix) or "(not set)".
 */
void FormatPassphraseDisplay(const char* passphrase, char* buf, size_t bufSize) {
    if (passphrase == nullptr || passphrase[0] == '\0') {
        snprintf(buf, bufSize, "(not set)");
    } else if (strlen(passphrase) == 16 && strncmp(passphrase, "Ryujinx-", 8) == 0) {
        // Show only the hex part
        snprintf(buf, bufSize, "%s", passphrase + 8);
    } else {
        snprintf(buf, bufSize, "(invalid)");
    }
}

/**
 * @brief Generate a random passphrase (overlay-side)
 *
 * Generates format: Ryujinx-[0-9a-f]{8}
 */
void GenerateRandomPassphraseOverlay(char* out, size_t out_size) {
    if (out == nullptr || out_size < 17) {
        if (out != nullptr && out_size > 0) out[0] = '\0';
        return;
    }

    static bool seeded = false;
    if (!seeded) {
        srand(static_cast<unsigned>(time(nullptr)));
        seeded = true;
    }

    const char* hex_chars = "0123456789abcdef";
    strcpy(out, "Ryujinx-");
    for (int i = 0; i < 8; i++) {
        out[8 + i] = hex_chars[rand() % 16];
    }
    out[16] = '\0';
}

// Forward declarations for submenus
class ServerSettingsGui;
class NetworkSettingsGui;
class LdnSettingsGui;
class DebugSettingsGui;
class HexKeyboardGui;

//=============================================================================
// Hex Keyboard GUI
//=============================================================================

/**
 * @brief Hexadecimal keyboard for passphrase input
 *
 * Displays a 4x4 grid of hex characters (0-9, a-f) for entering
 * the 8-character hex suffix of the passphrase.
 *
 * The "Ryujinx-" prefix is automatically added when saving.
 *
 * Controls:
 * - D-Pad: Navigate between keys
 * - A: Press selected key / confirm
 * - B: Backspace / cancel
 * - X: Clear all
 * - +: Confirm and save
 */
class HexKeyboardGui : public tsl::Gui {
public:
    HexKeyboardGui() : m_cursorX(0), m_cursorY(0), m_inputLen(0) {
        memset(m_input, 0, sizeof(m_input));

        // Load current passphrase (extract hex part only)
        RyuLdnConfigService* svc = ryuLdnGetService();
        if (svc) {
            char current[64];
            if (R_SUCCEEDED(ryuLdnGetPassphrase(svc, current))) {
                if (strlen(current) == 16 && strncmp(current, "Ryujinx-", 8) == 0) {
                    memcpy(m_input, current + 8, 8);
                    m_input[8] = '\0';
                    m_inputLen = 8;
                }
            }
        }
    }

    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("Edit Passphrase", "Hex only (0-9, a-f)");
        auto list = new tsl::elm::List();

        // Instructions
        list->addItem(new tsl::elm::CategoryHeader("Enter 8 hex characters"));

        // Current input display
        m_inputDisplay = new tsl::elm::ListItem("Passphrase");
        UpdateInputDisplay();
        list->addItem(m_inputDisplay);

        list->addItem(new tsl::elm::CategoryHeader("Keyboard"));

        // Display keyboard hint
        auto hintItem = new tsl::elm::ListItem("Use D-Pad + A to type");
        hintItem->setValue("[B]=Back [X]=Clear");
        list->addItem(hintItem);

        // Show keyboard layout as text (Tesla doesn't support custom grid drawing easily)
        auto row1 = new tsl::elm::ListItem("Row 1");
        row1->setValue("0 1 2 3");
        list->addItem(row1);

        auto row2 = new tsl::elm::ListItem("Row 2");
        row2->setValue("4 5 6 7");
        list->addItem(row2);

        auto row3 = new tsl::elm::ListItem("Row 3");
        row3->setValue("8 9 a b");
        list->addItem(row3);

        auto row4 = new tsl::elm::ListItem("Row 4");
        row4->setValue("c d e f");
        list->addItem(row4);

        // Selected key indicator
        m_selectedKeyItem = new tsl::elm::ListItem("Selected");
        UpdateSelectedKeyDisplay();
        list->addItem(m_selectedKeyItem);

        list->addItem(new tsl::elm::CategoryHeader("Actions"));

        // Confirm button
        auto confirmItem = new tsl::elm::ListItem("Save Passphrase");
        confirmItem->setValue("[+] or press here");
        list->addItem(confirmItem);

        // Clear button
        auto clearItem = new tsl::elm::ListItem("Clear Passphrase");
        clearItem->setValue("Set to empty");
        list->addItem(clearItem);

        frame->setContent(list);
        return frame;
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos,
                             HidAnalogStickState joyStickPosLeft,
                             HidAnalogStickState joyStickPosRight) override {
        // D-Pad navigation
        if (keysDown & HidNpadButton_Up) {
            m_cursorY = (m_cursorY + 3) % 4;
            UpdateSelectedKeyDisplay();
            return true;
        }
        if (keysDown & HidNpadButton_Down) {
            m_cursorY = (m_cursorY + 1) % 4;
            UpdateSelectedKeyDisplay();
            return true;
        }
        if (keysDown & HidNpadButton_Left) {
            m_cursorX = (m_cursorX + 3) % 4;
            UpdateSelectedKeyDisplay();
            return true;
        }
        if (keysDown & HidNpadButton_Right) {
            m_cursorX = (m_cursorX + 1) % 4;
            UpdateSelectedKeyDisplay();
            return true;
        }

        // A = Type selected character
        if (keysDown & HidNpadButton_A) {
            if (m_inputLen < 8) {
                m_input[m_inputLen++] = GetSelectedChar();
                m_input[m_inputLen] = '\0';
                UpdateInputDisplay();
            }
            return true;
        }

        // B = Backspace
        if (keysDown & HidNpadButton_B) {
            if (m_inputLen > 0) {
                m_input[--m_inputLen] = '\0';
                UpdateInputDisplay();
                return true;
            }
            // If empty, go back
            tsl::goBack();
            return true;
        }

        // X = Clear all
        if (keysDown & HidNpadButton_X) {
            m_inputLen = 0;
            m_input[0] = '\0';
            UpdateInputDisplay();
            return true;
        }

        // + = Save and exit
        if (keysDown & HidNpadButton_Plus) {
            SavePassphrase();
            tsl::goBack();
            return true;
        }

        // Y = Generate random
        if (keysDown & HidNpadButton_Y) {
            GenerateRandom();
            return true;
        }

        return false;
    }

private:
    int m_cursorX;
    int m_cursorY;
    char m_input[16];
    int m_inputLen;
    tsl::elm::ListItem* m_inputDisplay = nullptr;
    tsl::elm::ListItem* m_selectedKeyItem = nullptr;

    char GetSelectedChar() {
        const char* hex = "0123456789abcdef";
        return hex[m_cursorY * 4 + m_cursorX];
    }

    void UpdateSelectedKeyDisplay() {
        if (m_selectedKeyItem) {
            char buf[48];
            snprintf(buf, sizeof(buf), "'%c' (row %d, col %d)", GetSelectedChar(), m_cursorY + 1, m_cursorX + 1);
            m_selectedKeyItem->setValue(buf);
        }
    }

    void UpdateInputDisplay() {
        if (m_inputDisplay) {
            if (m_inputLen == 0) {
                m_inputDisplay->setValue("(empty)");
            } else {
                char buf[32];
                snprintf(buf, sizeof(buf), "%s (%d/8)", m_input, m_inputLen);
                m_inputDisplay->setValue(buf);
            }
        }
    }

    void SavePassphrase() {
        RyuLdnConfigService* svc = ryuLdnGetService();
        if (!svc) return;

        if (m_inputLen == 0) {
            // Clear passphrase
            ryuLdnSetPassphrase(svc, "");
        } else if (m_inputLen == 8) {
            // Build full passphrase with prefix
            char full[32];
            snprintf(full, sizeof(full), "Ryujinx-%s", m_input);
            ryuLdnSetPassphrase(svc, full);
        }
        // If not 8 chars, don't save (invalid)
    }

    void GenerateRandom() {
        const char* hex = "0123456789abcdef";
        for (int i = 0; i < 8; i++) {
            m_input[i] = hex[rand() % 16];
        }
        m_input[8] = '\0';
        m_inputLen = 8;
        UpdateInputDisplay();
    }
};

//=============================================================================
// Custom List Items
//=============================================================================

/**
 * @brief Status display item (read-only)
 *
 * Displays connection status with color coding.
 */
class StatusListItem : public tsl::elm::ListItem {
public:
    StatusListItem() : tsl::elm::ListItem("Server Status") {
        UpdateStatus();
    }

    void UpdateStatus() {
        if (g_initState != InitState::Loaded) {
            this->setValue("N/A");
            return;
        }

        RyuLdnConfigService* svc = ryuLdnGetService();
        if (!svc) {
            this->setValue("N/A");
            return;
        }

        RyuLdnConnectionStatus status;
        Result rc = ryuLdnGetConnectionStatus(svc, &status);
        if (R_FAILED(rc)) {
            this->setValue("Error");
            return;
        }

        this->setValue(ConnectionStatusToString(status));
    }
};

/**
 * @brief Server address display item
 */
class ServerAddressListItem : public tsl::elm::ListItem {
public:
    ServerAddressListItem() : tsl::elm::ListItem("Server") {
        UpdateAddress();
    }

    void UpdateAddress() {
        if (g_initState != InitState::Loaded) {
            this->setValue("N/A");
            return;
        }

        RyuLdnConfigService* svc = ryuLdnGetService();
        if (!svc) {
            this->setValue("N/A");
            return;
        }

        char host[64];
        u16 port;
        Result rc = ryuLdnGetServerAddress(svc, host, &port);
        if (R_FAILED(rc)) {
            this->setValue("Error");
            return;
        }

        char buf[96];
        snprintf(buf, sizeof(buf), "%s:%u", host, port);
        this->setValue(buf);
    }
};

/**
 * @brief Debug toggle item
 */
class DebugToggleListItem : public tsl::elm::ToggleListItem {
public:
    DebugToggleListItem() : ToggleListItem("Debug Logging", false) {
        if (g_initState != InitState::Loaded) {
            return;
        }

        RyuLdnConfigService* svc = ryuLdnGetService();
        if (!svc) return;

        u32 enabled;
        Result rc = ryuLdnGetDebugEnabled(svc, &enabled);
        if (R_SUCCEEDED(rc)) {
            this->setState(enabled != 0);
        }

        this->setStateChangedListener([](bool enabled) {
            RyuLdnConfigService* svc = ryuLdnGetService();
            if (svc) ryuLdnSetDebugEnabled(svc, enabled ? 1 : 0);
        });
    }
};

/**
 * @brief Save configuration button item
 *
 * Saves the current in-memory configuration to the config.ini file
 * on the SD card (/config/ryu_ldn_nx/config.ini).
 *
 * This allows settings changed via the overlay to persist across
 * reboots. Without saving, changes are lost when the Switch is
 * powered off or the sysmodule is restarted.
 *
 * Press A to save. Shows "Saved!" on success or "Failed" on error.
 */
class SaveConfigListItem : public tsl::elm::ListItem {
public:
    SaveConfigListItem() : tsl::elm::ListItem("Save Config") {
        this->setValue("Press A");
    }

    virtual bool onClick(u64 keys) override {
        if (keys & HidNpadButton_A) {
            if (g_initState == InitState::Loaded) {
                RyuLdnConfigService* svc = ryuLdnGetService();
                if (svc) {
                    RyuLdnConfigResult result;
                    Result rc = ryuLdnSaveConfig(svc, &result);
                    if (R_SUCCEEDED(rc) && result == RyuLdnConfigResult_Success) {
                        this->setValue("Saved!");
                    } else {
                        this->setValue("Failed");
                    }
                }
            }
            return true;
        }
        return false;
    }
};

/**
 * @brief Reload configuration button item
 *
 * Reloads the configuration from the config.ini file on the SD card,
 * discarding any unsaved in-memory changes.
 *
 * This is useful to revert changes made in the overlay without
 * having saved them, or to pick up changes made by editing the
 * config file directly on a PC.
 *
 * Press A to reload. Shows "Reloaded!" on success or "Failed" on error.
 */
class ReloadConfigListItem : public tsl::elm::ListItem {
public:
    ReloadConfigListItem() : tsl::elm::ListItem("Reload Config") {
        this->setValue("Press A");
    }

    virtual bool onClick(u64 keys) override {
        if (keys & HidNpadButton_A) {
            if (g_initState == InitState::Loaded) {
                RyuLdnConfigService* svc = ryuLdnGetService();
                if (svc) {
                    RyuLdnConfigResult result;
                    Result rc = ryuLdnReloadConfig(svc, &result);
                    if (R_SUCCEEDED(rc) && result == RyuLdnConfigResult_Success) {
                        this->setValue("Reloaded!");
                    } else {
                        this->setValue("Failed");
                    }
                }
            }
            return true;
        }
        return false;
    }
};

//=============================================================================
// Runtime Info List Items (Epic 6)
//=============================================================================

/**
 * @brief LDN State display item
 *
 * Shows the current LDN communication state when a game is active.
 */
class LdnStateListItem : public tsl::elm::ListItem {
public:
    LdnStateListItem() : tsl::elm::ListItem("LDN State") {
        UpdateState();
    }

    void UpdateState() {
        RyuLdnConfigService* svc = ryuLdnGetService();
        if (!svc) {
            this->setValue("N/A");
            return;
        }

        RyuLdnState state;
        Result rc = ryuLdnGetLdnState(svc, &state);
        if (R_FAILED(rc)) {
            this->setValue("Error");
            return;
        }

        this->setValue(ryuLdnStateToString(state));
    }
};

/**
 * @brief Session info display item
 *
 * Shows node count, role (host/client) when in a session.
 */
class SessionInfoListItem : public tsl::elm::ListItem {
public:
    SessionInfoListItem() : tsl::elm::ListItem("Session") {
        UpdateInfo();
    }

    void UpdateInfo() {
        RyuLdnConfigService* svc = ryuLdnGetService();
        if (!svc) {
            this->setValue("N/A");
            return;
        }

        RyuLdnSessionInfo info;
        Result rc = ryuLdnGetSessionInfo(svc, &info);
        if (R_FAILED(rc)) {
            this->setValue("Error");
            return;
        }

        if (info.node_count == 0) {
            this->setValue("Not in session");
        } else {
            char buf[48];
            snprintf(buf, sizeof(buf), "%d/%d (%s)",
                     info.node_count, info.max_nodes,
                     info.is_host ? "Host" : "Client");
            this->setValue(buf);
        }
    }
};

/**
 * @brief Latency display item
 *
 * Shows last measured RTT to server.
 */
class LatencyListItem : public tsl::elm::ListItem {
public:
    LatencyListItem() : tsl::elm::ListItem("Latency") {
        UpdateLatency();
    }

    void UpdateLatency() {
        RyuLdnConfigService* svc = ryuLdnGetService();
        if (!svc) {
            this->setValue("N/A");
            return;
        }

        u32 rtt_ms;
        Result rc = ryuLdnGetLastRtt(svc, &rtt_ms);
        if (R_FAILED(rc)) {
            this->setValue("Error");
            return;
        }

        if (rtt_ms == 0) {
            this->setValue("N/A");
        } else {
            char buf[16];
            snprintf(buf, sizeof(buf), "%u ms", rtt_ms);
            this->setValue(buf);
        }
    }
};

/**
 * @brief Force Reconnect button item
 *
 * Requests the MITM to reconnect to the server.
 */
class ForceReconnectListItem : public tsl::elm::ListItem {
public:
    ForceReconnectListItem() : tsl::elm::ListItem("Force Reconnect") {
        this->setValue("Press A");
    }

    virtual bool onClick(u64 keys) override {
        if (keys & HidNpadButton_A) {
            RyuLdnConfigService* svc = ryuLdnGetService();
            if (svc) {
                Result rc = ryuLdnForceReconnect(svc);
                if (R_SUCCEEDED(rc)) {
                    this->setValue("Requested!");
                } else {
                    this->setValue("Failed");
                }
            }
            return true;
        }
        return false;
    }
};

//=============================================================================
// Settings Submenus
//=============================================================================

/**
 * @brief Server Settings GUI
 *
 * Submenu for configuring server connection settings:
 * - Server Address: Displays current host:port (read-only display,
 *   editing requires keyboard which Tesla doesn't support well)
 * - Use TLS: Toggle to enable/disable TLS encryption for server connection
 *
 * Changes take effect immediately for toggles. Server address changes
 * require editing config.ini directly and using Reload Config.
 */
class ServerSettingsGui : public tsl::Gui {
public:
    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("Server Settings", g_version);
        auto list = new tsl::elm::List();

        if (g_initState != InitState::Loaded) {
            list->addItem(new tsl::elm::ListItem("Not available"));
            frame->setContent(list);
            return frame;
        }

        RyuLdnConfigService* svc = ryuLdnGetService();
        if (!svc) {
            list->addItem(new tsl::elm::ListItem("Service not available"));
            frame->setContent(list);
            return frame;
        }

        // Server address display (read-only - editing text not practical in Tesla)
        auto serverItem = new tsl::elm::ListItem("Server Address");
        char host[64];
        u16 port;
        if (R_SUCCEEDED(ryuLdnGetServerAddress(svc, host, &port))) {
            char buf[96];
            snprintf(buf, sizeof(buf), "%s:%u", host, port);
            serverItem->setValue(buf);
        }
        list->addItem(serverItem);

        // TLS encryption toggle
        auto tlsItem = new tsl::elm::ToggleListItem("Use TLS", false);
        u32 useTls;
        if (R_SUCCEEDED(ryuLdnGetUseTls(svc, &useTls))) {
            tlsItem->setState(useTls != 0);
        }
        tlsItem->setStateChangedListener([](bool enabled) {
            RyuLdnConfigService* svc = ryuLdnGetService();
            if (svc) ryuLdnSetUseTls(svc, enabled ? 1 : 0);
        });
        list->addItem(tlsItem);

        frame->setContent(list);
        return frame;
    }
};

/**
 * @brief Network Settings GUI
 *
 * Submenu for configuring network timing parameters:
 * - Connect Timeout: Time to wait for initial connection (displayed only)
 * - Ping Interval: How often to send keepalive pings (displayed only)
 *
 * These values are displayed for information. Editing numeric values
 * is not practical in Tesla overlay - use config.ini file to change them.
 */
class NetworkSettingsGui : public tsl::Gui {
public:
    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("Network Settings", g_version);
        auto list = new tsl::elm::List();

        if (g_initState != InitState::Loaded) {
            list->addItem(new tsl::elm::ListItem("Not available"));
            frame->setContent(list);
            return frame;
        }

        RyuLdnConfigService* svc = ryuLdnGetService();
        if (!svc) {
            list->addItem(new tsl::elm::ListItem("Service not available"));
            frame->setContent(list);
            return frame;
        }

        // Connect timeout display
        auto timeoutItem = new tsl::elm::ListItem("Connect Timeout");
        u32 timeout;
        if (R_SUCCEEDED(ryuLdnGetConnectTimeout(svc, &timeout))) {
            char buf[32];
            FormatTimeout(timeout, buf, sizeof(buf));
            timeoutItem->setValue(buf);
        }
        list->addItem(timeoutItem);

        // Ping interval display
        auto pingItem = new tsl::elm::ListItem("Ping Interval");
        u32 pingInterval;
        if (R_SUCCEEDED(ryuLdnGetPingInterval(svc, &pingInterval))) {
            char buf[32];
            FormatTimeout(pingInterval, buf, sizeof(buf));
            pingItem->setValue(buf);
        }
        list->addItem(pingItem);

        frame->setContent(list);
        return frame;
    }
};

/**
 * @brief LDN Settings GUI
 *
 * Submenu for configuring LDN (Local Network) settings:
 * - LDN Enabled: Master toggle for the LDN MITM functionality
 * - Passphrase: Room passphrase for matchmaking (hex keyboard editor)
 * - Generate Random: Creates a random 8-char hex passphrase
 * - Clear Passphrase: Removes the passphrase (matches all rooms)
 *
 * When LDN is disabled, the sysmodule will not intercept LDN calls
 * and games will use normal local wireless (if available).
 *
 * The passphrase is used to match players - only players with the
 * same passphrase can see and join each other's sessions.
 * Format: Ryujinx-[0-9a-f]{8} (prefix is hidden in UI)
 */
class LdnSettingsGui : public tsl::Gui {
public:
    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("LDN Settings", g_version);
        auto list = new tsl::elm::List();

        if (g_initState != InitState::Loaded) {
            list->addItem(new tsl::elm::ListItem("Not available"));
            frame->setContent(list);
            return frame;
        }

        RyuLdnConfigService* svc = ryuLdnGetService();
        if (!svc) {
            list->addItem(new tsl::elm::ListItem("Service not available"));
            frame->setContent(list);
            return frame;
        }

        // LDN enabled master toggle
        auto ldnItem = new tsl::elm::ToggleListItem("LDN Enabled", true);
        u32 ldnEnabled;
        if (R_SUCCEEDED(ryuLdnGetLdnEnabled(svc, &ldnEnabled))) {
            ldnItem->setState(ldnEnabled != 0);
        }
        ldnItem->setStateChangedListener([](bool enabled) {
            RyuLdnConfigService* svc = ryuLdnGetService();
            if (svc) ryuLdnSetLdnEnabled(svc, enabled ? 1 : 0);
        });
        list->addItem(ldnItem);

        list->addItem(new tsl::elm::CategoryHeader("Passphrase"));

        // Current passphrase display (shows hex part only)
        auto passphraseItem = new tsl::elm::ListItem("Current");
        char passphrase[64];
        if (R_SUCCEEDED(ryuLdnGetPassphrase(svc, passphrase))) {
            char display[32];
            FormatPassphraseDisplay(passphrase, display, sizeof(display));
            passphraseItem->setValue(display);
        }
        list->addItem(passphraseItem);

        // Edit passphrase button (opens hex keyboard)
        auto editItem = new tsl::elm::ListItem("Edit Passphrase");
        editItem->setValue(">");
        editItem->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<HexKeyboardGui>();
                return true;
            }
            return false;
        });
        list->addItem(editItem);

        // Generate random passphrase button
        auto randomItem = new tsl::elm::ListItem("Generate Random");
        randomItem->setValue("Press A");
        randomItem->setClickListener([this](u64 keys) {
            if (keys & HidNpadButton_A) {
                RyuLdnConfigService* svc = ryuLdnGetService();
                if (svc) {
                    char newPass[32];
                    GenerateRandomPassphraseOverlay(newPass, sizeof(newPass));
                    ryuLdnSetPassphrase(svc, newPass);
                }
                // Refresh the GUI
                tsl::changeTo<LdnSettingsGui>();
                return true;
            }
            return false;
        });
        list->addItem(randomItem);

        // Clear passphrase button
        auto clearItem = new tsl::elm::ListItem("Clear Passphrase");
        clearItem->setValue("Match all rooms");
        clearItem->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                RyuLdnConfigService* svc = ryuLdnGetService();
                if (svc) ryuLdnSetPassphrase(svc, "");
                tsl::changeTo<LdnSettingsGui>();
                return true;
            }
            return false;
        });
        list->addItem(clearItem);

        frame->setContent(list);
        return frame;
    }
};

/**
 * @brief Debug Settings GUI
 *
 * Submenu for configuring debug and logging settings:
 * - Debug Enabled: Master toggle for debug logging
 * - Debug Level: Current log verbosity (Error/Warning/Info/Verbose)
 * - Log to File: Toggle to write logs to SD card file
 *
 * When debug is enabled, log messages are written according to the
 * debug level. Higher levels include all messages from lower levels:
 * - Error (0): Only error messages
 * - Warning (1): Errors + warnings
 * - Info (2): Errors + warnings + info (default)
 * - Verbose (3): All messages including detailed traces
 *
 * Log files are written to /config/ryu_ldn_nx/ryu_ldn.log when
 * "Log to File" is enabled.
 */
class DebugSettingsGui : public tsl::Gui {
public:
    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("Debug Settings", g_version);
        auto list = new tsl::elm::List();

        if (g_initState != InitState::Loaded) {
            list->addItem(new tsl::elm::ListItem("Not available"));
            frame->setContent(list);
            return frame;
        }

        RyuLdnConfigService* svc = ryuLdnGetService();
        if (!svc) {
            list->addItem(new tsl::elm::ListItem("Service not available"));
            frame->setContent(list);
            return frame;
        }

        // Debug enabled master toggle
        auto debugItem = new tsl::elm::ToggleListItem("Debug Enabled", false);
        u32 debugEnabled;
        if (R_SUCCEEDED(ryuLdnGetDebugEnabled(svc, &debugEnabled))) {
            debugItem->setState(debugEnabled != 0);
        }
        debugItem->setStateChangedListener([](bool enabled) {
            RyuLdnConfigService* svc = ryuLdnGetService();
            if (svc) ryuLdnSetDebugEnabled(svc, enabled ? 1 : 0);
        });
        list->addItem(debugItem);

        // Debug level display (edit via config.ini for now)
        auto levelItem = new tsl::elm::ListItem("Debug Level");
        u32 level;
        if (R_SUCCEEDED(ryuLdnGetDebugLevel(svc, &level))) {
            levelItem->setValue(DebugLevelToString(level));
        }
        list->addItem(levelItem);

        // Log to file toggle
        auto logFileItem = new tsl::elm::ToggleListItem("Log to File", false);
        u32 logToFile;
        if (R_SUCCEEDED(ryuLdnGetLogToFile(svc, &logToFile))) {
            logFileItem->setState(logToFile != 0);
        }
        logFileItem->setStateChangedListener([](bool enabled) {
            RyuLdnConfigService* svc = ryuLdnGetService();
            if (svc) ryuLdnSetLogToFile(svc, enabled ? 1 : 0);
        });
        list->addItem(logFileItem);

        frame->setContent(list);
        return frame;
    }
};

//=============================================================================
// Main GUI
//=============================================================================

/**
 * @brief Main overlay GUI
 *
 * Main menu of the ryu_ldn_nx Tesla overlay. Displays:
 * - Status section: Connection status
 * - Server section: Current server address
 * - Settings section: Links to configuration submenus
 * - Config section: Save/reload configuration buttons
 *
 * The status section updates automatically every second (60 frames).
 * Press R to force an immediate refresh.
 *
 * Settings are organized into submenus for better organization:
 * - Server Settings: Host, port, TLS configuration
 * - Network Settings: Timeouts
 * - LDN Settings: Enable/disable LDN, passphrase
 * - Debug Settings: Logging configuration
 */
class MainGui : public tsl::Gui {
public:
    MainGui() {
        // Check game active state on construction
        RyuLdnConfigService* svc = ryuLdnGetService();
        if (svc) {
            u32 active;
            if (R_SUCCEEDED(ryuLdnIsGameActive(svc, &active))) {
                m_gameActive = (active != 0);
            }
        }
    }

    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("ryu_ldn_nx", g_version);
        auto list = new tsl::elm::List();

        if (g_initState == InitState::Error) {
            list->addItem(new tsl::elm::ListItem("ryu_ldn_nx not loaded"));
            list->addItem(new tsl::elm::ListItem("Check sysmodule installation"));
        } else if (g_initState == InitState::Uninit) {
            list->addItem(new tsl::elm::ListItem("Initializing..."));
        } else if (m_gameActive) {
            // =========================================================
            // Game Active Mode - Show runtime info, config is read-only
            // =========================================================

            // Runtime section - LDN state and session info
            list->addItem(new tsl::elm::CategoryHeader("Runtime (Game Active)"));

            m_ldnStateItem = new LdnStateListItem();
            list->addItem(m_ldnStateItem);

            m_sessionInfoItem = new SessionInfoListItem();
            list->addItem(m_sessionInfoItem);

            m_latencyItem = new LatencyListItem();
            list->addItem(m_latencyItem);

            // Force reconnect button
            list->addItem(new ForceReconnectListItem());

            // Status section
            list->addItem(new tsl::elm::CategoryHeader("Status"));
            m_statusItem = new StatusListItem();
            list->addItem(m_statusItem);

            // Config locked message
            list->addItem(new tsl::elm::CategoryHeader("Config"));
            auto lockedItem = new tsl::elm::ListItem("Config locked");
            lockedItem->setValue("(game in progress)");
            list->addItem(lockedItem);

        } else {
            // =========================================================
            // No Game Mode - Show configuration options
            // =========================================================

            // Status section - live connection information
            list->addItem(new tsl::elm::CategoryHeader("Status"));
            m_statusItem = new StatusListItem();
            list->addItem(m_statusItem);

            // Server section - current server
            list->addItem(new tsl::elm::CategoryHeader("Server"));
            m_serverItem = new ServerAddressListItem();
            list->addItem(m_serverItem);

            // Settings section - links to configuration submenus
            list->addItem(new tsl::elm::CategoryHeader("Settings"));

            // Server settings submenu
            auto serverSettingsItem = new tsl::elm::ListItem("Server Settings");
            serverSettingsItem->setValue(">");
            serverSettingsItem->setClickListener([](u64 keys) {
                if (keys & HidNpadButton_A) {
                    tsl::changeTo<ServerSettingsGui>();
                    return true;
                }
                return false;
            });
            list->addItem(serverSettingsItem);

            // Network settings submenu
            auto networkSettingsItem = new tsl::elm::ListItem("Network Settings");
            networkSettingsItem->setValue(">");
            networkSettingsItem->setClickListener([](u64 keys) {
                if (keys & HidNpadButton_A) {
                    tsl::changeTo<NetworkSettingsGui>();
                    return true;
                }
                return false;
            });
            list->addItem(networkSettingsItem);

            // LDN settings submenu
            auto ldnSettingsItem = new tsl::elm::ListItem("LDN Settings");
            ldnSettingsItem->setValue(">");
            ldnSettingsItem->setClickListener([](u64 keys) {
                if (keys & HidNpadButton_A) {
                    tsl::changeTo<LdnSettingsGui>();
                    return true;
                }
                return false;
            });
            list->addItem(ldnSettingsItem);

            // Debug settings submenu
            auto debugSettingsItem = new tsl::elm::ListItem("Debug Settings");
            debugSettingsItem->setValue(">");
            debugSettingsItem->setClickListener([](u64 keys) {
                if (keys & HidNpadButton_A) {
                    tsl::changeTo<DebugSettingsGui>();
                    return true;
                }
                return false;
            });
            list->addItem(debugSettingsItem);

            // Config persistence section - save/reload buttons
            list->addItem(new tsl::elm::CategoryHeader("Config"));
            list->addItem(new SaveConfigListItem());
            list->addItem(new ReloadConfigListItem());
        }

        frame->setContent(list);
        return frame;
    }

    virtual void update() override {
        // Update status every frame (Tesla calls this periodically)
        m_updateCounter++;
        if (m_updateCounter >= 60) {  // ~1 second at 60fps
            m_updateCounter = 0;
            RefreshStatus();
        }
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld,
                             const HidTouchState& touchPos,
                             HidAnalogStickState joyStickPosLeft,
                             HidAnalogStickState joyStickPosRight) override {
        // R button refreshes status immediately
        if (keysDown & HidNpadButton_R) {
            RefreshStatus();
            return true;
        }
        return false;
    }

private:
    void RefreshStatus() {
        if (g_initState != InitState::Loaded) return;

        if (m_statusItem) m_statusItem->UpdateStatus();

        // Refresh runtime items when game is active
        if (m_gameActive) {
            if (m_ldnStateItem) m_ldnStateItem->UpdateState();
            if (m_sessionInfoItem) m_sessionInfoItem->UpdateInfo();
            if (m_latencyItem) m_latencyItem->UpdateLatency();
        } else {
            if (m_serverItem) m_serverItem->UpdateAddress();
        }
    }

    // Common items
    StatusListItem* m_statusItem = nullptr;
    u32 m_updateCounter = 0;
    bool m_gameActive = false;

    // Config mode items (no game active)
    ServerAddressListItem* m_serverItem = nullptr;

    // Runtime mode items (game active)
    LdnStateListItem* m_ldnStateItem = nullptr;
    SessionInfoListItem* m_sessionInfoItem = nullptr;
    LatencyListItem* m_latencyItem = nullptr;
};

//=============================================================================
// Overlay Class
//=============================================================================

/**
 * @brief Main overlay application
 *
 * Handles service initialization and cleanup.
 * Connects directly to the ryu:cfg service provided by the sysmodule.
 */
class RyuLdnOverlay : public tsl::Overlay {
public:
    virtual void initServices() override {
        g_initState = InitState::Uninit;

        // Initialize connection to ryu:cfg service
        tsl::hlp::doWithSmSession([&] {
            Result rc = ryuLdnInitialize();
            if (R_FAILED(rc)) {
                g_initState = InitState::Error;
                return;
            }

            // Get version string
            RyuLdnConfigService* svc = ryuLdnGetService();
            if (svc) {
                rc = ryuLdnGetVersion(svc, g_version);
                if (R_FAILED(rc)) {
                    strcpy(g_version, "Unknown");
                }
            }

            g_initState = InitState::Loaded;
        });
    }

    virtual void exitServices() override {
        if (g_initState == InitState::Loaded) {
            ryuLdnExit();
        }
    }

    virtual void onShow() override {
        // Called when overlay becomes visible
    }

    virtual void onHide() override {
        // Called when overlay becomes hidden
    }

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<MainGui>();
    }
};

//=============================================================================
// Entry Point
//=============================================================================

/**
 * @brief Overlay entry point
 */
int main(int argc, char** argv) {
    return tsl::loop<RyuLdnOverlay>(argc, argv);
}
