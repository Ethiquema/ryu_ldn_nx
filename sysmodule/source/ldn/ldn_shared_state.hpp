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
#include <unordered_set>
#include <vector>
#include "ldn_types.hpp"
#include <atomic>

namespace ams::mitm::ldn {

// SessionInfo and CommState are defined in ldn_types.hpp

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
    /// @gdb{tag="LDN:STATE", msg="SharedState: constructor"}
    static SharedState& GetInstance();

    /**
     * @brief Reset all state to defaults
     *
     * Used for testing and cleanup.
     */
    /// @gdb{tag="LDN:STATE", msg="SharedState: Reset"}
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
    /// @gdb{tag="LDN:STATE", msg="SetGameActive: active=%d pid=%lu", args="$x1, $x2"}
    void SetGameActive(bool active, u64 process_id);

    /**
     * @brief Check if a game is actively using LDN
     *
     * @return True if a game is using LDN
     */
    /// @gdb{tag="LDN:STATE", msg="IsGameActive"}
    bool IsGameActive() const;

    /**
     * @brief Get the process ID of the active game
     *
     * @return Process ID, or 0 if no game is active
     */
    /// @gdb{tag="LDN:STATE", msg="GetActiveProcessId"}
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
    /// @gdb{tag="LDN:STATE", msg="SetLdnPid: pid=%lu", args="$x1"}
    void SetLdnPid(u64 pid);

    /**
     * @brief Get the PID that has opened ldn:u service
     *
     * @return Process ID, or 0 if no process has opened ldn:u
     */
    /// @gdb{tag="LDN:STATE", msg="GetLdnPid"}
    u64 GetLdnPid() const;

    /**
     * @brief Check if a PID has opened ldn:u
     *
     * @param pid Process ID to check
     * @return true if this PID has opened ldn:u
     */
    /// @gdb{tag="LDN:STATE", msg="IsLdnPid: pid=%lu", args="$x1"}
    bool IsLdnPid(u64 pid) const;

    // =========================================================================
    // LDN Game Whitelist (for BSD MITM)
    // =========================================================================

    /**
     * @brief Load the LDN game whitelist
     *
     * Called at startup to load the list of games that support LDN.
     * This replaces any existing whitelist.
     *
     * @param game_ids Vector of program IDs that support LDN
     */
    /// @gdb{tag="LDN:STATE", msg="LoadLdnWhitelist"}
    void LoadLdnWhitelist(const std::vector<u64>& game_ids);

    /**
     * @brief Check if a program_id is in the LDN whitelist
     *
     * Called by BSD ShouldMitm to decide whether to intercept.
     *
     * @param program_id The program ID to check
     * @return true if this program is in the whitelist
     */
    /// @gdb{tag="LDN:STATE", msg="IsLdnGame: program_id=0x%lx", args="$x1"}
    bool IsLdnGame(u64 program_id) const;

    /**
     * @brief Get the number of games in the whitelist
     *
     * @return Number of games loaded
     */
    /// @gdb{tag="LDN:STATE", msg="GetWhitelistSize"}
    size_t GetWhitelistSize() const;

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
    /// @gdb{tag="LDN:STATE", msg="SetLdnState: state=%d", args="$x1"}
    void SetLdnState(CommState state);

    /**
     * @brief Get current LDN communication state
     *
     * @return Current LDN state
     */
    /// @gdb{tag="LDN:STATE", msg="GetLdnState"}
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
    /// @gdb{tag="LDN:STATE", msg="SetSessionInfo: node_count=%d max=%d local=%d is_host=%d", args="$x1, $x2, $x3, $x4"}
    void SetSessionInfo(u8 node_count, u8 max_nodes, u8 local_node_id, bool is_host);

    /**
     * @brief Get session information
     *
     * @param[out] node_count Current number of nodes
     * @param[out] max_nodes Maximum nodes allowed
     * @param[out] local_node_id This node's ID
     * @param[out] is_host True if this node is the host
     */
    /// @gdb{tag="LDN:STATE", msg="GetSessionInfo"}
    void GetSessionInfo(u8& node_count, u8& max_nodes, u8& local_node_id, bool& is_host) const;

    /**
     * @brief Get session information as struct
     *
     * @return SessionInfo structure for IPC
     */
    /// @gdb{tag="LDN:STATE", msg="GetSessionInfoStruct"}
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
    /// @gdb{tag="LDN:STATE", msg="SetLastRtt: rtt=%u", args="$x1"}
    void SetLastRtt(u32 rtt_ms);

    /**
     * @brief Get last measured RTT
     *
     * @return Round-trip time in milliseconds, or 0 if not measured
     */
    /// @gdb{tag="LDN:STATE", msg="GetLastRtt"}
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
    /// @gdb{tag="LDN:STATE", msg="RequestReconnect"}
    void RequestReconnect();

    /**
     * @brief Consume reconnect request
     *
     * Called by MITM service to check and clear the reconnect flag.
     *
     * @return True if reconnect was requested (flag is cleared)
     */
    /// @gdb{tag="LDN:STATE", msg="ConsumeReconnectRequest"}
    bool ConsumeReconnectRequest();

    // =========================================================================
    // Active Session Counting (IPC session exit detection)
    // =========================================================================

    /**
     * @brief Increment the active IPC session count
     *
     * Called in the constructor of each MITM session (ldn:u and bsd:u).
     * When the game dies, Atmosphere closes all sessions and the destructor
     * decrements the counter for each one.
     */
    /// @gdb{tag="LDN:STATE", msg="IncrementSessionCount"}
    void IncrementSessionCount();

    /**
     * @brief Decrement the active IPC session count
     *
     * Called in the destructor of each MITM session.
     * Returns the new count after decrement.
     * When it reaches 0, no more sessions exist — the game has exited.
     */
    /// @gdb{tag="LDN:STATE", msg="DecrementSessionCount"}
    u32 DecrementSessionCount();

    /**
     * @brief Get the current active session count
     *
     * @return Number of active IPC sessions (ldn:u + bsd:u)
     */
    /// @gdb{tag="LDN:STATE", msg="GetActiveSessionCount"}
    u32 GetActiveSessionCount() const;

private:
    /// @gdb{tag="LDN:STATE", msg="SharedState: constructor"}
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
    std::atomic<u32> m_active_sessions{0};  ///< Active IPC session count (ldn:u + bsd:u)
    std::unordered_set<u64> m_ldn_games;  ///< Set of program_ids with LDN support
};

} // namespace ams::mitm::ldn
