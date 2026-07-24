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
#include "../debug/log.hpp"

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
    // Transition lookup table: [state][event] -> (target_state, valid)
    // Invalid transitions are represented by (Disconnected, false).
    // This table replaces the 9-case switch to reduce cyclomatic complexity
    // flagged by CodeQL cpp/complex-block.
    using T = ConnectionState;
    static constexpr struct { T target; bool valid; } table[9][11] = {
        // State: Disconnected
        {
            {T::Connecting,  true },  // Connect
            {T::Disconnected, false},  // ConnectSuccess
            {T::Disconnected, false},  // ConnectFailed
            {T::Disconnected, false},  // HandshakeStarted
            {T::Disconnected, false},  // HandshakeSuccess
            {T::Disconnected, false},  // HandshakeFailed
            {T::Disconnected, false},  // Disconnect
            {T::Disconnected, false},  // ConnectionLost
            {T::Disconnected, false},  // BackoffExpired
            {T::Connecting,  true },  // RetryRequested
            {T::Disconnected, false},  // FatalError
        },
        // State: Connecting
        {
            {T::Disconnected, false},  // Connect
            {T::Connected,   true },  // ConnectSuccess
            {T::Backoff,     true },  // ConnectFailed
            {T::Disconnected, false},  // HandshakeStarted
            {T::Disconnected, false},  // HandshakeSuccess
            {T::Disconnected, false},  // HandshakeFailed
            {T::Disconnected, true },  // Disconnect
            {T::Disconnected, false},  // ConnectionLost
            {T::Disconnected, false},  // BackoffExpired
            {T::Disconnected, false},  // RetryRequested
            {T::Error,       true },  // FatalError
        },
        // State: Connected
        {
            {T::Disconnected, false},  // Connect
            {T::Disconnected, false},  // ConnectSuccess
            {T::Disconnected, false},  // ConnectFailed
            {T::Handshaking, true },  // HandshakeStarted
            {T::Ready,       true },  // HandshakeSuccess
            {T::Backoff,      true },  // HandshakeFailed
            {T::Disconnecting, true},  // Disconnect
            {T::Backoff,      true },  // ConnectionLost
            {T::Disconnected, false},  // BackoffExpired
            {T::Disconnected, false},  // RetryRequested
            {T::Error,        true },  // FatalError
        },
        // State: Handshaking
        {
            {T::Disconnected, false},  // Connect
            {T::Disconnected, false},  // ConnectSuccess
            {T::Disconnected, false},  // ConnectFailed
            {T::Disconnected, false},  // HandshakeStarted
            {T::Ready,       true },  // HandshakeSuccess
            {T::Backoff,      true },  // HandshakeFailed
            {T::Disconnecting, true},  // Disconnect
            {T::Backoff,      true },  // ConnectionLost
            {T::Disconnected, false},  // BackoffExpired
            {T::Disconnected, false},  // RetryRequested
            {T::Error,        true },  // FatalError
        },
        // State: Ready
        {
            {T::Disconnected, false},  // Connect
            {T::Disconnected, false},  // ConnectSuccess
            {T::Disconnected, false},  // ConnectFailed
            {T::Disconnected, false},  // HandshakeStarted
            {T::Disconnected, false},  // HandshakeSuccess
            {T::Disconnected, false},  // HandshakeFailed
            {T::Disconnecting, true },  // Disconnect
            {T::Backoff,      true },  // ConnectionLost
            {T::Disconnected, false},  // BackoffExpired
            {T::Disconnected, false},  // RetryRequested
            {T::Error,        true },  // FatalError
        },
        // State: Backoff
        {
            {T::Disconnected, false},  // Connect
            {T::Disconnected, false},  // ConnectSuccess
            {T::Disconnected, false},  // ConnectFailed
            {T::Disconnected, false},  // HandshakeStarted
            {T::Disconnected, false},  // HandshakeSuccess
            {T::Disconnected, false},  // HandshakeFailed
            {T::Disconnected, true },  // Disconnect
            {T::Disconnected, false},  // ConnectionLost
            {T::Retrying,    true },  // BackoffExpired
            {T::Retrying,    true },  // RetryRequested
            {T::Error,       true },  // FatalError
        },
        // State: Retrying
        {
            {T::Disconnected, false},  // Connect
            {T::Connected,   true },  // ConnectSuccess
            {T::Backoff,     true },  // ConnectFailed
            {T::Disconnected, false},  // HandshakeStarted
            {T::Disconnected, false},  // HandshakeSuccess
            {T::Disconnected, false},  // HandshakeFailed
            {T::Disconnected, true },  // Disconnect
            {T::Disconnected, false},  // ConnectionLost
            {T::Disconnected, false},  // BackoffExpired
            {T::Disconnected, false},  // RetryRequested
            {T::Error,       true },  // FatalError
        },
        // State: Disconnecting
        //
        // Disconnecting is entered from Connected/Handshaking/Ready on a
        // user-initiated Disconnect. The TCP/socket teardown runs in the
        // background while the FSM waits for the transport to confirm the
        // close. Two events in particular were previously marked invalid
        // (valid=false), which would leave the machine stuck in
        // Disconnecting if they fired during teardown:
        //
        //   - HandshakeSuccess: when the user disconnects while a handshake
        //     is in flight, the server's success reply can still arrive
        //     before the FIN is processed. Treating it as invalid dropped
        //     the event on the floor and the machine never reached
        //     Disconnected. It is now a valid transition to Disconnected:
        //     the handshake succeeding does not cancel the pending
        //     disconnect, it just means the transport reached a clean state
        //     before being torn down.
        //   - HandshakeFailed: symmetric case — the handshake can fail
        //     while we are already tearing down. The teardown result is
        //     the same (we end up Disconnected), so we accept the event
        //     instead of stalling.
        //
        // The other transitions in this state (Connect, ConnectSuccess,
        // ConnectFailed, HandshakeStarted, Disconnect, BackoffExpired,
        // RetryRequested) remain invalid: they describe fresh connection
        // attempts that cannot start while a disconnect is in progress.
        // ConnectionLost and FatalError are already valid and lead to
        // Disconnected, which is the correct terminal state.
        {
            {T::Disconnected, false},  // Connect
            {T::Disconnected, true },  // ConnectSuccess
            {T::Disconnected, true },  // ConnectFailed
            {T::Disconnected, false},  // HandshakeStarted
            {T::Disconnected, true },  // HandshakeSuccess  (was invalid — fix)
            {T::Disconnected, true },  // HandshakeFailed   (was invalid — fix)
            {T::Disconnected, false},  // Disconnect
            {T::Disconnected, true },  // ConnectionLost
            {T::Disconnected, false},  // BackoffExpired
            {T::Disconnected, false},  // RetryRequested
            {T::Disconnected, true },  // FatalError
        },
        // State: Error
        {
            {T::Disconnected, false},  // Connect
            {T::Disconnected, false},  // ConnectSuccess
            {T::Disconnected, false},  // ConnectFailed
            {T::Disconnected, false},  // HandshakeStarted
            {T::Disconnected, false},  // HandshakeSuccess
            {T::Disconnected, false},  // HandshakeFailed
            {T::Disconnected, true },  // Disconnect
            {T::Disconnected, false},  // ConnectionLost
            {T::Disconnected, false},  // BackoffExpired
            {T::Connecting,  true },  // RetryRequested
            {T::Disconnected, false},  // FatalError
        },
    };

    static_assert(sizeof(table) / sizeof(table[0]) == 9, "State count mismatch");
    static_assert(sizeof(table[0]) / sizeof(table[0][0]) == 11, "Event count mismatch");

    const auto si = static_cast<size_t>(from);
    const auto ei = static_cast<size_t>(event);

    if (si >= 9 || ei >= 11) {
        return false;
    }

    const auto& entry = table[si][ei];
    if (entry.valid) {
        to = entry.target;
    }
    return entry.valid;
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

    // Log every TCP-client state change — critical for diagnosing silent
    // disconnects. Previously the machine would leave Ready on a quiet
    // ConnectionLost and SendProxyDataToServer would start returning
    // NotConnected with no trace of what transitioned.
    LOG_INFO("TCP state: %s -> %s (event=%s)",
             state_to_string(old_state),
             state_to_string(new_state),
             event_to_string(event));

    // Entering the Error state is unrecoverable and rare (today only triggered
    // by a protocol VersionMismatch rejection from the server — see
    // RyuLdnClient::HandleServerPacket). Surface it at LOG_ERROR so it is not
    // drowned in the steady-state LOG_INFO stream above.
    if (new_state == ConnectionState::Error) {
        LOG_ERROR("Connection state machine entered unrecoverable Error state: "
                  "%s -> Error (event=%s). Manual disconnect/reconnect required.",
                  state_to_string(old_state),
                  event_to_string(event));
    }

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
    // LOG_WARN on every call: this bypasses the state machine's transition
    // validation table, so it should never be used on a hot path. Legitimate
    // uses are:
    //   - Unit tests that need to drive the FSM into a specific state without
    //     replaying the event sequence (the only production caller today).
    //   - Fatal-error recovery in a shutdown path where the regular event
    //     vocabulary cannot express the required transition.
    // Any new production caller must justify here why process_event() cannot
    // reach the same state through a valid transition.
    LOG_WARN("force_state: bypassing state machine validation, forcing state=%d (%s) from state=%d",
             static_cast<int>(state), state_to_string(state), static_cast<int>(m_state));
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
