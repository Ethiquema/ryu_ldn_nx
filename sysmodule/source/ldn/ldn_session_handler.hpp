/**
 * @file ldn_session_handler.hpp
 * @brief LDN Session Handler - Manages LDN session state and packet processing
 *
 * This module provides a high-level handler for LDN session management.
 * It processes incoming packets and maintains session state (network info,
 * node list, connection status, etc.).
 *
 * ## Architecture
 *
 * The session handler sits between the packet dispatcher and the application:
 *
 * ```
 * +------------------+     +---------------------+     +---------------+
 * | PacketDispatcher | --> | LdnSessionHandler   | --> | Application   |
 * | (routing)        |     | (state management)  |     | (game logic)  |
 * +------------------+     +---------------------+     +---------------+
 * ```
 *
 * ## Session States (matching RyuLDN client)
 *
 * - **None**: Not initialized, no server connection
 * - **Initialized**: Connected to server, handshake complete
 * - **Station**: Joined a network as client
 * - **StationConnected**: Fully connected as station (deprecated, use Station)
 * - **AccessPoint**: Created a network as host
 * - **AccessPointCreated**: Network created (deprecated, use AccessPoint)
 * - **Error**: Error state, needs reset
 *
 * ## Usage Example
 *
 * ```cpp
 * LdnSessionHandler session;
 *
 * session.set_state_callback([](LdnSessionState old, LdnSessionState new) {
 *     printf("State: %d -> %d\n", old, new);
 * });
 *
 * session.set_network_updated_callback([](const NetworkInfo& info) {
 *     printf("Network updated: %d players\n", info.ldn.node_count);
 * });
 *
 * // Packets are routed from dispatcher to handler methods:
 * session.handle_initialize(header, msg);
 * session.handle_connected(header, info);
 * session.handle_sync_network(header, info);
 * ```
 *
 * @see ldn_packet_dispatcher.hpp for packet routing
 * @see LdnSession.cs in RyuLDN server for reference
 */

#pragma once

#include <cstdint>
#include <cstddef>

#include "../protocol/types.hpp"

namespace ryu_ldn::ldn {

/**
 * @brief LDN Session States
 *
 * Represents the current state of the LDN session.
 * Maps to Ryujinx NetworkState enum.
 */
enum class LdnSessionState : uint8_t {
    None = 0,               ///< Not initialized
    Initialized = 1,        ///< Server handshake complete, idle
    Station = 2,            ///< Joined a network as client
    StationConnected = 3,   ///< Connected as station (for compatibility)
    AccessPoint = 4,        ///< Created a network as host
    AccessPointCreated = 5, ///< Access point active (for compatibility)
    Error = 6               ///< Error state
};

/**
 * @brief Convert LdnSessionState to string
 *
 * @param state Session state
 * @return Human-readable string
 */
inline const char* ldn_session_state_to_string(LdnSessionState state) {
    switch (state) {
        case LdnSessionState::None:               return "None";
        case LdnSessionState::Initialized:        return "Initialized";
        case LdnSessionState::Station:            return "Station";
        case LdnSessionState::StationConnected:   return "StationConnected";
        case LdnSessionState::AccessPoint:        return "AccessPoint";
        case LdnSessionState::AccessPointCreated: return "AccessPointCreated";
        case LdnSessionState::Error:              return "Error";
        default:                                  return "Unknown";
    }
}

// ============================================================================
// Callback Types
// ============================================================================

/**
 * @brief Callback for session state changes
 *
 * @param old_state Previous state
 * @param new_state New state
 */
using SessionStateCallback = void (*)(LdnSessionState old_state, LdnSessionState new_state);

/**
 * @brief Callback for network info updates
 *
 * Called when network info changes (SyncNetwork, Connected).
 *
 * @param info Updated network information
 */
using NetworkUpdatedCallback = void (*)(const protocol::NetworkInfo& info);

/**
 * @brief Callback for scan results
 *
 * Called for each network found during scan.
 *
 * @param info Network found
 */
using ScanResultCallback = void (*)(const protocol::NetworkInfo& info);

/**
 * @brief Callback for scan completion
 *
 * Called when scan is finished (ScanReplyEnd received).
 */
using ScanCompletedCallback = void (*)();

/**
 * @brief Callback for disconnection events
 *
 * Called when a disconnection occurs.
 *
 * @param reason Disconnect reason (implementation-defined)
 */
using DisconnectedCallback = void (*)(uint32_t reason);

/**
 * @brief Callback for network errors
 *
 * Called when NetworkError packet is received.
 *
 * @param code Error code
 */
using ErrorCallback = void (*)(protocol::NetworkErrorCode code);

/**
 * @brief Callback for rejection events
 *
 * Called when a player is rejected/kicked from the session.
 *
 * @param node_id Node ID of rejected player
 * @param reason Rejection reason (DisconnectReason enum)
 */
using RejectedCallback = void (*)(uint32_t node_id, uint32_t reason);

/**
 * @brief Callback for accept policy changes
 *
 * Called when accept policy is confirmed changed.
 *
 * @param policy New accept policy (AcceptPolicy enum)
 */
using AcceptPolicyChangedCallback = void (*)(protocol::AcceptPolicy policy);

// ============================================================================
// LdnSessionHandler Class
// ============================================================================

/**
 * @brief LDN Session Handler
 *
 * Manages LDN session state and processes incoming packets from the server.
 * Maintains network info, node list, and provides callbacks for state changes.
 *
 * ## Thread Safety
 *
 * NOT thread-safe. All methods should be called from the same thread.
 */
class LdnSessionHandler {
public:
    /**
     * @brief Default constructor
     *
     * Creates handler in None state.
     */
    LdnSessionHandler();

    /**
     * @brief Destructor
     */
    ~LdnSessionHandler() = default;

    // Non-copyable
    LdnSessionHandler(const LdnSessionHandler&) = delete;
    LdnSessionHandler& operator=(const LdnSessionHandler&) = delete;

    // ========================================================================
    // Callback Registration
    // ========================================================================

    /**
     * @brief Set callback for state changes
     *
     * @param callback Function to call on state change (nullptr to disable)
     */
    void set_state_callback(SessionStateCallback callback);

    /**
     * @brief Set callback for network info updates
     *
     * @param callback Function to call when network info changes
     */
    void set_network_updated_callback(NetworkUpdatedCallback callback);

    /**
     * @brief Set callback for scan results
     *
     * @param callback Function to call for each scan result
     */
    void set_scan_result_callback(ScanResultCallback callback);

    /**
     * @brief Set callback for scan completion
     *
     * @param callback Function to call when scan ends
     */
    void set_scan_completed_callback(ScanCompletedCallback callback);

    /**
     * @brief Set callback for disconnection events
     *
     * @param callback Function to call on disconnect
     */
    void set_disconnected_callback(DisconnectedCallback callback);

    /**
     * @brief Set callback for errors
     *
     * @param callback Function to call on error
     */
    void set_error_callback(ErrorCallback callback);

    /**
     * @brief Set callback for rejection events
     *
     * @param callback Function to call when a player is rejected
     */
    void set_rejected_callback(RejectedCallback callback);

    /**
     * @brief Set callback for accept policy changes
     *
     * @param callback Function to call when accept policy changes
     */
    void set_accept_policy_changed_callback(AcceptPolicyChangedCallback callback);

    // ========================================================================
    // Packet Handlers
    // ========================================================================

    /**
     * @brief Handle Initialize response from server
     *
     * Called when server responds to our Initialize with assigned ID/MAC.
     * Transitions to Initialized state.
     *
     * @param header Packet header
     * @param msg Initialize message with assigned values
     */
    void handle_initialize(const protocol::LdnHeader& header,
                           const protocol::InitializeMessage& msg);

    /**
     * @brief Handle Connected packet (join success)
     *
     * Called when successfully joined a network.
     * Transitions to Station state.
     *
     * @param header Packet header
     * @param info Network info of joined network
     */
    void handle_connected(const protocol::LdnHeader& header,
                          const protocol::NetworkInfo& info);

    /**
     * @brief Handle SyncNetwork packet
     *
     * Called when network state changes (player join/leave, etc.).
     * Updates stored network info and notifies callback.
     *
     * @param header Packet header
     * @param info Updated network info
     */
    void handle_sync_network(const protocol::LdnHeader& header,
                             const protocol::NetworkInfo& info);

    /**
     * @brief Handle ScanReply packet
     *
     * Called for each network found during scan.
     *
     * @param header Packet header
     * @param info Network found
     */
    void handle_scan_reply(const protocol::LdnHeader& header,
                           const protocol::NetworkInfo& info);

    /**
     * @brief Handle ScanReplyEnd packet
     *
     * Called when scan is complete.
     *
     * @param header Packet header
     */
    void handle_scan_reply_end(const protocol::LdnHeader& header);

    /**
     * @brief Handle Ping packet
     *
     * Processes ping from server or response to our ping.
     *
     * @param header Packet header
     * @param msg Ping message
     * @return true if echo should be sent (server requested), false otherwise
     */
    bool handle_ping(const protocol::LdnHeader& header,
                     const protocol::PingMessage& msg);

    /**
     * @brief Handle Disconnect packet
     *
     * Called when a client disconnects from the session.
     *
     * @param header Packet header
     * @param msg Disconnect message
     */
    void handle_disconnect(const protocol::LdnHeader& header,
                           const protocol::DisconnectMessage& msg);

    /**
     * @brief Handle NetworkError packet
     *
     * Called when server reports an error.
     *
     * @param header Packet header
     * @param msg Error message
     */
    void handle_network_error(const protocol::LdnHeader& header,
                              const protocol::NetworkErrorMessage& msg);

    /**
     * @brief Handle Reject packet
     *
     * Called when a player is rejected/kicked from the session.
     * If we are the rejected player, we leave the session.
     *
     * @param header Packet header
     * @param req Reject request with node_id and reason
     */
    void handle_reject(const protocol::LdnHeader& header,
                       const protocol::RejectRequest& req);

    /**
     * @brief Handle RejectReply packet
     *
     * Called when server confirms a rejection was processed.
     * Usually sent back to the host who initiated the rejection.
     *
     * @param header Packet header
     */
    void handle_reject_reply(const protocol::LdnHeader& header);

    /**
     * @brief Handle SetAcceptPolicy response
     *
     * Called when server confirms accept policy change.
     *
     * @param header Packet header
     * @param req Accept policy request
     */
    void handle_set_accept_policy(const protocol::LdnHeader& header,
                                   const protocol::SetAcceptPolicyRequest& req);

    // ========================================================================
    // State Queries
    // ========================================================================

    /**
     * @brief Get current session state
     *
     * @return Current state
     */
    LdnSessionState get_state() const { return m_state; }

    /**
     * @brief Check if in an active session
     *
     * @return true if in Station or AccessPoint state
     */
    bool is_in_session() const;

    /**
     * @brief Check if we are the host
     *
     * @return true if we created the access point
     */
    bool is_host() const { return m_is_host; }

    /**
     * @brief Get current node count
     *
     * @return Number of connected players
     */
    uint8_t get_node_count() const;

    /**
     * @brief Get maximum nodes for current session
     *
     * @return Maximum players allowed
     */
    uint8_t get_max_nodes() const;

    /**
     * @brief Get our local node ID
     *
     * @return Our node ID (0-7), or -1 if not in session
     */
    int8_t get_local_node_id() const { return m_local_node_id; }

    /**
     * @brief Set our local node ID
     *
     * Called when we know our assigned node ID.
     *
     * @param node_id Node ID (0-7)
     */
    void set_local_node_id(int8_t node_id);

    /**
     * @brief Get current network info
     *
     * @return Reference to stored network info (may be stale if not in session)
     */
    const protocol::NetworkInfo& get_network_info() const { return m_network_info; }

    /**
     * @brief Get last ping ID received from server
     *
     * @return Last ping ID for echo response
     */
    uint8_t get_last_ping_id() const { return m_last_ping_id; }

    /**
     * @brief Get assigned session ID
     *
     * @return Session ID assigned by server
     */
    const protocol::SessionId& get_session_id() const { return m_session_id; }

    /**
     * @brief Get assigned MAC address
     *
     * @return MAC address assigned by server
     */
    const protocol::MacAddress& get_mac_address() const { return m_mac_address; }

    /**
     * @brief Get current accept policy
     *
     * @return Current accept policy (only valid for host)
     */
    protocol::AcceptPolicy get_accept_policy() const { return m_accept_policy; }

    // ========================================================================
    // Actions
    // ========================================================================

    /**
     * @brief Leave current session
     *
     * Transitions back to Initialized state.
     */
    void leave_session();

    /**
     * @brief Reset handler to initial state
     *
     * Clears all state and returns to None.
     */
    void reset();

private:
    // ========================================================================
    // Internal State
    // ========================================================================

    LdnSessionState m_state;                    ///< Current session state
    bool m_is_host;                             ///< Whether we are the host
    int8_t m_local_node_id;                     ///< Our node ID (-1 if not assigned)
    uint8_t m_last_ping_id;                     ///< Last ping ID from server

    protocol::SessionId m_session_id;           ///< Assigned session ID
    protocol::MacAddress m_mac_address;         ///< Assigned MAC address
    protocol::NetworkInfo m_network_info;       ///< Current network info

    protocol::AcceptPolicy m_accept_policy;      ///< Current accept policy

    // Callbacks
    SessionStateCallback m_state_callback;
    NetworkUpdatedCallback m_network_updated_callback;
    ScanResultCallback m_scan_result_callback;
    ScanCompletedCallback m_scan_completed_callback;
    DisconnectedCallback m_disconnected_callback;
    ErrorCallback m_error_callback;
    RejectedCallback m_rejected_callback;
    AcceptPolicyChangedCallback m_accept_policy_changed_callback;

    // ========================================================================
    // Internal Methods
    // ========================================================================

    /**
     * @brief Set state and invoke callback
     *
     * @param new_state New state to set
     */
    void set_state(LdnSessionState new_state);
};

} // namespace ryu_ldn::ldn
