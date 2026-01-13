/**
 * @file ldn_state_machine.cpp
 * @brief LDN Communication State Machine implementation
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ldn_state_machine.hpp"

namespace ams::mitm::ldn {

LdnStateMachine::LdnStateMachine()
    : m_mutex()
    , m_state(CommState::None)
    , m_state_event(os::EventClearMode_AutoClear, true)
    , m_callback(nullptr)
    , m_callback_user_data(nullptr)
{
}

LdnStateMachine::~LdnStateMachine() {
}

// ============================================================================
// State Queries
// ============================================================================

CommState LdnStateMachine::GetState() const {
    std::scoped_lock lk(m_mutex);
    return m_state;
}

bool LdnStateMachine::IsInState(CommState state) const {
    std::scoped_lock lk(m_mutex);
    return m_state == state;
}

bool LdnStateMachine::IsInitialized() const {
    std::scoped_lock lk(m_mutex);
    return m_state != CommState::None && m_state != CommState::Error;
}

bool LdnStateMachine::IsNetworkActive() const {
    std::scoped_lock lk(m_mutex);
    return m_state == CommState::AccessPointCreated ||
           m_state == CommState::StationConnected;
}

// ============================================================================
// State Transitions
// ============================================================================

StateTransitionResult LdnStateMachine::Initialize() {
    std::scoped_lock lk(m_mutex);

    if (m_state == CommState::Initialized) {
        return StateTransitionResult::AlreadyInState;
    }
    if (m_state != CommState::None) {
        return StateTransitionResult::InvalidTransition;
    }

    CommState old_state = m_state;
    m_state = CommState::Initialized;
    SignalStateChange();

    if (m_callback) {
        m_callback(old_state, m_state, m_callback_user_data);
    }

    return StateTransitionResult::Success;
}

StateTransitionResult LdnStateMachine::Finalize() {
    std::scoped_lock lk(m_mutex);

    if (m_state == CommState::None) {
        return StateTransitionResult::AlreadyInState;
    }

    CommState old_state = m_state;
    m_state = CommState::None;
    SignalStateChange();

    if (m_callback) {
        m_callback(old_state, m_state, m_callback_user_data);
    }

    return StateTransitionResult::Success;
}

StateTransitionResult LdnStateMachine::OpenAccessPoint() {
    std::scoped_lock lk(m_mutex);

    if (m_state == CommState::AccessPoint) {
        return StateTransitionResult::AlreadyInState;
    }
    if (m_state != CommState::Initialized) {
        return StateTransitionResult::InvalidTransition;
    }

    CommState old_state = m_state;
    m_state = CommState::AccessPoint;
    SignalStateChange();

    if (m_callback) {
        m_callback(old_state, m_state, m_callback_user_data);
    }

    return StateTransitionResult::Success;
}

StateTransitionResult LdnStateMachine::CloseAccessPoint() {
    std::scoped_lock lk(m_mutex);

    if (m_state == CommState::Initialized) {
        return StateTransitionResult::AlreadyInState;
    }
    if (m_state != CommState::AccessPoint && m_state != CommState::AccessPointCreated) {
        return StateTransitionResult::InvalidTransition;
    }

    CommState old_state = m_state;
    m_state = CommState::Initialized;
    SignalStateChange();

    if (m_callback) {
        m_callback(old_state, m_state, m_callback_user_data);
    }

    return StateTransitionResult::Success;
}

StateTransitionResult LdnStateMachine::CreateNetwork() {
    std::scoped_lock lk(m_mutex);

    if (m_state == CommState::AccessPointCreated) {
        return StateTransitionResult::AlreadyInState;
    }
    if (m_state != CommState::AccessPoint) {
        return StateTransitionResult::InvalidTransition;
    }

    CommState old_state = m_state;
    m_state = CommState::AccessPointCreated;
    SignalStateChange();

    if (m_callback) {
        m_callback(old_state, m_state, m_callback_user_data);
    }

    return StateTransitionResult::Success;
}

StateTransitionResult LdnStateMachine::DestroyNetwork() {
    std::scoped_lock lk(m_mutex);

    if (m_state == CommState::AccessPoint) {
        return StateTransitionResult::AlreadyInState;
    }
    if (m_state != CommState::AccessPointCreated) {
        return StateTransitionResult::InvalidTransition;
    }

    CommState old_state = m_state;
    m_state = CommState::AccessPoint;
    SignalStateChange();

    if (m_callback) {
        m_callback(old_state, m_state, m_callback_user_data);
    }

    return StateTransitionResult::Success;
}

StateTransitionResult LdnStateMachine::OpenStation() {
    std::scoped_lock lk(m_mutex);

    if (m_state == CommState::Station) {
        return StateTransitionResult::AlreadyInState;
    }
    if (m_state != CommState::Initialized) {
        return StateTransitionResult::InvalidTransition;
    }

    CommState old_state = m_state;
    m_state = CommState::Station;
    SignalStateChange();

    if (m_callback) {
        m_callback(old_state, m_state, m_callback_user_data);
    }

    return StateTransitionResult::Success;
}

StateTransitionResult LdnStateMachine::CloseStation() {
    std::scoped_lock lk(m_mutex);

    if (m_state == CommState::Initialized) {
        return StateTransitionResult::AlreadyInState;
    }
    if (m_state != CommState::Station && m_state != CommState::StationConnected) {
        return StateTransitionResult::InvalidTransition;
    }

    CommState old_state = m_state;
    m_state = CommState::Initialized;
    SignalStateChange();

    if (m_callback) {
        m_callback(old_state, m_state, m_callback_user_data);
    }

    return StateTransitionResult::Success;
}

StateTransitionResult LdnStateMachine::Connect() {
    std::scoped_lock lk(m_mutex);

    if (m_state == CommState::StationConnected) {
        return StateTransitionResult::AlreadyInState;
    }
    if (m_state != CommState::Station) {
        return StateTransitionResult::InvalidTransition;
    }

    CommState old_state = m_state;
    m_state = CommState::StationConnected;
    SignalStateChange();

    if (m_callback) {
        m_callback(old_state, m_state, m_callback_user_data);
    }

    return StateTransitionResult::Success;
}

StateTransitionResult LdnStateMachine::Disconnect() {
    std::scoped_lock lk(m_mutex);

    if (m_state == CommState::Station) {
        return StateTransitionResult::AlreadyInState;
    }
    if (m_state != CommState::StationConnected) {
        return StateTransitionResult::InvalidTransition;
    }

    CommState old_state = m_state;
    m_state = CommState::Station;
    SignalStateChange();

    if (m_callback) {
        m_callback(old_state, m_state, m_callback_user_data);
    }

    return StateTransitionResult::Success;
}

StateTransitionResult LdnStateMachine::SetError() {
    std::scoped_lock lk(m_mutex);

    if (m_state == CommState::Error) {
        return StateTransitionResult::AlreadyInState;
    }

    CommState old_state = m_state;
    m_state = CommState::Error;
    SignalStateChange();

    if (m_callback) {
        m_callback(old_state, m_state, m_callback_user_data);
    }

    return StateTransitionResult::Success;
}

// ============================================================================
// Event Management
// ============================================================================

os::NativeHandle LdnStateMachine::GetStateChangeEventHandle() const {
    return m_state_event.GetReadableHandle();
}

void LdnStateMachine::SetStateCallback(StateCallback callback, void* user_data) {
    std::scoped_lock lk(m_mutex);
    m_callback = callback;
    m_callback_user_data = user_data;
}

// ============================================================================
// Utilities
// ============================================================================

const char* LdnStateMachine::StateToString(CommState state) {
    switch (state) {
        case CommState::None:               return "None";
        case CommState::Initialized:        return "Initialized";
        case CommState::AccessPoint:        return "AccessPoint";
        case CommState::AccessPointCreated: return "AccessPointCreated";
        case CommState::Station:            return "Station";
        case CommState::StationConnected:   return "StationConnected";
        case CommState::Error:              return "Error";
        default:                            return "Unknown";
    }
}

const char* LdnStateMachine::ResultToString(StateTransitionResult result) {
    switch (result) {
        case StateTransitionResult::Success:           return "Success";
        case StateTransitionResult::InvalidTransition: return "InvalidTransition";
        case StateTransitionResult::AlreadyInState:    return "AlreadyInState";
        default:                                       return "Unknown";
    }
}

// ============================================================================
// Private Methods
// ============================================================================

StateTransitionResult LdnStateMachine::TransitionTo(CommState new_state) {
    if (m_state == new_state) {
        return StateTransitionResult::AlreadyInState;
    }

    if (!IsValidTransition(m_state, new_state)) {
        return StateTransitionResult::InvalidTransition;
    }

    CommState old_state = m_state;
    m_state = new_state;
    SignalStateChange();

    if (m_callback) {
        m_callback(old_state, m_state, m_callback_user_data);
    }

    return StateTransitionResult::Success;
}

bool LdnStateMachine::IsValidTransition(CommState from, CommState to) {
    // Special cases: can always go to Error or None (via Finalize)
    if (to == CommState::Error || to == CommState::None) {
        return true;
    }

    switch (from) {
        case CommState::None:
            return to == CommState::Initialized;

        case CommState::Initialized:
            return to == CommState::AccessPoint || to == CommState::Station;

        case CommState::AccessPoint:
            return to == CommState::AccessPointCreated || to == CommState::Initialized;

        case CommState::AccessPointCreated:
            return to == CommState::AccessPoint || to == CommState::Initialized;

        case CommState::Station:
            return to == CommState::StationConnected || to == CommState::Initialized;

        case CommState::StationConnected:
            return to == CommState::Station || to == CommState::Initialized;

        case CommState::Error:
            // Can recover to Initialized via re-initialization
            return to == CommState::Initialized;

        default:
            return false;
    }
}

void LdnStateMachine::SignalStateChange() {
    m_state_event.Signal();
}

} // namespace ams::mitm::ldn
