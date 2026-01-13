/**
 * @file ldn_state_machine.hpp
 * @brief LDN Communication State Machine
 *
 * Thread-safe state machine for managing LDN communication states
 * with proper transition validation.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include "ldn_types.hpp"

namespace ams::mitm::ldn {

/**
 * @brief Result codes for state transitions
 */
enum class StateTransitionResult {
    Success,            ///< Transition successful
    InvalidTransition,  ///< Transition not allowed from current state
    AlreadyInState,     ///< Already in the target state
};

/**
 * @brief LDN State Machine
 *
 * Manages the LDN communication state with thread-safe transitions
 * and automatic event signaling on state changes.
 *
 * ## State Diagram
 *
 * ```
 *                    +------+
 *                    | None |
 *                    +------+
 *                       |
 *                  Initialize
 *                       v
 *                +-------------+
 *                | Initialized |
 *                +-------------+
 *               /               \
 *       OpenAccessPoint      OpenStation
 *             /                   \
 *            v                     v
 *     +-------------+         +---------+
 *     | AccessPoint |         | Station |
 *     +-------------+         +---------+
 *            |                     |
 *      CreateNetwork            Connect
 *            v                     v
 * +--------------------+   +------------------+
 * | AccessPointCreated |   | StationConnected |
 * +--------------------+   +------------------+
 * ```
 *
 * Any state can transition to Error on fatal errors.
 * Finalize from any state returns to None.
 */
class LdnStateMachine {
public:
    using StateCallback = void (*)(CommState old_state, CommState new_state, void* user_data);

    /**
     * @brief Constructor
     */
    LdnStateMachine();

    /**
     * @brief Destructor
     */
    ~LdnStateMachine();

    // Non-copyable
    LdnStateMachine(const LdnStateMachine&) = delete;
    LdnStateMachine& operator=(const LdnStateMachine&) = delete;

    // ========================================================================
    // State Queries
    // ========================================================================

    /**
     * @brief Get current state
     * @return Current CommState
     */
    CommState GetState() const;

    /**
     * @brief Check if in a specific state
     * @param state State to check
     * @return true if in the specified state
     */
    bool IsInState(CommState state) const;

    /**
     * @brief Check if initialized (not None or Error)
     * @return true if service is initialized
     */
    bool IsInitialized() const;

    /**
     * @brief Check if in a connected state (AccessPointCreated or StationConnected)
     * @return true if connected to a network
     */
    bool IsNetworkActive() const;

    // ========================================================================
    // State Transitions
    // ========================================================================

    /**
     * @brief Initialize the service (None -> Initialized)
     * @return Transition result
     */
    StateTransitionResult Initialize();

    /**
     * @brief Finalize the service (Any -> None)
     * @return Transition result
     */
    StateTransitionResult Finalize();

    /**
     * @brief Open access point mode (Initialized -> AccessPoint)
     * @return Transition result
     */
    StateTransitionResult OpenAccessPoint();

    /**
     * @brief Close access point mode (AccessPoint/AccessPointCreated -> Initialized)
     * @return Transition result
     */
    StateTransitionResult CloseAccessPoint();

    /**
     * @brief Create network (AccessPoint -> AccessPointCreated)
     * @return Transition result
     */
    StateTransitionResult CreateNetwork();

    /**
     * @brief Destroy network (AccessPointCreated -> AccessPoint)
     * @return Transition result
     */
    StateTransitionResult DestroyNetwork();

    /**
     * @brief Open station mode (Initialized -> Station)
     * @return Transition result
     */
    StateTransitionResult OpenStation();

    /**
     * @brief Close station mode (Station/StationConnected -> Initialized)
     * @return Transition result
     */
    StateTransitionResult CloseStation();

    /**
     * @brief Connect to network (Station -> StationConnected)
     * @return Transition result
     */
    StateTransitionResult Connect();

    /**
     * @brief Disconnect from network (StationConnected -> Station)
     * @return Transition result
     */
    StateTransitionResult Disconnect();

    /**
     * @brief Set error state (Any -> Error)
     * @return Transition result
     */
    StateTransitionResult SetError();

    // ========================================================================
    // Event Management
    // ========================================================================

    /**
     * @brief Get the state change event handle
     * @return Event handle for state changes
     */
    os::NativeHandle GetStateChangeEventHandle() const;

    /**
     * @brief Set callback for state changes
     * @param callback Callback function (can be nullptr)
     * @param user_data User data passed to callback
     */
    void SetStateCallback(StateCallback callback, void* user_data);

    // ========================================================================
    // Utilities
    // ========================================================================

    /**
     * @brief Convert state to string for logging
     * @param state State to convert
     * @return Human-readable state name
     */
    static const char* StateToString(CommState state);

    /**
     * @brief Convert transition result to string
     * @param result Result to convert
     * @return Human-readable result name
     */
    static const char* ResultToString(StateTransitionResult result);

private:
    /**
     * @brief Perform state transition if valid
     * @param new_state Target state
     * @return Transition result
     */
    StateTransitionResult TransitionTo(CommState new_state);

    /**
     * @brief Check if transition is valid
     * @param from Source state
     * @param to Target state
     * @return true if transition is allowed
     */
    static bool IsValidTransition(CommState from, CommState to);

    /**
     * @brief Signal state change event
     */
    void SignalStateChange();

private:
    mutable os::SdkMutex m_mutex;           ///< Mutex for thread safety
    CommState m_state;                       ///< Current state
    os::SystemEvent m_state_event;           ///< State change event

    StateCallback m_callback;                ///< Optional state change callback
    void* m_callback_user_data;              ///< User data for callback
};

} // namespace ams::mitm::ldn
