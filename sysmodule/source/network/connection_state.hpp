/**
 * @file connection_state.hpp
 * @brief Connection state machine for managing network connection lifecycle
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <cstdint>

namespace ryu_ldn {
namespace network {

/**
 * @brief Connection states for the state machine
 */
enum class ConnectionState : uint8_t {
    Disconnected,   ///< Not connected, idle
    Connecting,     ///< Connection attempt in progress
    Connected,      ///< Successfully connected
    Handshaking,    ///< TCP connected, performing protocol handshake
    Ready,          ///< Fully connected and handshake complete
    Backoff,        ///< Waiting before retry (after failure)
    Retrying,       ///< Retry attempt in progress
    Disconnecting,  ///< Graceful disconnect in progress
    Error           ///< Unrecoverable error state
};

/**
 * @brief Events that can trigger state transitions
 */
enum class ConnectionEvent : uint8_t {
    Connect,            ///< Request to connect
    ConnectSuccess,     ///< TCP connection established
    ConnectFailed,      ///< TCP connection failed
    HandshakeStarted,   ///< Protocol handshake initiated (sent Initialize)
    HandshakeSuccess,   ///< Protocol handshake completed
    HandshakeFailed,    ///< Protocol handshake failed
    Disconnect,         ///< Request to disconnect
    ConnectionLost,     ///< Connection unexpectedly lost
    BackoffExpired,     ///< Backoff timer expired, ready to retry
    RetryRequested,     ///< Manual retry requested
    FatalError          ///< Unrecoverable error occurred
};

/**
 * @brief Result of a state transition
 */
enum class TransitionResult : uint8_t {
    Success,            ///< Transition completed successfully
    InvalidTransition,  ///< Transition not allowed from current state
    AlreadyInState      ///< Already in the requested state
};

/**
 * @brief Callback type for state change notifications
 * @param old_state Previous state
 * @param new_state New state
 * @param event Event that triggered the transition
 */
using StateChangeCallback = void (*)(ConnectionState old_state,
                                      ConnectionState new_state,
                                      ConnectionEvent event);

/**
 * @brief Connection state machine
 *
 * Manages the connection lifecycle with well-defined states and transitions.
 * Thread-safe when using the provided atomic operations.
 *
 * State diagram:
 * @code
 *                    Connect
 *   Disconnected ──────────────► Connecting
 *        ▲                           │
 *        │                    Success│Fail
 *        │                           ▼
 *        │    Disconnect        Connected
 *        ◄────────────────────      │
 *        │                    Handshake
 *        │                           ▼
 *        │                    Handshaking
 *        │                           │
 *        │                  Success  │  Fail
 *        │                     ▼     ▼
 *        │                   Ready  Backoff
 *        │                     │       │
 *        │      ConnectionLost │       │ BackoffExpired
 *        │                     ▼       ▼
 *        ◄────────────────── Backoff ► Retrying ──► Connecting
 * @endcode
 */
class ConnectionStateMachine {
public:
    /**
     * @brief Constructor - starts in Disconnected state
     */
    ConnectionStateMachine();

    /**
     * @brief Get current connection state
     * @return Current state
     */
    ConnectionState get_state() const { return m_state; }

    /**
     * @brief Check if currently connected (Connected, Handshaking, or Ready)
     * @return true if in a connected state
     */
    bool is_connected() const;

    /**
     * @brief Check if fully ready (handshake complete)
     * @return true if in Ready state
     */
    bool is_ready() const { return m_state == ConnectionState::Ready; }

    /**
     * @brief Check if in a transitional state (Connecting, Handshaking, etc.)
     * @return true if transitioning
     */
    bool is_transitioning() const;

    /**
     * @brief Process an event and perform state transition
     * @param event Event to process
     * @return Result of the transition attempt
     */
    TransitionResult process_event(ConnectionEvent event);

    /**
     * @brief Set callback for state changes
     * @param callback Function to call on state change (nullptr to disable)
     */
    void set_state_change_callback(StateChangeCallback callback);

    /**
     * @brief Get retry count since last successful connection
     * @return Number of retry attempts
     */
    uint32_t get_retry_count() const { return m_retry_count; }

    /**
     * @brief Reset retry count (call on successful connection)
     */
    void reset_retry_count() { m_retry_count = 0; }

    /**
     * @brief Force state (use with caution, bypasses transition validation)
     * @param state State to force
     */
    void force_state(ConnectionState state);

    /**
     * @brief Convert state to string for logging
     * @param state State to convert
     * @return Human-readable state name
     */
    static const char* state_to_string(ConnectionState state);

    /**
     * @brief Convert event to string for logging
     * @param event Event to convert
     * @return Human-readable event name
     */
    static const char* event_to_string(ConnectionEvent event);

private:
    ConnectionState m_state;
    StateChangeCallback m_callback;
    uint32_t m_retry_count;

    /**
     * @brief Check if transition is valid
     * @param from Current state
     * @param event Event
     * @param to Output: target state if valid
     * @return true if transition is valid
     */
    bool is_valid_transition(ConnectionState from, ConnectionEvent event,
                             ConnectionState& to) const;

    /**
     * @brief Perform the state transition
     * @param new_state Target state
     * @param event Event that caused transition
     */
    void transition_to(ConnectionState new_state, ConnectionEvent event);
};

/**
 * @brief Convert TransitionResult to string
 * @param result Result to convert
 * @return Human-readable result string
 */
inline const char* transition_result_to_string(TransitionResult result) {
    switch (result) {
        case TransitionResult::Success:           return "Success";
        case TransitionResult::InvalidTransition: return "InvalidTransition";
        case TransitionResult::AlreadyInState:    return "AlreadyInState";
        default:                                  return "Unknown";
    }
}

} // namespace network
} // namespace ryu_ldn
