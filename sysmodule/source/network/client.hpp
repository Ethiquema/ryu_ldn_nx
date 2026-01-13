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

#include "tcp_client.hpp"
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
 */
using ClientStateCallback = void (*)(ConnectionState old_state, ConnectionState new_state);

/**
 * @brief Callback type for received packets
 *
 * Called for each packet received from the server.
 *
 * @param packet_id Type of packet received
 * @param data Pointer to packet payload (after header)
 * @param size Size of payload in bytes
 */
using ClientPacketCallback = void (*)(protocol::PacketId packet_id,
                                       const uint8_t* data,
                                       size_t size);

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
    RyuLdnClient();

    /**
     * @brief Constructor with custom configuration
     *
     * @param config Client configuration
     */
    explicit RyuLdnClient(const RyuLdnClientConfig& config);

    /**
     * @brief Destructor - ensures clean disconnect
     */
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
    void set_config(const RyuLdnClientConfig& config);

    /**
     * @brief Get current configuration
     *
     * @return Reference to current configuration
     */
    const RyuLdnClientConfig& get_config() const { return m_config; }

    // ========================================================================
    // Callbacks
    // ========================================================================

    /**
     * @brief Set callback for state changes
     *
     * @param callback Function to call on state change (nullptr to disable)
     */
    void set_state_callback(ClientStateCallback callback);

    /**
     * @brief Set callback for received packets
     *
     * @param callback Function to call on packet receive (nullptr to disable)
     */
    void set_packet_callback(ClientPacketCallback callback);

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
    ClientOpResult connect(const char* host, uint16_t port);

    /**
     * @brief Gracefully disconnect from server
     *
     * Sends disconnect message and closes connection.
     */
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
    void update(uint64_t current_time_ms);

    // ========================================================================
    // State Queries
    // ========================================================================

    /**
     * @brief Get current connection state
     *
     * @return Current state
     */
    ConnectionState get_state() const;

    /**
     * @brief Check if connected (TCP established)
     *
     * @return true if TCP is connected (may not be ready for packets)
     */
    bool is_connected() const;

    /**
     * @brief Check if fully ready (handshake complete)
     *
     * @return true if ready to send/receive packets
     */
    bool is_ready() const;

    /**
     * @brief Check if in transitional state
     *
     * @return true if connecting, disconnecting, or in backoff
     */
    bool is_transitioning() const;

    /**
     * @brief Get retry count since last successful connection
     *
     * @return Number of connection attempts
     */
    uint32_t get_retry_count() const;

    /**
     * @brief Get last error code from server
     *
     * Returns the error code from the most recent NetworkError message.
     * Useful for diagnosing connection failures.
     *
     * @return Last error code (NetworkErrorCode::None if no error)
     */
    protocol::NetworkErrorCode get_last_error_code() const;

    /**
     * @brief Get last measured round-trip time
     *
     * Returns the RTT from the most recent ping/pong exchange.
     * Useful for monitoring connection quality.
     *
     * @return RTT in milliseconds (0 if no ping completed yet)
     */
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
    ClientOpResult send_scan(const protocol::ScanFilterFull& filter);

    /**
     * @brief Send request to create an access point
     *
     * @param request Access point parameters
     * @return ClientOpResult indicating success or failure
     */
    ClientOpResult send_create_access_point(const protocol::CreateAccessPointRequest& request);

    /**
     * @brief Send request to connect to a network
     *
     * @param request Connection parameters
     * @return ClientOpResult indicating success or failure
     */
    ClientOpResult send_connect(const protocol::ConnectRequest& request);

    /**
     * @brief Send proxy data to another client
     *
     * @param header Proxy header with destination info
     * @param data Data to send
     * @param size Size of data
     * @return ClientOpResult indicating success or failure
     */
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
    ClientOpResult send_ping();

private:
    // ========================================================================
    // Internal State
    // ========================================================================

    RyuLdnClientConfig m_config;            ///< Client configuration
    TcpClient m_tcp_client;                 ///< Low-level TCP client
    ConnectionStateMachine m_state_machine; ///< Connection state tracking
    ReconnectManager m_reconnect_manager;   ///< Reconnection backoff logic

    ClientStateCallback m_state_callback;   ///< User callback for state changes
    ClientPacketCallback m_packet_callback; ///< User callback for packets

    uint64_t m_last_ping_time_ms;           ///< Time of last ping sent
    uint64_t m_backoff_start_time_ms;       ///< Start of current backoff period
    uint32_t m_current_backoff_delay_ms;    ///< Current backoff delay

    protocol::SessionId m_session_id;       ///< Our session ID (from server)
    protocol::MacAddress m_mac_address;     ///< Our MAC address

    bool m_handshake_sent;                  ///< Whether Initialize has been sent
    bool m_initialized;                     ///< Whether socket system is initialized

    uint64_t m_handshake_start_time_ms;     ///< Time when handshake was initiated
    uint32_t m_handshake_timeout_ms;        ///< Handshake timeout (default: 5000ms)
    protocol::NetworkErrorCode m_last_error_code; ///< Last error from server

    uint64_t m_last_pong_time_ms;           ///< Time when last pong was received
    uint32_t m_ping_timeout_ms;             ///< Ping response timeout (default: 10000ms)
    uint32_t m_pending_ping_count;          ///< Number of pings without response
    uint64_t m_last_rtt_ms;                 ///< Last measured round-trip time

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
    void try_connect();

    /**
     * @brief Process received packets
     */
    void process_packets();

    /**
     * @brief Handle a single received packet
     */
    void handle_packet(protocol::PacketId id, const uint8_t* data, size_t size);

    /**
     * @brief Send Initialize handshake message
     */
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
    bool process_handshake_response(protocol::PacketId id,
                                     const uint8_t* data,
                                     size_t size);

    /**
     * @brief Check if handshake has timed out
     *
     * @param current_time_ms Current time in milliseconds
     * @return true if handshake timeout has elapsed
     */
    bool is_handshake_timeout(uint64_t current_time_ms) const;

    /**
     * @brief Generate a unique MAC address
     */
    void generate_mac_address();

    /**
     * @brief Start backoff timer
     */
    void start_backoff();

    /**
     * @brief Check if backoff has expired
     */
    bool is_backoff_expired(uint64_t current_time_ms) const;
};

/**
 * @brief Convert ClientOpResult to string for logging
 *
 * @param result Result to convert
 * @return Human-readable string representation
 */
const char* client_op_result_to_string(ClientOpResult result);

} // namespace network
} // namespace ryu_ldn
