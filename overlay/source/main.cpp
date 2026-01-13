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

//=============================================================================
// Main GUI
//=============================================================================

/**
 * @brief Main overlay GUI
 *
 * Displays connection status, session info, and configuration options.
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
            // Status section
            list->addItem(new tsl::elm::CategoryHeader("Status"));
            m_statusItem = new StatusListItem();
            list->addItem(m_statusItem);

            m_ldnStateItem = new LdnStateListItem();
            list->addItem(m_ldnStateItem);

            m_sessionItem = new SessionInfoListItem();
            list->addItem(m_sessionItem);

            m_latencyItem = new LatencyListItem();
            list->addItem(m_latencyItem);

            // Server section
            list->addItem(new tsl::elm::CategoryHeader("Server"));
            m_serverItem = new ServerAddressListItem();
            list->addItem(m_serverItem);

            list->addItem(new ReconnectListItem());

            // Settings section
            list->addItem(new tsl::elm::CategoryHeader("Settings"));
            list->addItem(new DebugToggleListItem());
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
