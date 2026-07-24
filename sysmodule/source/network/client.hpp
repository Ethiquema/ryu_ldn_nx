/**
 * @file client.hpp
 * @brief RyuLdn Network Client - High-level client for ryu_ldn server communication
 *
 * This module provides the main network client that assembles all lower-level
 * components (Socket, TcpClient, ConnectionStateMachine, ReconnectManager) into
 * a complete, production-ready client for communicating with ryu_ldn servers.
 *
 * ## Architecture
 *
 * ```
 * +------------------+
 * |  RyuLdnClient    |  <-- High-level API
 * +------------------+
 *         |
 *         v
 * +------------------+     +---------------------+
 * |    TcpClient     | <-> | ConnectionStateMachine |
 * | (protocol layer) |     | (state management)     |
 * +------------------+     +---------------------+
 *         |                         |
 *         v                         v
 * +------------------+     +---------------------+
 * |     Socket       |     |  ReconnectManager   |
 * | (transport)      |     |  (backoff logic)    |
 * +------------------+     +---------------------+
 * ```
 *
 * ## Features
 *
 * - Automatic connection management with exponential backoff
 * - State machine for tracking connection lifecycle
 * - Protocol handshake handling
 * - Keepalive/ping support
 * - Packet send/receive with callbacks
 * - Thread-safe design (when used with external synchronization)
 *
 * ## Usage Example
 *
 * ```cpp
 * RyuLdnClient client;
 *
 * // Set up callbacks
 * client.set_state_callback([](ConnectionState state) {
 *     printf("State: %s\n", ConnectionStateMachine::state_to_string(state));
 * });
 *
 * client.set_packet_callback([](PacketId id, const uint8_t* data, size_t size) {
 *     // Handle received packet
 * });
 *
 * // Connect to server
 * client.connect("192.168.1.100", 30456);
 *
 * // Main loop
 * while (running) {
 *     client.update();  // Process events, handle reconnection
 *
 *     if (client.is_ready()) {
 *         // Send packets
 *         client.send_scan_request(filter);
 *     }
 * }
 *
 * client.disconnect();
 * ```
 *
 * ## Thread Safety
 *
 * This class is designed to be used from a single thread. For multi-threaded
 * use, external synchronization is required. The update() method should be
 * called from the same thread that calls other methods.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>

#include "itcp_client.hpp"
#include "connection_state.hpp"
#include "reconnect.hpp"
#include "../config/config.hpp"
#include "../protocol/types.hpp"

namespace ryu_ldn {
namespace network {

/**
 * @brief Result codes for RyuLdnClient operations
 */
enum class ClientOpResult : uint8_t {
    Success,            ///< Operation completed successfully
    NotConnected,       ///< Not connected to server
    NotReady,           ///< Connected but handshake not complete
    AlreadyConnected,   ///< Already connected
    ConnectionFailed,   ///< Connection attempt failed
    SendFailed,         ///< Failed to send packet
    InvalidState,       ///< Invalid state for this operation
    Timeout,            ///< Operation timed out
    ProtocolError,      ///< Protocol error occurred
    InternalError       ///< Internal error
};

/**
 * @brief Callback type for state changes
 *
 * Called whenever the connection state changes.
 *
 * @param old_state Previous state
 * @param new_state New state
 * @param user_data User-provided context pointer
 */
using ClientStateCallback = void (*)(ConnectionState old_state, ConnectionState new_state, void* user_data);

/**
 * @brief Callback type for received packets
 *
 * Called for each packet received from the server.
 *
 * @param packet_id Type of packet received
 * @param data Pointer to packet payload (after header)
 * @param size Size of payload in bytes
 * @param user_data User-provided context pointer
 */
using ClientPacketCallback = void (*)(protocol::PacketId packet_id,
                                       const uint8_t* data,
                                       size_t size,
                                       void* user_data);

/**
 * @brief Configuration for RyuLdnClient
 */
struct RyuLdnClientConfig {
    /**
     * @brief Server hostname or IP address
     */
    char host[config::MAX_HOST_LENGTH + 1];

    /**
     * @brief Server port number
     */
    uint16_t port;

    /**
     * @brief Connection timeout in milliseconds
     */
    uint32_t connect_timeout_ms;

    /**
     * @brief Receive timeout in milliseconds
     */
    uint32_t recv_timeout_ms;

    /**
     * @brief Ping interval in milliseconds (0 to disable)
     */
    uint32_t ping_interval_ms;

    /**
     * @brief Reconnection configuration
     */
    ReconnectConfig reconnect;

    /**
     * @brief Whether to automatically reconnect on disconnect
     */
    bool auto_reconnect;

    /**
     * @brief Room passphrase for filtering (empty = public rooms)
     *
     * Format: "Ryujinx-[0-9a-f]{8}" or empty string
     */
    char passphrase[config::MAX_PASSPHRASE_LENGTH + 1];

    /**
     * @brief Default constructor with sensible defaults
     */
    RyuLdnClientConfig();

    /**
     * @brief Constructor from Config structure
     *
     * @param cfg Application configuration
     */
    explicit RyuLdnClientConfig(const config::Config& cfg);
};

/**
 * @brief High-level RyuLdn network client
 *
 * This is the main client class that applications should use to communicate
 * with ryu_ldn servers. It handles all the complexity of connection management,
 * state tracking, and protocol handling.
 *
 * ## Lifecycle
 *
 * 1. Create client with configuration
 * 2. Set up callbacks for state changes and packets
 * 3. Call connect() to initiate connection
 * 4. Call update() regularly to process events
 * 5. Send packets when is_ready() returns true
 * 6. Call disconnect() when done
 *
 * ## State Transitions
 *
 * The client maintains an internal state machine:
 *
 * - **Disconnected**: Initial state, call connect() to start
 * - **Connecting**: TCP connection in progress
 * - **Connected**: TCP connected, handshake starting
 * - **Handshaking**: Protocol handshake in progress
 * - **Ready**: Fully connected and operational
 * - **Backoff**: Waiting before retry after failure
 * - **Retrying**: Retry attempt in progress
 * - **Disconnecting**: Graceful disconnect in progress
 * - **Error**: Fatal error, call disconnect() and retry
 */
class RyuLdnClient {
public:
    /**
     * @brief Constructor with default configuration
     *
     * Creates a client with default settings. Call set_config() before
     * connecting to customize behavior.
     */
    /// @gdb{tag="NETWORK:LIFECYCLE", msg="Client constructor"}
    RyuLdnClient();

    /**
     * @brief Constructor with custom configuration
     *
     * @param config Client configuration
     */
    explicit RyuLdnClient(const RyuLdnClientConfig& config);

    /**
     * @brief Constructor with injected ITcpClient (for testing)
     *
     * @param config Client configuration
     * @param tcp_client Injected TCP client implementation
     */
    RyuLdnClient(const RyuLdnClientConfig& config, std::unique_ptr<ITcpClient> tcp_client);

    /**
     * @brief Destructor - ensures clean disconnect
     */
    /// @gdb{tag="NETWORK:LIFECYCLE", msg="Client destructor"}
    ~RyuLdnClient();

    // Non-copyable
    RyuLdnClient(const RyuLdnClient&) = delete;
    RyuLdnClient& operator=(const RyuLdnClient&) = delete;

    // Movable
    RyuLdnClient(RyuLdnClient&& other) noexcept;
    RyuLdnClient& operator=(RyuLdnClient&& other) noexcept;

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Update client configuration
     *
     * Can be called while disconnected to change settings.
     *
     * @param config New configuration
     */
    /// @gdb{tag="NETWORK:LIFECYCLE", msg="Configuration set"}
    void set_config(const RyuLdnClientConfig& config);

    /**
     * @brief Get current configuration
     *
     * @return Reference to current configuration
     */
    /// @gdb{tag="NETWORK:CONNECTION", msg="get_config"}
    const RyuLdnClientConfig& get_config() const { return m_config; }

    // ========================================================================
    // Callbacks
    // ========================================================================

    /**
     * @brief Set callback for state changes
     *
     * @param callback Function to call on state change (nullptr to disable)
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="set_state_callback: cb=%p user_data=%p", args="$x1, $x2"}
    void set_state_callback(ClientStateCallback callback, void* user_data = nullptr);

    /**
     * @brief Set callback for received packets
     *
     * @param callback Function to call on packet receive (nullptr to disable)
     * @param user_data User-provided context pointer passed to callback
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="set_packet_callback: cb=%p user_data=%p", args="$x1, $x2"}
    void set_packet_callback(ClientPacketCallback callback, void* user_data = nullptr);

    // ========================================================================
    // Connection Management
    // ========================================================================

    /**
     * @brief Initiate connection to server
     *
     * Starts the connection process. The connection is asynchronous -
     * call update() regularly and check is_ready() or use the state
     * callback to know when connected.
     *
     * @return ClientOpResult::Success if connection started
     * @return ClientOpResult::AlreadyConnected if already connected
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="connect: entering"}
    ClientOpResult connect();

    /**
     * @brief Initiate connection with specific host/port
     *
     * Overrides configuration for this connection attempt.
     *
     * @param host Server hostname or IP
     * @param port Server port
     * @return ClientOpResult indicating success or failure
     */
    /// @gdb{tag="NETWORK:CONNECTION", msg="Connect initiated"}
    ClientOpResult connect(const char* host, uint16_t port);

    /**
     * @brief Gracefully disconnect from server
     *
     * Sends disconnect message and closes connection.
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="disconnect: entering"}
    void disconnect();

    /**
     * @brief Update client - must be called regularly
     *
     * This method:
     * - Processes incoming packets
     * - Handles reconnection logic
     * - Sends keepalive pings
     * - Updates state machine
     *
     * Call this from your main loop, ideally every frame or tick.
     *
     * @param current_time_ms Current time in milliseconds (for timing)
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="update: state=%d tick", args="$x1"}
    void update(uint64_t current_time_ms);

    // ========================================================================
    // State Queries
    // ========================================================================

    /**
     * @brief Get current connection state
     *
     * @return Current state
     */
    /// @gdb{tag="NETWORK:CONNECTION", msg="get_state"}
    ConnectionState get_state() const;

    /**
     * @brief Check if connected (TCP established)
     *
     * @return true if TCP is connected (may not be ready for packets)
     */
    /// @gdb{tag="NETWORK:CONNECTION", msg="is_connected queried"}
    bool is_connected() const;

    /**
     * @brief Check if fully ready (handshake complete)
     *
     * @return true if ready to send/receive packets
     */
    /// @gdb{tag="NETWORK:CONNECTION", msg="is_ready queried"}
    bool is_ready() const;

    /**
     * @brief Check if in transitional state
     *
     * @return true if connecting, disconnecting, or in backoff
     */
    /// @gdb{tag="NETWORK:CONNECTION", msg="is_transitioning queried"}
    bool is_transitioning() const;

    /**
     * @brief Get retry count since last successful connection
     *
     * @return Number of connection attempts
     */
    /// @gdb{tag="NETWORK:CONNECTION", msg="get_retry_count"}
    uint32_t get_retry_count() const;

    /**
     * @brief Get last error code from server
     *
     * Returns the error code from the most recent NetworkError message.
     * Useful for diagnosing connection failures.
     *
     * @return Last error code (NetworkErrorCode::None if no error)
     */
    /// @gdb{tag="NETWORK:CONNECTION", msg="get_last_error_code"}
    protocol::NetworkErrorCode get_last_error_code() const;

    /**
     * @brief Get last measured round-trip time
     *
     * Returns the RTT from the most recent ping/pong exchange.
     * Useful for monitoring connection quality.
     *
     * @return RTT in milliseconds (0 if no ping completed yet)
     */
    /// @gdb{tag="NETWORK:CONNECTION", msg="get_last_rtt_ms"}
    uint64_t get_last_rtt_ms() const;

    // ========================================================================
    // Packet Sending
    // ========================================================================

    /**
     * @brief Send a scan request to find networks
     *
     * @param filter Scan filter parameters
     * @return ClientOpResult indicating success or failure
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="send_scan"}
    ClientOpResult send_scan(const protocol::ScanFilterFull& filter);

    /**
     * @brief Send request to create an access point
     *
     * @param request Access point parameters
     * @return ClientOpResult indicating success or failure
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="send_create_access_point"}
    ClientOpResult send_create_access_point(const protocol::CreateAccessPointRequest& request,
                                            const uint8_t* advertise_data = nullptr,
                                            size_t advertise_size = 0);

    /**
     * @brief Send request to connect to a network
     *
     * @param request Connection parameters
     * @return ClientOpResult indicating success or failure
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="send_connect"}
    ClientOpResult send_connect(const protocol::ConnectRequest& request);

    /**
     * @brief Send request to create a private access point
     *
     * @param request Access point parameters with security settings
     * @return ClientOpResult indicating success or failure
     */
    ClientOpResult send_create_access_point_private(const protocol::CreateAccessPointPrivateRequest& request);

    /**
     * @brief Send request to connect to a private network
     *
     * @param request Connection parameters with security settings
     * @return ClientOpResult indicating success or failure
     */
    ClientOpResult send_connect_private(const protocol::ConnectPrivateRequest& request);

    /**
     * @brief Send proxy data to another client
     *
     * @param header Proxy header with destination info
     * @param data Data to send
     * @param size Size of data
     * @return ClientOpResult indicating success or failure
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="send_proxy_data"}
    ClientOpResult send_proxy_data(const protocol::ProxyDataHeader& header,
                                    const uint8_t* data,
                                    size_t size);

    /**
     * @brief Send a ping to keep connection alive
     *
     * Normally called automatically by update(), but can be
     * called manually if needed.
     *
     * @return ClientOpResult indicating success or failure
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="send_ping"}
    ClientOpResult send_ping();

    /**
     * @brief Send a ping response to echo back a server ping
     *
     * @param ping_id The ping ID from the server's ping request
     * @return ClientOpResult indicating success or failure
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="send_ping_response: ping_id=%u", args="$x1"}
    ClientOpResult send_ping_response(uint8_t ping_id);

    /**
     * @brief Send disconnect notification to server
     *
     * Notifies the server that we're leaving the network.
     *
     * @return ClientOpResult indicating success or failure
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="send_disconnect_network"}
    ClientOpResult send_disconnect_network();

    /**
     * @brief Send SetAcceptPolicy request (host only)
     *
     * Changes the accept policy for new connections.
     *
     * @param policy New accept policy
     * @return ClientOpResult indicating success or failure
     */
    ClientOpResult send_set_accept_policy(protocol::AcceptPolicy policy);

    /**
     * @brief Send SetAdvertiseData request (host only)
     *
     * Updates the advertise data for the network.
     *
     * @param data Advertise data buffer
     * @param size Size of data (max 384 bytes)
     * @return ClientOpResult indicating success or failure
     */
    ClientOpResult send_set_advertise_data(const uint8_t* data, size_t size);

    /**
     * @brief Send Reject request (host only)
     *
     * Rejects/kicks a player from the network.
     *
     * @param node_id Node ID of player to reject
     * @param reason Disconnect reason
     * @return ClientOpResult indicating success or failure
     */
    ClientOpResult send_reject(uint32_t node_id, protocol::DisconnectReason reason);

    /**
     * @brief Send raw pre-encoded packet
     *
     * Sends a packet that has already been encoded. Used by P2P components
     * to forward notifications to the master server.
     *
     * @param data Encoded packet data
     * @param size Packet size
     * @return ClientOpResult indicating success or failure
     */
    ClientOpResult send_raw_packet(const void* data, size_t size);

private:
    // ========================================================================
    // Internal State
    // ========================================================================

    // --- Configuration & dependencies ---------------------------------------
    RyuLdnClientConfig m_config;            ///< Client configuration
    std::unique_ptr<ITcpClient> m_tcp_client; ///< Low-level TCP client (injected)
    ConnectionStateMachine m_state_machine; ///< Connection state tracking
    ReconnectManager m_reconnect_manager;   ///< Reconnection backoff logic

    // --- Callbacks (set by user, invoked from update() / try_connect) ------
    ClientStateCallback m_state_callback;   ///< User callback for state changes
    void* m_state_callback_user_data;       ///< User data for state callback
    ClientPacketCallback m_packet_callback; ///< User callback for packets
    void* m_packet_callback_user_data;      ///< User data for packet callback

    // --- Timing (all in ms, anchored on the value passed to update()) ------
    uint64_t m_last_ping_time_ms;           ///< Time of last ping sent
    uint64_t m_backoff_start_time_ms;       ///< Start of current backoff period
    uint32_t m_current_backoff_delay_ms;    ///< Current backoff delay
    uint64_t m_last_update_time_ms;        ///< Time from last update() call

    // --- Identity (assigned by server during handshake) --------------------
    protocol::SessionId m_session_id;       ///< Our session ID (from server)
    protocol::MacAddress m_mac_address;     ///< Our MAC address

    // --- Handshake state ---------------------------------------------------
    bool m_handshake_sent;                  ///< Whether Initialize has been sent
    bool m_initialized;                     ///< Whether socket system is initialized

    // --- Handshake timing & timeout ----------------------------------------
    uint64_t m_handshake_start_time_ms;     ///< Time when handshake was initiated
    uint32_t m_handshake_timeout_ms;        ///< Handshake timeout (default: 5000ms)
    protocol::NetworkErrorCode m_last_error_code; ///< Last error from server

    // --- Ping / keepalive tracking -----------------------------------------
    uint64_t m_last_pong_time_ms;           ///< Time when last pong was received
    uint32_t m_ping_timeout_ms;             ///< Ping response timeout (default: 10000ms)
    uint32_t m_pending_ping_count;          ///< Number of pings without response
    uint64_t m_last_rtt_ms;                 ///< Last measured round-trip time
    uint8_t m_ping_id;                      ///< Incrementing ping ID for tracking

    // ========================================================================
    // Internal Methods
    // ========================================================================

    /**
     * @brief Handle state machine callback
     */
    static void state_machine_callback(ConnectionState old_state,
                                        ConnectionState new_state,
                                        ConnectionEvent event);

    /**
     * @brief Attempt TCP connection
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="try_connect: attempting TCP"}
    void try_connect();

    /**
     * @brief Handle Handshaking state in update loop
     *
     * Checks for handshake timeout, then attempts to receive and
     * process the handshake response from the server.
     *
     * @param current_time_ms Current time in milliseconds
     */
    void handle_handshaking_state(uint64_t current_time_ms);

    /**
     * @brief Handle Ready state in update loop
     *
     * Processes incoming packets and sends keepalive pings.
     * Detects connection loss via ping timeout.
     *
     * @param current_time_ms Current time in milliseconds
     */
    void handle_ready_state(uint64_t current_time_ms);

    /**
     * @brief Process received packets
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="process_packets: draining receive buffer"}
    void process_packets();

    /**
     * @brief Handle a single received packet
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="handle_packet: id=%u size=%zu", args="$x1, $x2"}
    void handle_packet(protocol::PacketId id, const uint8_t* data, size_t size);

    /**
     * @brief Send Initialize handshake message
     */
    /// @gdb{tag="NETWORK:PACKET", msg="send_initialize (handshake)"}
    ClientOpResult send_initialize();

    /**
     * @brief Process handshake response
     *
     * Called when in Handshaking state to process server response.
     *
     * @param id Packet ID received
     * @param data Packet data
     * @param size Packet size
     * @return true if handshake completed (success or failure)
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="process_handshake_response: id=%u", args="$x1"}
    bool process_handshake_response(protocol::PacketId id,
                                     const uint8_t* data,
                                     size_t size);

    /**
     * @brief Check if handshake has timed out
     *
     * @param current_time_ms Current time in milliseconds
     * @return true if handshake timeout has elapsed
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="is_handshake_timeout"}
    bool is_handshake_timeout(uint64_t current_time_ms) const;

    /**
     * @brief Generate a unique MAC address
     */
    /// @gdb{tag="NETWORK:PACKET", msg="Generating MAC address"}
    void generate_mac_address();

    /**
     * @brief Start backoff timer
     *
     * Captures m_last_update_time_ms as the backoff start time.
     * If m_last_update_time_ms is 0 (called before first update()),
     * the start time will be deferred to the first is_backoff_expired()
     * call which has a valid current_time_ms.
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="start_backoff: delay=%u retry=%u", args="$x0, $x1"}
    void start_backoff();

    /**
     * @brief Check if backoff has expired
     *
     * If start_backoff() was called before the first update() (so
     * m_backoff_start_time_ms is 0), this captures current_time_ms as
     * the start time and returns false (start timer, check next tick).
     * Otherwise, checks if the backoff delay has elapsed.
     *
     * @param current_time_ms Current time in milliseconds
     * @return true if backoff period has elapsed
     */
    /// @gdb{tag="NETWORK:STATE_CALLBACKS", msg="is_backoff_expired"}
    bool is_backoff_expired(uint64_t current_time_ms);

    // ========================================================================
    // Private helpers (extracted from duplicated send/recv patterns)
    // ========================================================================

    /**
     * @brief Common handling for a ConnectionLost result from the TCP client.
     *
     * Extracted from the ~20 sites that previously inlined:
     *   `ConnectionLost → process_event → start_backoff → state callback`.
     * Runs the state-machine ConnectionLost event, optionally starts the
     * reconnect backoff (only when `auto_reconnect` is set and the caller
     * confirms the loss is recoverable — e.g. NOT during a graceful
     * disconnect), and fires the user state callback if the state changed.
     *
     * @param start_reconnect If true and m_config.auto_reconnect is set,
     *                        calls start_backoff() to schedule a retry.
     */
    void handle_connection_lost(bool start_reconnect);

    /**
     * @brief Translate a TCP send result into a ClientOpResult.
     *
     * Extracted from the ~12 send_* sites that previously inlined:
     *   `is_ready() → tcp_client->send_* → ConnectionLost handling → return`.
     * The caller performs the `is_ready()` guard and the actual send, then
     * passes the resulting ClientResult here. On ConnectionLost this routes
     * through handle_connection_lost(false) (no backoff — the send methods
     * are not allowed to trigger reconnect themselves; the update() loop
     * owns the reconnect lifecycle). On any other non-success result the
     * helper returns ClientOpResult::SendFailed without touching the state
     * machine.
     *
     * @param send_result Result from the underlying ITcpClient send call.
     * @return ClientOpResult::Success on success, SendFailed on any error.
     */
    ClientOpResult handle_send_result(ClientResult send_result);
};

/**
 * @brief Convert ClientOpResult to string for logging
 *
 * @param result Result to convert
 * @return Human-readable string representation
 */
/// @gdb{tag="NETWORK:PACKET", msg="client_op_result_to_string"}
const char* client_op_result_to_string(ClientOpResult result);

} // namespace network
} // namespace ryu_ldn
