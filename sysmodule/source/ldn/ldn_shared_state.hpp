/**
 * @file ldn_shared_state.hpp
 * @brief Shared runtime state singleton for LDN information
 *
 * This module provides a thread-safe singleton for sharing runtime LDN state
 * between the MITM service (ldn:u) and the configuration service (ryu:cfg).
 *
 * The MITM service updates the state when:
 * - A game initializes/finalizes LDN
 * - LDN state transitions occur
 * - Session info changes (players join/leave)
 * - RTT measurements are received
 *
 * The configuration service reads the state to:
 * - Report game active status to the overlay
 * - Provide runtime LDN info (state, session, latency)
 * - Handle force reconnect requests
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>

// Forward declare CommState to avoid circular includes
// CommState is defined in ldn_types.hpp
namespace ams::mitm::ldn {
    enum class CommState : u32;
}

namespace ams::mitm::ldn {

/**
 * @brief Session information structure
 *
 * Contains information about the current LDN session.
 */
struct SessionInfo {
    u8 node_count;      ///< Current number of nodes in session
    u8 max_nodes;       ///< Maximum nodes allowed in session
    u8 local_node_id;   ///< This node's ID in the session
    u8 is_host;         ///< 1 if this node is the host, 0 otherwise
    u8 reserved[4];     ///< Reserved for future use
};
static_assert(sizeof(SessionInfo) == 8, "SessionInfo must be 8 bytes for IPC");

/**
 * @brief Shared runtime state singleton
 *
 * This class provides a thread-safe singleton for sharing runtime LDN state
 * between the MITM service (which updates the state) and the ryu:cfg service
 * (which exposes it to the overlay).
 *
 * All methods are thread-safe and use a mutex for synchronization.
 *
 * @example
 * @code
 * // In MITM service (ICommunicationService)
 * auto& state = SharedState::GetInstance();
 * state.SetGameActive(true, client_process_id);
 * state.SetLdnState(CommState::Initialized);
 *
 * // In config service (ryu:cfg)
 * auto& state = SharedState::GetInstance();
 * if (state.IsGameActive()) {
 *     CommState ldn_state = state.GetLdnState();
 *     // Return to overlay
 * }
 * @endcode
 */
class SharedState {
public:
    /**
     * @brief Get the singleton instance
     *
     * @return Reference to the global SharedState instance
     */
    static SharedState& GetInstance();

    /**
     * @brief Reset all state to defaults
     *
     * Used for testing and cleanup.
     */
    void Reset();

    // =========================================================================
    // Game Active State
    // =========================================================================

    /**
     * @brief Set game active state
     *
     * Called by MITM service when a game initializes or finalizes LDN.
     * When set to false, also resets all runtime state.
     *
     * @param active True if a game is using LDN
     * @param process_id Process ID of the game (0 when inactive)
     */
    void SetGameActive(bool active, u64 process_id);

    /**
     * @brief Check if a game is actively using LDN
     *
     * @return True if a game is using LDN
     */
    bool IsGameActive() const;

    /**
     * @brief Get the process ID of the active game
     *
     * @return Process ID, or 0 if no game is active
     */
    u64 GetActiveProcessId() const;

    /**
     * @brief Set the PID that has opened ldn:u service
     *
     * Called immediately when LdnMitMService is created, BEFORE Initialize().
     * This allows BSD MITM to know which process to intercept even before
     * the game calls Initialize().
     *
     * @param pid Process ID that opened ldn:u, or 0 to clear
     */
    void SetLdnPid(u64 pid);

    /**
     * @brief Get the PID that has opened ldn:u service
     *
     * @return Process ID, or 0 if no process has opened ldn:u
     */
    u64 GetLdnPid() const;

    /**
     * @brief Check if a PID has opened ldn:u
     *
     * @param pid Process ID to check
     * @return true if this PID has opened ldn:u
     */
    bool IsLdnPid(u64 pid) const;

    // =========================================================================
    // LDN State
    // =========================================================================

    /**
     * @brief Set current LDN communication state
     *
     * Called by MITM service on state transitions.
     *
     * @param state New LDN state
     */
    void SetLdnState(CommState state);

    /**
     * @brief Get current LDN communication state
     *
     * @return Current LDN state
     */
    CommState GetLdnState() const;

    // =========================================================================
    // Session Info
    // =========================================================================

    /**
     * @brief Set session information
     *
     * Called by MITM service when network info is updated.
     *
     * @param node_count Current number of nodes
     * @param max_nodes Maximum nodes allowed
     * @param local_node_id This node's ID
     * @param is_host True if this node is the host
     */
    void SetSessionInfo(u8 node_count, u8 max_nodes, u8 local_node_id, bool is_host);

    /**
     * @brief Get session information
     *
     * @param[out] node_count Current number of nodes
     * @param[out] max_nodes Maximum nodes allowed
     * @param[out] local_node_id This node's ID
     * @param[out] is_host True if this node is the host
     */
    void GetSessionInfo(u8& node_count, u8& max_nodes, u8& local_node_id, bool& is_host) const;

    /**
     * @brief Get session information as struct
     *
     * @return SessionInfo structure for IPC
     */
    SessionInfo GetSessionInfoStruct() const;

    // =========================================================================
    // RTT (Round-Trip Time)
    // =========================================================================

    /**
     * @brief Set last measured RTT
     *
     * Called by network client after ping response.
     *
     * @param rtt_ms Round-trip time in milliseconds
     */
    void SetLastRtt(u32 rtt_ms);

    /**
     * @brief Get last measured RTT
     *
     * @return Round-trip time in milliseconds, or 0 if not measured
     */
    u32 GetLastRtt() const;

    // =========================================================================
    // Reconnect Request
    // =========================================================================

    /**
     * @brief Request a reconnection
     *
     * Called by config service when user requests reconnect from overlay.
     * The MITM service should periodically check and consume this flag.
     */
    void RequestReconnect();

    /**
     * @brief Consume reconnect request
     *
     * Called by MITM service to check and clear the reconnect flag.
     *
     * @return True if reconnect was requested (flag is cleared)
     */
    bool ConsumeReconnectRequest();

private:
    SharedState() = default;
    SharedState(const SharedState&) = delete;
    SharedState& operator=(const SharedState&) = delete;

    mutable ams::os::SdkMutex m_mutex{};
    bool m_game_active = false;
    u64 m_process_id = 0;
    u64 m_ldn_pid = 0;  ///< PID that opened ldn:u (set before Initialize)
    CommState m_ldn_state = static_cast<CommState>(0); // CommState::None
    u8 m_node_count = 0;
    u8 m_max_nodes = 0;
    u8 m_local_node_id = 0;
    bool m_is_host = false;
    u32 m_last_rtt_ms = 0;
    bool m_reconnect_requested = false;
};

} // namespace ams::mitm::ldn
