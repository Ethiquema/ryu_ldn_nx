/**
 * @file ldn_session_handler.cpp
 * @brief Implementation of LDN Session Handler
 *
 * This file implements the LDN session management logic that processes
 * incoming packets and maintains session state.
 *
 * ## State Machine
 *
 * The session handler follows this state machine:
 *
 * ```
 *     [None] --Initialize--> [Initialized]
 *                                  |
 *        +---------+---------------+---------------+
 *        |         |                               |
 *        v         v                               v
 *   [Station]  [AccessPoint]                   [Error]
 *        |         |
 *        +---------+
 *             |
 *             v
 *      [Initialized] (on leave/disconnect)
 * ```
 *
 * ## Packet Handling
 *
 * Each handle_* method corresponds to a specific packet type from the server.
 * The handler updates internal state and invokes registered callbacks.
 *
 * @see LdnSession.cs in RyuLDN server for reference implementation
 */

#include "ldn_session_handler.hpp"
#include <cstring>

namespace ryu_ldn::ldn {

// ============================================================================
// Constructor
// ============================================================================

/**
 * @brief Construct a new LdnSessionHandler
 *
 * Initializes all state to defaults:
 * - State: None
 * - Not host
 * - No node ID assigned
 * - No callbacks registered
 */
LdnSessionHandler::LdnSessionHandler()
    : m_state(LdnSessionState::None)
    , m_is_host(false)
    , m_local_node_id(-1)
    , m_last_ping_id(0)
    , m_session_id{}
    , m_mac_address{}
    , m_network_info{}
    , m_accept_policy(protocol::AcceptPolicy::AcceptAll)
    , m_state_callback(nullptr)
    , m_network_updated_callback(nullptr)
    , m_scan_result_callback(nullptr)
    , m_scan_completed_callback(nullptr)
    , m_disconnected_callback(nullptr)
    , m_error_callback(nullptr)
    , m_rejected_callback(nullptr)
    , m_accept_policy_changed_callback(nullptr)
{
}

// ============================================================================
// Callback Registration
// ============================================================================

/**
 * @brief Set callback for state changes
 *
 * The callback is invoked whenever the session state changes.
 * Useful for updating UI or triggering application logic.
 *
 * @param callback Function pointer or nullptr to disable
 */
void LdnSessionHandler::set_state_callback(SessionStateCallback callback) {
    m_state_callback = callback;
}

/**
 * @brief Set callback for network info updates
 *
 * Called when:
 * - Successfully joined a network (Connected)
 * - Network state changes (SyncNetwork)
 *
 * @param callback Function pointer or nullptr to disable
 */
void LdnSessionHandler::set_network_updated_callback(NetworkUpdatedCallback callback) {
    m_network_updated_callback = callback;
}

/**
 * @brief Set callback for scan results
 *
 * Called once for each network found during scan operation.
 * Application should collect these until scan_completed is called.
 *
 * @param callback Function pointer or nullptr to disable
 */
void LdnSessionHandler::set_scan_result_callback(ScanResultCallback callback) {
    m_scan_result_callback = callback;
}

/**
 * @brief Set callback for scan completion
 *
 * Called when ScanReplyEnd is received, indicating no more
 * scan results will be sent for the current scan operation.
 *
 * @param callback Function pointer or nullptr to disable
 */
void LdnSessionHandler::set_scan_completed_callback(ScanCompletedCallback callback) {
    m_scan_completed_callback = callback;
}

/**
 * @brief Set callback for disconnection events
 *
 * Called when:
 * - Another player disconnects from the session
 * - We are kicked from the session
 * - Host closes the session
 *
 * @param callback Function pointer or nullptr to disable
 */
void LdnSessionHandler::set_disconnected_callback(DisconnectedCallback callback) {
    m_disconnected_callback = callback;
}

/**
 * @brief Set callback for error events
 *
 * Called when NetworkError packet is received from server.
 * Application should check the error code and handle appropriately.
 *
 * @param callback Function pointer or nullptr to disable
 */
void LdnSessionHandler::set_error_callback(ErrorCallback callback) {
    m_error_callback = callback;
}

/**
 * @brief Set callback for rejection events
 *
 * Called when a player is rejected/kicked from the session.
 *
 * @param callback Function pointer or nullptr to disable
 */
void LdnSessionHandler::set_rejected_callback(RejectedCallback callback) {
    m_rejected_callback = callback;
}

/**
 * @brief Set callback for accept policy changes
 *
 * Called when accept policy is confirmed changed.
 *
 * @param callback Function pointer or nullptr to disable
 */
void LdnSessionHandler::set_accept_policy_changed_callback(AcceptPolicyChangedCallback callback) {
    m_accept_policy_changed_callback = callback;
}

// ============================================================================
// Packet Handlers
// ============================================================================

/**
 * @brief Handle Initialize response from server
 *
 * The server sends Initialize in response to our Initialize request.
 * This response contains:
 * - Assigned session ID (may be same as requested or server-generated)
 * - Assigned MAC address (may be same as requested or server-generated)
 *
 * After receiving this, we are ready to scan, create, or join networks.
 *
 * @param header Packet header (unused but kept for consistency)
 * @param msg Initialize message containing assigned ID and MAC
 */
void LdnSessionHandler::handle_initialize(const protocol::LdnHeader& header,
                                           const protocol::InitializeMessage& msg) {
    (void)header;  // Unused

    // Store assigned session ID and MAC address
    std::memcpy(&m_session_id, &msg.id, sizeof(m_session_id));
    std::memcpy(&m_mac_address, &msg.mac_address, sizeof(m_mac_address));

    // Transition to Initialized state
    set_state(LdnSessionState::Initialized);
}

/**
 * @brief Handle Connected packet (join success)
 *
 * Server sends Connected when we successfully join a network.
 * The packet contains full NetworkInfo with:
 * - Network ID and configuration
 * - List of all connected nodes
 * - Our assigned node ID (determined by position in node list)
 *
 * @param header Packet header (unused)
 * @param info Complete network information
 */
void LdnSessionHandler::handle_connected(const protocol::LdnHeader& header,
                                          const protocol::NetworkInfo& info) {
    (void)header;

    // Store network info
    std::memcpy(&m_network_info, &info, sizeof(m_network_info));

    // We joined as a station (client), not host
    m_is_host = false;

    // Transition to Station state
    set_state(LdnSessionState::Station);

    // Notify application
    if (m_network_updated_callback) {
        m_network_updated_callback(m_network_info);
    }
}

/**
 * @brief Handle SyncNetwork packet
 *
 * Server broadcasts SyncNetwork to all clients when network state changes:
 * - Player joins
 * - Player leaves
 * - Host changes settings
 * - Advertise data updated
 *
 * This packet is also sent when we successfully create an access point,
 * confirming we are now the host.
 *
 * @param header Packet header (unused)
 * @param info Updated network information
 */
void LdnSessionHandler::handle_sync_network(const protocol::LdnHeader& header,
                                             const protocol::NetworkInfo& info) {
    (void)header;

    // Store updated network info
    std::memcpy(&m_network_info, &info, sizeof(m_network_info));

    // Check if we are the host (node 0)
    // This handles the case where we created an access point
    if (m_local_node_id == 0) {
        m_is_host = true;
        if (m_state == LdnSessionState::Initialized) {
            // We just created an access point
            set_state(LdnSessionState::AccessPoint);
        }
    }

    // If we were in Initialized state and receive SyncNetwork,
    // it might be because we created an access point
    if (m_state == LdnSessionState::Initialized && m_is_host) {
        set_state(LdnSessionState::AccessPoint);
    }

    // Notify application
    if (m_network_updated_callback) {
        m_network_updated_callback(m_network_info);
    }
}

/**
 * @brief Handle ScanReply packet
 *
 * Server sends one ScanReply for each network that matches the scan filter.
 * Multiple ScanReply packets may be received before ScanReplyEnd.
 *
 * The application should collect these results until scan_completed is called.
 *
 * @param header Packet header (unused)
 * @param info Network information for one found network
 */
void LdnSessionHandler::handle_scan_reply(const protocol::LdnHeader& header,
                                           const protocol::NetworkInfo& info) {
    (void)header;

    // Forward to application callback
    if (m_scan_result_callback) {
        m_scan_result_callback(info);
    }
}

/**
 * @brief Handle ScanReplyEnd packet
 *
 * Server sends this after all ScanReply packets to indicate
 * the scan operation is complete. No more networks will be reported
 * for this scan.
 *
 * @param header Packet header (unused)
 */
void LdnSessionHandler::handle_scan_reply_end(const protocol::LdnHeader& header) {
    (void)header;

    // Notify application that scan is complete
    if (m_scan_completed_callback) {
        m_scan_completed_callback();
    }
}

/**
 * @brief Handle Ping packet
 *
 * Ping packets serve two purposes:
 * 1. Server keepalive (requester=0): Server checks if we're still alive.
 *    We must echo the ping back immediately.
 * 2. Our ping response (requester=1): Response to a ping we sent.
 *    Indicates connection is alive.
 *
 * @param header Packet header (unused)
 * @param msg Ping message with requester and ID
 * @return true if we need to echo the ping back (server requested)
 */
bool LdnSessionHandler::handle_ping(const protocol::LdnHeader& header,
                                     const protocol::PingMessage& msg) {
    (void)header;

    m_last_ping_id = msg.id;

    // If requester is 0, server is pinging us - need to echo back
    return (msg.requester == 0);
}

/**
 * @brief Handle Disconnect packet
 *
 * Disconnect packets indicate a client left the session.
 * The disconnect_ip field identifies who disconnected.
 *
 * If the disconnecting IP matches our IP, we've been kicked or
 * the session was closed.
 *
 * @param header Packet header (unused)
 * @param msg Disconnect message with client IP
 */
void LdnSessionHandler::handle_disconnect(const protocol::LdnHeader& header,
                                           const protocol::DisconnectMessage& msg) {
    (void)header;

    // Notify application
    if (m_disconnected_callback) {
        m_disconnected_callback(msg.disconnect_ip);
    }

    // Note: We don't automatically leave the session here.
    // The application should call leave_session() if appropriate
    // after determining if the disconnect affects us.
}

/**
 * @brief Handle NetworkError packet
 *
 * Server sends NetworkError when something goes wrong:
 * - Failed to join (session full, rejected, etc.)
 * - Protocol error
 * - Internal server error
 *
 * The application should check the error code and handle appropriately.
 * Some errors are recoverable, others may require reconnection.
 *
 * @param header Packet header (unused)
 * @param msg Error message with code
 */
void LdnSessionHandler::handle_network_error(const protocol::LdnHeader& header,
                                              const protocol::NetworkErrorMessage& msg) {
    (void)header;

    // Convert to typed error code
    protocol::NetworkErrorCode code =
        static_cast<protocol::NetworkErrorCode>(msg.error_code);

    // Notify application
    if (m_error_callback) {
        m_error_callback(code);
    }
}

/**
 * @brief Handle Reject packet
 *
 * Reject packets are sent when a player is kicked/rejected from the session.
 * This can be initiated by the host or by the server.
 *
 * If the rejected node_id matches our local node ID, we have been kicked
 * and should leave the session.
 *
 * @param header Packet header (unused)
 * @param req Reject request containing node_id and reason
 */
void LdnSessionHandler::handle_reject(const protocol::LdnHeader& header,
                                       const protocol::RejectRequest& req) {
    (void)header;

    // Notify application
    if (m_rejected_callback) {
        m_rejected_callback(req.node_id, req.disconnect_reason);
    }

    // If we are the rejected player, leave the session
    if (static_cast<int8_t>(req.node_id) == m_local_node_id) {
        leave_session();
    }
}

/**
 * @brief Handle RejectReply packet
 *
 * RejectReply is sent by the server to confirm that a rejection
 * request was processed. This is typically sent back to the host
 * who initiated the rejection.
 *
 * @param header Packet header (unused)
 */
void LdnSessionHandler::handle_reject_reply(const protocol::LdnHeader& header) {
    (void)header;

    // RejectReply is just an acknowledgment, no action needed
    // The actual rejection effect is already handled by SyncNetwork
}

/**
 * @brief Handle SetAcceptPolicy response
 *
 * SetAcceptPolicy response confirms that the accept policy was changed.
 * This is sent back to the host who changed the policy.
 *
 * @param header Packet header (unused)
 * @param req Accept policy request containing new policy
 */
void LdnSessionHandler::handle_set_accept_policy(const protocol::LdnHeader& header,
                                                  const protocol::SetAcceptPolicyRequest& req) {
    (void)header;

    // Update stored accept policy
    m_accept_policy = static_cast<protocol::AcceptPolicy>(req.accept_policy);

    // Notify application
    if (m_accept_policy_changed_callback) {
        m_accept_policy_changed_callback(m_accept_policy);
    }
}

// ============================================================================
// State Queries
// ============================================================================

/**
 * @brief Check if in an active session
 *
 * @return true if currently in a Station or AccessPoint state
 */
bool LdnSessionHandler::is_in_session() const {
    return m_state == LdnSessionState::Station ||
           m_state == LdnSessionState::StationConnected ||
           m_state == LdnSessionState::AccessPoint ||
           m_state == LdnSessionState::AccessPointCreated;
}

/**
 * @brief Get current node count
 *
 * @return Number of connected players in current session
 */
uint8_t LdnSessionHandler::get_node_count() const {
    if (!is_in_session()) {
        return 0;
    }
    return m_network_info.ldn.node_count;
}

/**
 * @brief Get maximum nodes for current session
 *
 * @return Maximum players allowed, or 0 if not in session
 */
uint8_t LdnSessionHandler::get_max_nodes() const {
    if (!is_in_session()) {
        return 0;
    }
    return m_network_info.ldn.node_count_max;
}

/**
 * @brief Set our local node ID
 *
 * Called when the server assigns us a node ID or when
 * we determine our node ID from the network info.
 *
 * @param node_id Our node ID (0-7)
 */
void LdnSessionHandler::set_local_node_id(int8_t node_id) {
    m_local_node_id = node_id;

    // If we're node 0, we're the host
    if (node_id == 0 && is_in_session()) {
        m_is_host = true;
    }
}

// ============================================================================
// Actions
// ============================================================================

/**
 * @brief Leave current session
 *
 * Clears session-specific state and returns to Initialized state.
 * The application should send a Disconnect packet before calling this.
 */
void LdnSessionHandler::leave_session() {
    if (!is_in_session() && m_state != LdnSessionState::Initialized) {
        return;
    }

    // Clear session-specific state
    m_is_host = false;
    m_local_node_id = -1;
    std::memset(&m_network_info, 0, sizeof(m_network_info));

    // Return to Initialized state
    set_state(LdnSessionState::Initialized);
}

/**
 * @brief Reset handler to initial state
 *
 * Clears all state and callbacks. Use this when disconnecting
 * from the server entirely.
 */
void LdnSessionHandler::reset() {
    m_state = LdnSessionState::None;
    m_is_host = false;
    m_local_node_id = -1;
    m_last_ping_id = 0;
    m_accept_policy = protocol::AcceptPolicy::AcceptAll;

    std::memset(&m_session_id, 0, sizeof(m_session_id));
    std::memset(&m_mac_address, 0, sizeof(m_mac_address));
    std::memset(&m_network_info, 0, sizeof(m_network_info));

    // Note: We don't clear callbacks - they can persist across resets
}

// ============================================================================
// Internal Methods
// ============================================================================

/**
 * @brief Set state and invoke callback
 *
 * Handles state transition and notifies registered callback.
 *
 * @param new_state New state to transition to
 */
void LdnSessionHandler::set_state(LdnSessionState new_state) {
    if (m_state == new_state) {
        return;  // No change
    }

    LdnSessionState old_state = m_state;
    m_state = new_state;

    // Notify callback
    if (m_state_callback) {
        m_state_callback(old_state, new_state);
    }
}

} // namespace ryu_ldn::ldn
