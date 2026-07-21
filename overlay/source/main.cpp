/**
 * @file main.cpp
 * @brief Entry point for the ryu_ldn_nx Tesla Overlay.
 *
 * Initializes the ryu:cfg IPC service, queries the sysmodule version,
 * and loads the MainDashboardGui as the initial screen.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include "ryu_ldn_ipc.h"
#include "app/overlay_state.hpp"
#include "views/connection/main_dashboard_gui.hpp"

class RyuLdnOverlay : public tsl::Overlay {
public:
    virtual void initServices() override {
        auto& state = OverlayState::Instance();
        state.SetStatus(OverlayState::InitStatus::Uninit);
        tsl::hlp::doWithSmSession([&] {
            Result rc = ryuLdnInitialize();
            if (R_FAILED(rc)) { state.SetStatus(OverlayState::InitStatus::Error); return; }
            RyuLdnConfigService* svc = ryuLdnGetService();
            if (svc) {
                char version[32];
                rc = ryuLdnGetVersion(svc, version);
                state.SetVersion(R_SUCCEEDED(rc) ? version : "Unknown");
            }
            state.SetStatus(OverlayState::InitStatus::Loaded);
        });

    }
    virtual void exitServices() override {
        if (OverlayState::Instance().GetStatus() == OverlayState::InitStatus::Loaded)
            ryuLdnExit();
    }
    virtual void onShow() override {}
    virtual void onHide() override {}
    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<MainDashboardGui>();
    }
};

int main(int argc, char** argv) {
    return tsl::loop<RyuLdnOverlay>(argc, argv);
}
