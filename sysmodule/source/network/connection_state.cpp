/**
 * @file connection_state.cpp
 * @brief Connection state machine implementation
 *
 * This module implements a finite state machine (FSM) for managing the
 * lifecycle of network connections. The FSM ensures that connections
 * follow valid state transitions and provides hooks for monitoring
 * state changes.
 *
 * @section States
 * - Disconnected: Initial state, no active connection
 * - Connecting: TCP connection attempt in progress
 * - Connected: TCP connected, ready for handshake
 * - Handshaking: Protocol handshake in progress
 * - Ready: Fully connected and operational
 * - Backoff: Waiting before retry after failure
 * - Retrying: Retry attempt in progress
 * - Disconnecting: Graceful disconnect in progress
 * - Error: Unrecoverable error state
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "connection_state.hpp"

namespace ryu_ldn {
namespace network {

/**
 * @brief Constructor initializes state machine in Disconnected state
 *
 * The state machine starts in the Disconnected state with no callback
 * registered and zero retry count.
 */
ConnectionStateMachine::ConnectionStateMachine()
    : m_state(ConnectionState::Disconnected)
    , m_callback(nullptr)
    , m_retry_count(0)
{
}

/**
 * @brief Check if currently in a connected state
 *
 * A connection is considered "connected" if TCP is established,
 * regardless of whether handshake is complete.
 *
 * @return true if in Connected, Handshaking, or Ready state
 */
bool ConnectionStateMachine::is_connected() const {
    switch (m_state) {
        case ConnectionState::Connected:
        case ConnectionState::Handshaking:
        case ConnectionState::Ready:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Check if in a transitional (non-stable) state
 *
 * Transitional states are intermediate states where the connection
 * is neither fully established nor fully disconnected.
 *
 * @return true if in a transitional state
 */
bool ConnectionStateMachine::is_transitioning() const {
    switch (m_state) {
        case ConnectionState::Connecting:
        case ConnectionState::Handshaking:
        case ConnectionState::Retrying:
        case ConnectionState::Disconnecting:
        case ConnectionState::Backoff:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Validate state transition and determine target state
 *
 * This method implements the state transition table. Each state has
 * a defined set of valid events that can trigger transitions to
 * specific target states.
 *
 * @param from Current state
 * @param event Event being processed
 * @param[out] to Target state if transition is valid
 * @return true if the transition is valid, false otherwise
 */
bool ConnectionStateMachine::is_valid_transition(ConnectionState from,
                                                  ConnectionEvent event,
                                                  ConnectionState& to) const {
    switch (from) {
        // ================================================================
        // Disconnected: Waiting for connection request
        // ================================================================
        case ConnectionState::Disconnected:
            switch (event) {
                case ConnectionEvent::Connect:
                    to = ConnectionState::Connecting;
                    return true;
                case ConnectionEvent::RetryRequested:
                    to = ConnectionState::Connecting;
                    return true;
                default:
                    return false;
            }

        // ================================================================
        // Connecting: TCP connection attempt in progress
        // ================================================================
        case ConnectionState::Connecting:
            switch (event) {
                case ConnectionEvent::ConnectSuccess:
                    to = ConnectionState::Connected;
                    return true;
                case ConnectionEvent::ConnectFailed:
                    to = ConnectionState::Backoff;
                    return true;
                case ConnectionEvent::Disconnect:
                    to = ConnectionState::Disconnected;
                    return true;
                case ConnectionEvent::FatalError:
                    to = ConnectionState::Error;
                    return true;
                default:
                    return false;
            }

        // ================================================================
        // Connected: TCP established, ready for handshake
        // ================================================================
        case ConnectionState::Connected:
            switch (event) {
                case ConnectionEvent::HandshakeStarted:
                    to = ConnectionState::Handshaking;
                    return true;
                case ConnectionEvent::HandshakeSuccess:
                    to = ConnectionState::Ready;
                    return true;
                case ConnectionEvent::HandshakeFailed:
                    to = ConnectionState::Backoff;
                    return true;
                case ConnectionEvent::ConnectionLost:
                    to = ConnectionState::Backoff;
                    return true;
                case ConnectionEvent::Disconnect:
                    to = ConnectionState::Disconnecting;
                    return true;
                case ConnectionEvent::FatalError:
                    to = ConnectionState::Error;
                    return true;
                default:
                    return false;
            }

        // ================================================================
        // Handshaking: Protocol handshake in progress
        // ================================================================
        case ConnectionState::Handshaking:
            switch (event) {
                case ConnectionEvent::HandshakeSuccess:
                    to = ConnectionState::Ready;
                    return true;
                case ConnectionEvent::HandshakeFailed:
                    to = ConnectionState::Backoff;
                    return true;
                case ConnectionEvent::ConnectionLost:
                    to = ConnectionState::Backoff;
                    return true;
                case ConnectionEvent::Disconnect:
                    to = ConnectionState::Disconnecting;
                    return true;
                case ConnectionEvent::FatalError:
                    to = ConnectionState::Error;
                    return true;
                default:
                    return false;
            }

        // ================================================================
        // Ready: Fully connected and operational
        // ================================================================
        case ConnectionState::Ready:
            switch (event) {
                case ConnectionEvent::ConnectionLost:
                    to = ConnectionState::Backoff;
                    return true;
                case ConnectionEvent::Disconnect:
                    to = ConnectionState::Disconnecting;
                    return true;
                case ConnectionEvent::FatalError:
                    to = ConnectionState::Error;
                    return true;
                default:
                    return false;
            }

        // ================================================================
        // Backoff: Waiting before retry after failure
        // ================================================================
        case ConnectionState::Backoff:
            switch (event) {
                case ConnectionEvent::BackoffExpired:
                    to = ConnectionState::Retrying;
                    return true;
                case ConnectionEvent::Disconnect:
                    to = ConnectionState::Disconnected;
                    return true;
                case ConnectionEvent::RetryRequested:
                    to = ConnectionState::Retrying;
                    return true;
                case ConnectionEvent::FatalError:
                    to = ConnectionState::Error;
                    return true;
                default:
                    return false;
            }

        // ================================================================
        // Retrying: Retry connection attempt in progress
        // ================================================================
        case ConnectionState::Retrying:
            switch (event) {
                case ConnectionEvent::ConnectSuccess:
                    to = ConnectionState::Connected;
                    return true;
                case ConnectionEvent::ConnectFailed:
                    to = ConnectionState::Backoff;
                    return true;
                case ConnectionEvent::Disconnect:
                    to = ConnectionState::Disconnected;
                    return true;
                case ConnectionEvent::FatalError:
                    to = ConnectionState::Error;
                    return true;
                default:
                    return false;
            }

        // ================================================================
        // Disconnecting: Graceful disconnect in progress
        // ================================================================
        case ConnectionState::Disconnecting:
            switch (event) {
                case ConnectionEvent::ConnectSuccess:
                case ConnectionEvent::ConnectFailed:
                case ConnectionEvent::ConnectionLost:
                    to = ConnectionState::Disconnected;
                    return true;
                case ConnectionEvent::FatalError:
                    to = ConnectionState::Disconnected;
                    return true;
                default:
                    return false;
            }

        // ================================================================
        // Error: Unrecoverable error state
        // ================================================================
        case ConnectionState::Error:
            switch (event) {
                case ConnectionEvent::Disconnect:
                    to = ConnectionState::Disconnected;
                    return true;
                case ConnectionEvent::RetryRequested:
                    to = ConnectionState::Connecting;
                    return true;
                default:
                    return false;
            }

        default:
            return false;
    }
}

/**
 * @brief Execute state transition and notify callback
 *
 * This method performs the actual state change, updates the retry
 * counter as appropriate, and invokes the state change callback
 * if one is registered.
 *
 * Retry count is incremented when transitioning to Retrying or
 * Connecting from Backoff/Retrying states. It is reset to zero
 * when reaching the Ready state.
 *
 * @param new_state Target state to transition to
 * @param event Event that triggered this transition
 */
void ConnectionStateMachine::transition_to(ConnectionState new_state,
                                            ConnectionEvent event) {
    ConnectionState old_state = m_state;
    m_state = new_state;

    // Update retry count on retry attempts
    if (new_state == ConnectionState::Retrying ||
        new_state == ConnectionState::Connecting) {
        if (old_state == ConnectionState::Backoff ||
            old_state == ConnectionState::Retrying) {
            m_retry_count++;
        }
    }

    // Reset retry count on successful connection
    if (new_state == ConnectionState::Ready) {
        m_retry_count = 0;
    }

    // Notify callback if registered
    if (m_callback != nullptr) {
        m_callback(old_state, new_state, event);
    }
}

/**
 * @brief Process an event and perform state transition if valid
 *
 * This is the main entry point for driving the state machine. Events
 * are validated against the current state, and if a valid transition
 * exists, it is executed.
 *
 * @param event Event to process
 * @return TransitionResult indicating success or failure reason
 */
TransitionResult ConnectionStateMachine::process_event(ConnectionEvent event) {
    ConnectionState target_state;

    // Check for no-op transitions
    if (m_state == ConnectionState::Disconnected &&
        event == ConnectionEvent::Disconnect) {
        return TransitionResult::AlreadyInState;
    }

    if (m_state == ConnectionState::Ready &&
        event == ConnectionEvent::ConnectSuccess) {
        return TransitionResult::AlreadyInState;
    }

    // Validate and perform transition
    if (is_valid_transition(m_state, event, target_state)) {
        transition_to(target_state, event);
        return TransitionResult::Success;
    }

    return TransitionResult::InvalidTransition;
}

/**
 * @brief Set callback for state change notifications
 *
 * The callback will be invoked after each successful state transition
 * with the old state, new state, and triggering event.
 *
 * @param callback Function pointer to callback, or nullptr to disable
 */
void ConnectionStateMachine::set_state_change_callback(StateChangeCallback callback) {
    m_callback = callback;
}

/**
 * @brief Force state machine into a specific state
 *
 * This method bypasses all transition validation and directly sets
 * the state. Use with caution - primarily for testing or error
 * recovery scenarios.
 *
 * @warning Does not invoke the state change callback
 * @warning Does not update retry count
 *
 * @param state State to force
 */
void ConnectionStateMachine::force_state(ConnectionState state) {
    m_state = state;
}

/**
 * @brief Convert ConnectionState enum to human-readable string
 *
 * @param state State to convert
 * @return Null-terminated string representation
 */
const char* ConnectionStateMachine::state_to_string(ConnectionState state) {
    switch (state) {
        case ConnectionState::Disconnected:  return "Disconnected";
        case ConnectionState::Connecting:    return "Connecting";
        case ConnectionState::Connected:     return "Connected";
        case ConnectionState::Handshaking:   return "Handshaking";
        case ConnectionState::Ready:         return "Ready";
        case ConnectionState::Backoff:       return "Backoff";
        case ConnectionState::Retrying:      return "Retrying";
        case ConnectionState::Disconnecting: return "Disconnecting";
        case ConnectionState::Error:         return "Error";
        default:                             return "Unknown";
    }
}

/**
 * @brief Convert ConnectionEvent enum to human-readable string
 *
 * @param event Event to convert
 * @return Null-terminated string representation
 */
const char* ConnectionStateMachine::event_to_string(ConnectionEvent event) {
    switch (event) {
        case ConnectionEvent::Connect:           return "Connect";
        case ConnectionEvent::ConnectSuccess:    return "ConnectSuccess";
        case ConnectionEvent::ConnectFailed:     return "ConnectFailed";
        case ConnectionEvent::HandshakeStarted:  return "HandshakeStarted";
        case ConnectionEvent::HandshakeSuccess:  return "HandshakeSuccess";
        case ConnectionEvent::HandshakeFailed:   return "HandshakeFailed";
        case ConnectionEvent::Disconnect:        return "Disconnect";
        case ConnectionEvent::ConnectionLost:    return "ConnectionLost";
        case ConnectionEvent::BackoffExpired:    return "BackoffExpired";
        case ConnectionEvent::RetryRequested:    return "RetryRequested";
        case ConnectionEvent::FatalError:        return "FatalError";
        default:                                 return "Unknown";
    }
}

} // namespace network
} // namespace ryu_ldn
