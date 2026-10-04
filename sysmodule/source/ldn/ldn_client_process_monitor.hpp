/**
 * @file ldn_client_process_monitor.hpp
 * @brief Client Process Monitor implementation
 *
 * Required for firmware 18.0.0+ compatibility.
 * This is a stub implementation - the real functionality is not needed
 * but the interface must exist for games to work.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include "interfaces/iclient_process_monitor.hpp"

namespace ams::mitm::ldn {

class IClientProcessMonitor {
public:
    /// @gdb{tag="LDN:LIFECYCLE", msg="ClientProcessMonitor created"}
    IClientProcessMonitor();
    /// @gdb{tag="LDN:LIFECYCLE", msg="ClientProcessMonitor destroyed"}
    ~IClientProcessMonitor();

    static Result RegisterClient(const sf::ClientProcessId &client_process_id);
};

static_assert(ams::mitm::ldn::IsIClientProcessMonitorInterface<IClientProcessMonitor>);

} // namespace ams::mitm::ldn
