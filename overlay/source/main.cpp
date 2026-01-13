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

/// Global service handles
Service g_ldnService;
RyuLdnConfigService g_configService;
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
 * @brief Convert LDN state to string
 */
const char* LdnStateToString(RyuLdnState state) {
    switch (state) {
        case RyuLdnState_None:              return "None";
        case RyuLdnState_Initialized:       return "Initialized";
        case RyuLdnState_AccessPoint:       return "Access Point";
        case RyuLdnState_AccessPointCreated:return "Hosting";
        case RyuLdnState_Station:           return "Station";
        case RyuLdnState_StationConnected:  return "Connected";
        case RyuLdnState_Error:             return "Error";
        default:                            return "Unknown";
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
 * @brief Format passphrase for display (masked)
 */
void FormatPassphraseMasked(const char* passphrase, char* buf, size_t bufSize) {
    if (passphrase == nullptr || passphrase[0] == '\0') {
        snprintf(buf, bufSize, "(not set)");
    } else {
        size_t len = strlen(passphrase);
        if (len <= 4) {
            snprintf(buf, bufSize, "****");
        } else {
            snprintf(buf, bufSize, "%c%c****%c%c",
                     passphrase[0], passphrase[1],
                     passphrase[len-2], passphrase[len-1]);
        }
    }
}

// Forward declarations for submenus
class ServerSettingsGui;
class NetworkSettingsGui;
class LdnSettingsGui;
class DebugSettingsGui;

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

        RyuLdnConnectionStatus status;
        Result rc = ryuLdnGetConnectionStatus(&g_configService, &status);
        if (R_FAILED(rc)) {
            this->setValue("Error");
            return;
        }

        this->setValue(ConnectionStatusToString(status));
    }
};

/**
 * @brief LDN State display item
 */
class LdnStateListItem : public tsl::elm::ListItem {
public:
    LdnStateListItem() : tsl::elm::ListItem("LDN State") {
        UpdateState();
    }

    void UpdateState() {
        if (g_initState != InitState::Loaded) {
            this->setValue("N/A");
            return;
        }

        RyuLdnState state;
        Result rc = ryuLdnGetLdnState(&g_configService, &state);
        if (R_FAILED(rc)) {
            this->setValue("Error");
            return;
        }

        this->setValue(LdnStateToString(state));
    }
};

/**
 * @brief Session info display item
 */
class SessionInfoListItem : public tsl::elm::ListItem {
public:
    SessionInfoListItem() : tsl::elm::ListItem("Session") {
        UpdateInfo();
    }

    void UpdateInfo() {
        if (g_initState != InitState::Loaded) {
            this->setValue("N/A");
            return;
        }

        RyuLdnSessionInfo info;
        Result rc = ryuLdnGetSessionInfo(&g_configService, &info);
        if (R_FAILED(rc)) {
            this->setValue("Not in session");
            return;
        }

        if (info.node_count == 0) {
            this->setValue("Not in session");
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "%d/%d players (%s)",
                     info.node_count, info.node_count_max,
                     info.is_host ? "Host" : "Client");
            this->setValue(buf);
        }
    }
};

/**
 * @brief Latency display item
 */
class LatencyListItem : public tsl::elm::ListItem {
public:
    LatencyListItem() : tsl::elm::ListItem("Latency") {
        UpdateLatency();
    }

    void UpdateLatency() {
        if (g_initState != InitState::Loaded) {
            this->setValue("N/A");
            return;
        }

        u32 rtt_ms;
        Result rc = ryuLdnGetLastRtt(&g_configService, &rtt_ms);
        if (R_FAILED(rc) || rtt_ms == 0) {
            this->setValue("N/A");
            return;
        }

        char buf[32];
        snprintf(buf, sizeof(buf), "%u ms", rtt_ms);
        this->setValue(buf);
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

        char host[64];
        u16 port;
        Result rc = ryuLdnGetServerAddress(&g_configService, host, &port);
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

        u32 enabled;
        Result rc = ryuLdnGetDebugEnabled(&g_configService, &enabled);
        if (R_SUCCEEDED(rc)) {
            this->setState(enabled != 0);
        }

        this->setStateChangedListener([](bool enabled) {
            ryuLdnSetDebugEnabled(&g_configService, enabled ? 1 : 0);
        });
    }
};

/**
 * @brief Reconnect button item
 *
 * Provides a button to force the sysmodule to disconnect and reconnect
 * to the configured server. Useful when changing server settings or
 * when the connection is in a bad state.
 *
 * Press A to trigger reconnection. The button shows "Reconnecting..."
 * on success or "Failed" if the IPC call fails.
 */
class ReconnectListItem : public tsl::elm::ListItem {
public:
    ReconnectListItem() : tsl::elm::ListItem("Force Reconnect") {
        this->setValue("Press A");
    }

    virtual bool onClick(u64 keys) override {
        if (keys & HidNpadButton_A) {
            if (g_initState == InitState::Loaded) {
                Result rc = ryuLdnForceReconnect(&g_configService);
                if (R_SUCCEEDED(rc)) {
                    this->setValue("Reconnecting...");
                } else {
                    this->setValue("Failed");
                }
            }
            return true;
        }
        return false;
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
                RyuLdnConfigResult result;
                Result rc = ryuLdnSaveConfig(&g_configService, &result);
                if (R_SUCCEEDED(rc) && result == RyuLdnConfigResult_Success) {
                    this->setValue("Saved!");
                } else {
                    this->setValue("Failed");
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
                RyuLdnConfigResult result;
                Result rc = ryuLdnReloadConfig(&g_configService, &result);
                if (R_SUCCEEDED(rc) && result == RyuLdnConfigResult_Success) {
                    this->setValue("Reloaded!");
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
 * - Force Reconnect: Button to reconnect with new settings
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

        // Server address display (read-only - editing text not practical in Tesla)
        auto serverItem = new tsl::elm::ListItem("Server Address");
        char host[64];
        u16 port;
        if (R_SUCCEEDED(ryuLdnGetServerAddress(&g_configService, host, &port))) {
            char buf[96];
            snprintf(buf, sizeof(buf), "%s:%u", host, port);
            serverItem->setValue(buf);
        }
        list->addItem(serverItem);

        // TLS encryption toggle
        auto tlsItem = new tsl::elm::ToggleListItem("Use TLS", false);
        u32 useTls;
        if (R_SUCCEEDED(ryuLdnGetUseTls(&g_configService, &useTls))) {
            tlsItem->setState(useTls != 0);
        }
        tlsItem->setStateChangedListener([](bool enabled) {
            ryuLdnSetUseTls(&g_configService, enabled ? 1 : 0);
        });
        list->addItem(tlsItem);

        // Reconnect button to apply changes
        list->addItem(new ReconnectListItem());

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
 * - Reconnect Delay: Time to wait between reconnection attempts (displayed only)
 * - Max Reconnect Attempts: Number of retries before giving up (displayed only)
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

        // Connect timeout display
        auto timeoutItem = new tsl::elm::ListItem("Connect Timeout");
        u32 timeout;
        if (R_SUCCEEDED(ryuLdnGetConnectTimeout(&g_configService, &timeout))) {
            char buf[32];
            FormatTimeout(timeout, buf, sizeof(buf));
            timeoutItem->setValue(buf);
        }
        list->addItem(timeoutItem);

        // Ping interval display
        auto pingItem = new tsl::elm::ListItem("Ping Interval");
        u32 pingInterval;
        if (R_SUCCEEDED(ryuLdnGetPingInterval(&g_configService, &pingInterval))) {
            char buf[32];
            FormatTimeout(pingInterval, buf, sizeof(buf));
            pingItem->setValue(buf);
        }
        list->addItem(pingItem);

        // Reconnect delay display
        auto delayItem = new tsl::elm::ListItem("Reconnect Delay");
        u32 delay;
        if (R_SUCCEEDED(ryuLdnGetReconnectDelay(&g_configService, &delay))) {
            char buf[32];
            FormatTimeout(delay, buf, sizeof(buf));
            delayItem->setValue(buf);
        }
        list->addItem(delayItem);

        // Max reconnect attempts display
        auto attemptsItem = new tsl::elm::ListItem("Max Reconnect Attempts");
        u32 attempts;
        if (R_SUCCEEDED(ryuLdnGetMaxReconnectAttempts(&g_configService, &attempts))) {
            char buf[32];
            if (attempts == 0) {
                snprintf(buf, sizeof(buf), "Unlimited");
            } else {
                snprintf(buf, sizeof(buf), "%u", attempts);
            }
            attemptsItem->setValue(buf);
        }
        list->addItem(attemptsItem);

        frame->setContent(list);
        return frame;
    }
};

/**
 * @brief LDN Settings GUI
 *
 * Submenu for configuring LDN (Local Network) settings:
 * - LDN Enabled: Master toggle for the LDN MITM functionality
 * - Passphrase: Room passphrase for matchmaking (displayed masked for privacy)
 *
 * When LDN is disabled, the sysmodule will not intercept LDN calls
 * and games will use normal local wireless (if available).
 *
 * The passphrase is used to match players - only players with the
 * same passphrase can see and join each other's sessions.
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

        // LDN enabled master toggle
        auto ldnItem = new tsl::elm::ToggleListItem("LDN Enabled", true);
        u32 ldnEnabled;
        if (R_SUCCEEDED(ryuLdnGetLdnEnabled(&g_configService, &ldnEnabled))) {
            ldnItem->setState(ldnEnabled != 0);
        }
        ldnItem->setStateChangedListener([](bool enabled) {
            ryuLdnSetLdnEnabled(&g_configService, enabled ? 1 : 0);
        });
        list->addItem(ldnItem);

        // Passphrase display (masked for privacy, edit via config.ini)
        auto passphraseItem = new tsl::elm::ListItem("Passphrase");
        char passphrase[64];
        if (R_SUCCEEDED(ryuLdnGetPassphrase(&g_configService, passphrase))) {
            char masked[32];
            FormatPassphraseMasked(passphrase, masked, sizeof(masked));
            passphraseItem->setValue(masked);
        }
        list->addItem(passphraseItem);

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

        // Debug enabled master toggle
        auto debugItem = new tsl::elm::ToggleListItem("Debug Enabled", false);
        u32 debugEnabled;
        if (R_SUCCEEDED(ryuLdnGetDebugEnabled(&g_configService, &debugEnabled))) {
            debugItem->setState(debugEnabled != 0);
        }
        debugItem->setStateChangedListener([](bool enabled) {
            ryuLdnSetDebugEnabled(&g_configService, enabled ? 1 : 0);
        });
        list->addItem(debugItem);

        // Debug level display (edit via config.ini for now)
        auto levelItem = new tsl::elm::ListItem("Debug Level");
        u32 level;
        if (R_SUCCEEDED(ryuLdnGetDebugLevel(&g_configService, &level))) {
            levelItem->setValue(DebugLevelToString(level));
        }
        list->addItem(levelItem);

        // Log to file toggle
        auto logFileItem = new tsl::elm::ToggleListItem("Log to File", false);
        u32 logToFile;
        if (R_SUCCEEDED(ryuLdnGetLogToFile(&g_configService, &logToFile))) {
            logFileItem->setState(logToFile != 0);
        }
        logFileItem->setStateChangedListener([](bool enabled) {
            ryuLdnSetLogToFile(&g_configService, enabled ? 1 : 0);
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
 * - Status section: Connection status, LDN state, session info, latency
 * - Server section: Current server address and reconnect button
 * - Settings section: Links to configuration submenus
 * - Config section: Save/reload configuration buttons
 *
 * The status section updates automatically every second (60 frames).
 * Press R to force an immediate refresh.
 *
 * Settings are organized into submenus for better organization:
 * - Server Settings: Host, port, TLS configuration
 * - Network Settings: Timeouts and reconnection parameters
 * - LDN Settings: Enable/disable LDN, passphrase
 * - Debug Settings: Logging configuration
 */
class MainGui : public tsl::Gui {
public:
    MainGui() = default;

    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("ryu_ldn_nx", g_version);
        auto list = new tsl::elm::List();

        if (g_initState == InitState::Error) {
            list->addItem(new tsl::elm::ListItem("ryu_ldn_nx not loaded"));
            list->addItem(new tsl::elm::ListItem("Check sysmodule installation"));
        } else if (g_initState == InitState::Uninit) {
            list->addItem(new tsl::elm::ListItem("Initializing..."));
        } else {
            // Status section - live connection information
            list->addItem(new tsl::elm::CategoryHeader("Status"));
            m_statusItem = new StatusListItem();
            list->addItem(m_statusItem);

            m_ldnStateItem = new LdnStateListItem();
            list->addItem(m_ldnStateItem);

            m_sessionItem = new SessionInfoListItem();
            list->addItem(m_sessionItem);

            m_latencyItem = new LatencyListItem();
            list->addItem(m_latencyItem);

            // Server section - current server and quick reconnect
            list->addItem(new tsl::elm::CategoryHeader("Server"));
            m_serverItem = new ServerAddressListItem();
            list->addItem(m_serverItem);

            list->addItem(new ReconnectListItem());

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
        if (m_ldnStateItem) m_ldnStateItem->UpdateState();
        if (m_sessionItem) m_sessionItem->UpdateInfo();
        if (m_latencyItem) m_latencyItem->UpdateLatency();
    }

    StatusListItem* m_statusItem = nullptr;
    LdnStateListItem* m_ldnStateItem = nullptr;
    SessionInfoListItem* m_sessionItem = nullptr;
    LatencyListItem* m_latencyItem = nullptr;
    ServerAddressListItem* m_serverItem = nullptr;
    u32 m_updateCounter = 0;
};

//=============================================================================
// Overlay Class
//=============================================================================

/**
 * @brief Main overlay application
 *
 * Handles service initialization and cleanup.
 */
class RyuLdnOverlay : public tsl::Overlay {
public:
    virtual void initServices() override {
        g_initState = InitState::Uninit;

        // Initialize services within SM session
        tsl::hlp::doWithSmSession([&] {
            // Get ldn:u service
            Result rc = smGetService(&g_ldnService, "ldn:u");
            if (R_FAILED(rc)) {
                g_initState = InitState::Error;
                return;
            }

            // Get our custom config service from ldn:u MITM
            rc = ryuLdnGetConfigFromService(&g_ldnService, &g_configService);
            if (R_FAILED(rc)) {
                serviceClose(&g_ldnService);
                g_initState = InitState::Error;
                return;
            }

            // Get version string
            rc = ryuLdnGetVersion(&g_configService, g_version);
            if (R_FAILED(rc)) {
                strcpy(g_version, "Unknown");
            }

            g_initState = InitState::Loaded;
        });
    }

    virtual void exitServices() override {
        if (g_initState == InitState::Loaded) {
            serviceClose(&g_configService.s);
            serviceClose(&g_ldnService);
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
