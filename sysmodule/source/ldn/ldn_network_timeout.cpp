/**
 * @file ldn_network_timeout.cpp
 * @brief Network inactivity timeout manager implementation
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ldn_network_timeout.hpp"
#include "../debug/log.hpp"

namespace ams::mitm::ldn {

NetworkTimeout::NetworkTimeout(uint64_t idle_timeout_ms, TimeoutCallback callback)
    : m_idle_timeout_ms(idle_timeout_ms)
    , m_callback(callback)
    , m_timeout_start_ms(0)
    , m_active(false)
    , m_mutex(false) // Non-recursive mutex
{
    LOG_VERBOSE("NetworkTimeout created with %lu ms timeout", idle_timeout_ms);
}

NetworkTimeout::~NetworkTimeout() {
    DisableTimeout();
    LOG_VERBOSE("NetworkTimeout destroyed");
}

bool NetworkTimeout::RefreshTimeout() {
    std::scoped_lock lock(m_mutex);

    // Get current time
    m_timeout_start_ms = os::ConvertToTimeSpan(os::GetSystemTick()).GetMilliSeconds();
    m_active = true;

    LOG_VERBOSE("NetworkTimeout refreshed, will expire in %lu ms", m_idle_timeout_ms);

    return true;
}

void NetworkTimeout::DisableTimeout() {
    std::scoped_lock lock(m_mutex);

    m_active = false;
    m_timeout_start_ms = 0;

    LOG_VERBOSE("NetworkTimeout disabled");
}

bool NetworkTimeout::CheckTimeout(uint64_t current_time_ms) {
    std::scoped_lock lock(m_mutex);

    if (!m_active || m_callback == nullptr) {
        return false;
    }

    // Check if timeout has elapsed
    if (current_time_ms >= m_timeout_start_ms + m_idle_timeout_ms) {
        LOG_INFO("NetworkTimeout expired after %lu ms of inactivity",
                 current_time_ms - m_timeout_start_ms);

        m_active = false;

        // Invoke callback (must be quick, we're holding the lock)
        // Note: Ryujinx releases the lock before calling, but our callback
        // is just setting a flag, so it's safe to hold
        m_callback();

        return true;
    }

    return false;
}

bool NetworkTimeout::IsActive() const {
    std::scoped_lock lock(m_mutex);
    return m_active;
}

} // namespace ams::mitm::ldn
