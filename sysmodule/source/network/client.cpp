/**
 * @file client.cpp
 * @brief RyuLdn Network Client Implementation
 *
 * This module implements the high-level RyuLdn client that coordinates
 * all the lower-level networking components to provide a complete,
 * production-ready client for ryu_ldn server communication.
 *
 * ## Implementation Overview
 *
 * The client uses a state machine pattern to manage the connection lifecycle.
 * The main update() loop drives all state transitions based on:
 *
 * 1. **Current state**: Determines what actions are valid
 * 2. **Events**: Connection success/failure, packet receipt, timeouts
 * 3. **Configuration**: Auto-reconnect, ping intervals, etc.
 *
 * ## State Machine Integration
 *
 * The client wraps ConnectionStateMachine and responds to state changes:
 *
 * - **Disconnected**: Ready to connect, no action needed
 * - **Connecting**: Wait for TCP connect result
 * - **Connected**: Send Initialize handshake
 * - **Handshaking**: Wait for server response (implicit in Connected for now)
 * - **Ready**: Process packets, send keepalives
 * - **Backoff**: Wait for timer, then retry
 * - **Error**: Requires manual disconnect/reconnect
 *
 * ## Packet Processing
 *
 * Incoming packets are handled in process_packets():
 *
 * 1. Poll TCP client for available packets
 * 2. For each packet, call handle_packet() to process
 * 3. User callback is invoked for application-specific handling
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "client.hpp"
#include "tcp_client.hpp"
#include "../debug/log.hpp"
#include <cstring>
#include <memory>

namespace ryu_ldn {
namespace network {


// ============================================================================
// RyuLdnClientConfig Implementation
// ============================================================================

/**
 * @brief Default constructor with sensible defaults
 *
 * Initializes configuration with:
 * - Host: "127.0.0.1" (localhost)
 * - Port: 30456 (default ryu_ldn port)
 * - Connect timeout: 5000ms
 * - Recv timeout: 20ms (short poll for quick burst draining)
 * - Ping interval: 0ms (disabled — server-initiated pings only)
 * - Auto reconnect: enabled
 */
RyuLdnClientConfig::RyuLdnClientConfig()
    : port(30456)
    , connect_timeout_ms(5000)
    , recv_timeout_ms(20)  // 20 ms — short poll so process_packets drains
                           // bursts quickly. Sends from MITM no longer take
                           // m_client_mutex, so this only bounds how long
                           // the BG thread itself stays in poll() and does
                           // not block game sends.
    , ping_interval_ms(0)  // Disabled — RyuLdn protocol is unidirectional:
                           // only the SERVER initiates pings (Requester=0)
                           // and the client echoes them back. The server's
                           // HandlePing ignores client-initiated pings
                           // (Requester=1), so sending one and waiting for
                           // a pong always times out and incorrectly tears
                           // down the connection. Mirrors Ryujinx's behavior.
    , reconnect()
    , auto_reconnect(true)
{
    std::strncpy(host, "127.0.0.1", sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';
    passphrase[0] = '\0';  // Empty passphrase = public rooms
}

/**
 * @brief Constructor from application Config
 *
 * Translates application-level configuration to client configuration.
 *
 * @param cfg Application configuration from INI file
 */
RyuLdnClientConfig::RyuLdnClientConfig(const config::Config& cfg)
    : port(cfg.server.port)
    , connect_timeout_ms(5000)  // Hardcoded: 5s connection timeout
    , recv_timeout_ms(20)  // 20 ms — see default ctor
    , ping_interval_ms(0)  // Forced 0 — server drives pings
    , reconnect()
    , auto_reconnect(true)  // Hardcoded: auto-reconnect always enabled
{
    // Copy host, ensuring null termination
    std::memset(host, 0, sizeof(host));
    std::memcpy(host, cfg.server.host, sizeof(host) - 1);

    // Copy passphrase, ensuring null termination
    std::memset(passphrase, 0, sizeof(passphrase));
    std::memcpy(passphrase, cfg.ldn.passphrase, sizeof(passphrase) - 1);

    // Configure reconnection with hardcoded constants
    reconnect.initial_delay_ms = 1000;  // 1s initial delay
    reconnect.max_delay_ms = 10000;     // 10s max (10x initial)
    reconnect.multiplier_percent = 200;  // 2x
    reconnect.jitter_percent = 10;
    reconnect.max_retries = 0;  // 0 = unlimited retries
}

// ============================================================================
// RyuLdnClient Implementation
// ============================================================================

/**
 * @brief Constructor with default configuration
 *
 * Creates a client ready to connect. Socket initialization is
 * deferred until first connection attempt.
 */
RyuLdnClient::RyuLdnClient()
    : m_config()
    , m_tcp_client(std::make_unique<TcpClient>())
    , m_state_machine()
    , m_reconnect_manager()
    , m_state_callback(nullptr)
    , m_packet_callback(nullptr)
    , m_packet_callback_user_data(nullptr)
    , m_last_ping_time_ms(0)
    , m_backoff_start_time_ms(0)
    , m_current_backoff_delay_ms(0)
    , m_last_update_time_ms(0)
    , m_session_id{}
    , m_mac_address{}
    , m_handshake_sent(false)
    , m_initialized(false)
    , m_handshake_start_time_ms(0)
    , m_handshake_timeout_ms(5000)
    , m_last_error_code(protocol::NetworkErrorCode::None)
    , m_last_pong_time_ms(0)
    , m_ping_timeout_ms(10000)
    , m_pending_ping_count(0)
    , m_last_rtt_ms(0)
    , m_ping_id(0)
{
    generate_mac_address();
}

/**
 * @brief Constructor with custom configuration
 *
 * @param config Client configuration including server address
 */
RyuLdnClient::RyuLdnClient(const RyuLdnClientConfig& config)
    : m_config(config)
    , m_tcp_client(std::make_unique<TcpClient>())
    , m_state_machine()
    , m_reconnect_manager(config.reconnect)
    , m_state_callback(nullptr)
    , m_state_callback_user_data(nullptr)
    , m_packet_callback(nullptr)
    , m_packet_callback_user_data(nullptr)
    , m_last_ping_time_ms(0)
    , m_backoff_start_time_ms(0)
    , m_current_backoff_delay_ms(0)
    , m_last_update_time_ms(0)
    , m_session_id{}
    , m_mac_address{}
    , m_handshake_sent(false)
    , m_initialized(false)
    , m_handshake_start_time_ms(0)
    , m_handshake_timeout_ms(5000)
    , m_last_error_code(protocol::NetworkErrorCode::None)
    , m_last_pong_time_ms(0)
    , m_ping_timeout_ms(10000)
    , m_pending_ping_count(0)
    , m_last_rtt_ms(0)
    , m_ping_id(0)
{
    generate_mac_address();
}

RyuLdnClient::RyuLdnClient(const RyuLdnClientConfig& config, std::unique_ptr<ITcpClient> tcp_client)
    : m_config(config)
    , m_tcp_client(std::move(tcp_client))
    , m_state_machine()
    , m_reconnect_manager(config.reconnect)
    , m_state_callback(nullptr)
    , m_state_callback_user_data(nullptr)
    , m_packet_callback(nullptr)
    , m_packet_callback_user_data(nullptr)
    , m_last_ping_time_ms(0)
    , m_backoff_start_time_ms(0)
    , m_current_backoff_delay_ms(0)
    , m_last_update_time_ms(0)
    , m_session_id{}
    , m_mac_address{}
    , m_handshake_sent(false)
    , m_initialized(false)
    , m_handshake_start_time_ms(0)
    , m_handshake_timeout_ms(5000)
    , m_last_error_code(protocol::NetworkErrorCode::None)
    , m_last_pong_time_ms(0)
    , m_ping_timeout_ms(10000)
    , m_pending_ping_count(0)
    , m_last_rtt_ms(0)
    , m_ping_id(0)
{
    generate_mac_address();
}

/**
 * @brief Destructor - ensures clean disconnect
 *
 * Disconnects from server if connected and cleans up resources.
 */
RyuLdnClient::~RyuLdnClient() {
    if (m_tcp_client) {
        disconnect();
    }
}

/**
 * @brief Move constructor
 *
 * Transfers ownership of all resources from other client.
 *
 * @param other Source client to move from
 */
RyuLdnClient::RyuLdnClient(RyuLdnClient&& other) noexcept
    : m_config(other.m_config)
    , m_tcp_client(std::move(other.m_tcp_client))
    , m_state_machine()  // Can't move, but state is reset anyway
    , m_reconnect_manager(other.m_reconnect_manager.get_config())
    , m_state_callback(other.m_state_callback)
    , m_state_callback_user_data(other.m_state_callback_user_data)
    , m_packet_callback(other.m_packet_callback)
    , m_packet_callback_user_data(other.m_packet_callback_user_data)
    , m_last_ping_time_ms(other.m_last_ping_time_ms)
    , m_backoff_start_time_ms(other.m_backoff_start_time_ms)
    , m_current_backoff_delay_ms(other.m_current_backoff_delay_ms)
    , m_last_update_time_ms(other.m_last_update_time_ms)
    , m_session_id(other.m_session_id)
    , m_mac_address(other.m_mac_address)
    , m_handshake_sent(other.m_handshake_sent)
    , m_initialized(other.m_initialized)
    , m_handshake_start_time_ms(other.m_handshake_start_time_ms)
    , m_handshake_timeout_ms(other.m_handshake_timeout_ms)
    , m_last_error_code(other.m_last_error_code)
    , m_last_pong_time_ms(other.m_last_pong_time_ms)
    , m_ping_timeout_ms(other.m_ping_timeout_ms)
    , m_pending_ping_count(other.m_pending_ping_count)
    , m_last_rtt_ms(other.m_last_rtt_ms)
{
    other.m_state_callback = nullptr;
    other.m_packet_callback = nullptr;
    other.m_packet_callback_user_data = nullptr;
    other.m_initialized = false;
}

/**
 * @brief Move assignment operator
 *
 * @param other Source client to move from
 * @return Reference to this
 */
RyuLdnClient& RyuLdnClient::operator=(RyuLdnClient&& other) noexcept {
    if (this != &other) {
        disconnect();

        m_config = other.m_config;
        m_tcp_client = std::move(other.m_tcp_client);
        // m_state_machine - reset to disconnected
        m_reconnect_manager.set_config(other.m_reconnect_manager.get_config());
        m_state_callback = other.m_state_callback;
        m_packet_callback = other.m_packet_callback;
        m_state_callback_user_data = other.m_state_callback_user_data;
        m_packet_callback_user_data = other.m_packet_callback_user_data;
        m_last_ping_time_ms = other.m_last_ping_time_ms;
        m_backoff_start_time_ms = other.m_backoff_start_time_ms;
        m_current_backoff_delay_ms = other.m_current_backoff_delay_ms;
        m_last_update_time_ms = other.m_last_update_time_ms;
        m_session_id = other.m_session_id;
        m_mac_address = other.m_mac_address;
        m_handshake_sent = other.m_handshake_sent;
        m_initialized = other.m_initialized;
        m_handshake_start_time_ms = other.m_handshake_start_time_ms;
        m_handshake_timeout_ms = other.m_handshake_timeout_ms;
        m_last_error_code = other.m_last_error_code;
        m_last_pong_time_ms = other.m_last_pong_time_ms;
        m_ping_timeout_ms = other.m_ping_timeout_ms;
        m_pending_ping_count = other.m_pending_ping_count;
        m_last_rtt_ms = other.m_last_rtt_ms;

        other.m_state_callback = nullptr;
        other.m_packet_callback = nullptr;
        other.m_state_callback_user_data = nullptr;
        other.m_packet_callback_user_data = nullptr;
        other.m_initialized = false;
    }
    return *this;
}

// ============================================================================
// Configuration
// ============================================================================

/**
 * @brief Update client configuration
 *
 * @param config New configuration to use
 */
void RyuLdnClient::set_config(const RyuLdnClientConfig& config) {
    m_config = config;
    m_reconnect_manager.set_config(config.reconnect);
}

// ============================================================================
// Callbacks
// ============================================================================

/**
 * @brief Set callback for state changes
 *
 * @param callback Function to call when state changes
 */
void RyuLdnClient::set_state_callback(ClientStateCallback callback, void* user_data) {
    m_state_callback = callback;
    m_state_callback_user_data = user_data;
}

/**
 * @brief Set callback for received packets
 *
 * @param callback Function to call for each received packet
 * @param user_data User-provided context pointer passed to callback
 */
void RyuLdnClient::set_packet_callback(ClientPacketCallback callback, void* user_data) {
    m_packet_callback = callback;
    m_packet_callback_user_data = user_data;
}

// ============================================================================
// Connection Management
// ============================================================================

/**
 * @brief Initiate connection to server
 *
 * Uses host/port from configuration.
 *
 * @return ClientOpResult indicating success or failure
 */
ClientOpResult RyuLdnClient::connect() {
    return connect(m_config.host, m_config.port);
}

/**
 * @brief Initiate connection with specific host/port
 *
 * Updates configuration and starts connection process.
 *
 * @param host Server hostname or IP address
 * @param port Server port number
 * @return ClientOpResult indicating success or failure
 */
ClientOpResult RyuLdnClient::connect(const char* host, uint16_t port) {
    LOG_INFO("Connecting to %s:%u", host ? host : m_config.host, port);

    // Check if already connected or connecting
    if (m_state_machine.is_connected() || m_state_machine.is_transitioning()) {
        if (m_state_machine.get_state() != ConnectionState::Backoff) {
            LOG_WARN("Already connected or connecting");
            return ClientOpResult::AlreadyConnected;
        }
    }

    // Update config with new host/port
    if (host != nullptr) {
        std::strncpy(m_config.host, host, sizeof(m_config.host) - 1);
        m_config.host[sizeof(m_config.host) - 1] = '\0';
    }
    m_config.port = port;

    // Initialize socket system if needed
    if (!m_initialized) {
        if (!m_tcp_client->initialize()) {
            return ClientOpResult::InternalError;
        }
        m_initialized = true;
    }

    // Reset handshake state
    m_handshake_sent = false;

    // Trigger state machine transition
    auto result = m_state_machine.process_event(ConnectionEvent::Connect);
    if (result != TransitionResult::Success) {
        return ClientOpResult::InvalidState;
    }

    // Actually try to connect
    try_connect();

    // State callback: transitioned from Disconnected → Connecting/Connected/Backoff
    if (m_state_callback) {
        m_state_callback(ConnectionState::Disconnected, m_state_machine.get_state(), m_state_callback_user_data);
    }

    return ClientOpResult::Success;
}

/**
 * @brief Disconnect from server
 *
 * Sends disconnect message (if connected) and closes connection.
 */
void RyuLdnClient::disconnect() {
    LOG_INFO("Disconnecting from server");

    // Send disconnect message if we're ready
    if (m_tcp_client && m_state_machine.is_ready()) {
        protocol::DisconnectMessage msg{};
        m_tcp_client->send_disconnect(msg);
    }

    // Close TCP connection
    if (m_tcp_client) {
        m_tcp_client->disconnect();
    }

    ConnectionState prev_state = m_state_machine.get_state();

    // Update state machine - Disconnect moves to Disconnecting state
    m_state_machine.process_event(ConnectionEvent::Disconnect);

    // Complete the disconnect - ConnectionLost moves Disconnecting -> Disconnected
    if (m_state_machine.get_state() == ConnectionState::Disconnecting) {
        m_state_machine.process_event(ConnectionEvent::ConnectionLost);
    }

    // Reset reconnection state
    m_reconnect_manager.reset();
    m_handshake_sent = false;

    // State callback: transitioned to Disconnected
    if (m_state_callback) {
        m_state_callback(prev_state, m_state_machine.get_state(), m_state_callback_user_data);
    }

    LOG_VERBOSE("Disconnect complete");
}

/**
 * @brief Update client - must be called regularly
 *
 * Handles all client maintenance tasks.
 *
 * @param current_time_ms Current time in milliseconds
 */
void RyuLdnClient::update(uint64_t current_time_ms) {
    m_last_update_time_ms = current_time_ms;
    ConnectionState state = m_state_machine.get_state();

    switch (state) {
        case ConnectionState::Disconnected:
            // Nothing to do
            break;

        case ConnectionState::Connecting:
        case ConnectionState::Retrying:
            // Connection attempt is synchronous in TcpClient
            // If we're still in this state, something went wrong
            break;

        case ConnectionState::Connected:
            // TCP connected, send handshake if not done
            if (!m_handshake_sent) {
                if (send_initialize() == ClientOpResult::Success) {
                    m_handshake_sent = true;
                    m_handshake_start_time_ms = current_time_ms;
                    // Transition to Handshaking state to wait for response
                    m_state_machine.process_event(ConnectionEvent::HandshakeStarted);
                    if (m_state_callback) {
                        m_state_callback(ConnectionState::Connected, ConnectionState::Handshaking, m_state_callback_user_data);
                    }
                } else {
                    ConnectionState before_state = m_state_machine.get_state();
                    m_state_machine.process_event(ConnectionEvent::HandshakeFailed);
                    if (m_config.auto_reconnect) {
                        start_backoff();
                    }
                    if (m_state_callback && before_state != m_state_machine.get_state()) {
                        m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
                    }
                }
            }
            break;

        case ConnectionState::Handshaking:
            handle_handshaking_state(current_time_ms);
            break;

        case ConnectionState::Ready:
            handle_ready_state(current_time_ms);
            break;

        case ConnectionState::Backoff:
            // Check if backoff has expired
            if (is_backoff_expired(current_time_ms)) {
                m_state_machine.process_event(ConnectionEvent::BackoffExpired);
                // This transitions to Retrying, then we try to connect
                ConnectionState before_connect = m_state_machine.get_state();
                try_connect();
                if (m_state_callback && before_connect != m_state_machine.get_state()) {
                    m_state_callback(before_connect, m_state_machine.get_state(), m_state_callback_user_data);
                }
            }
            break;

        case ConnectionState::Disconnecting:
            // TCP client handles this
            {
                ConnectionState before_state = m_state_machine.get_state();
                m_state_machine.process_event(ConnectionEvent::ConnectionLost);
                if (m_state_callback) {
                    m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
                }
            }
            break;

        case ConnectionState::Error:
            // Requires manual disconnect/reconnect
            break;
    }
}



/**
 * @brief Handle Handshaking state in update loop
 *
 * Checks for handshake timeout, then attempts to receive and
 * process the handshake response from the server.
 *
 * @param current_time_ms Current time in milliseconds
 */
void RyuLdnClient::handle_handshaking_state(uint64_t current_time_ms) {
    // Check for handshake timeout
    if (is_handshake_timeout(current_time_ms)) {
        m_last_error_code = protocol::NetworkErrorCode::HandshakeTimeout;
        ConnectionState before_state = m_state_machine.get_state();
        m_state_machine.process_event(ConnectionEvent::HandshakeFailed);
        if (m_config.auto_reconnect) {
            start_backoff();
        }
        if (m_state_callback && before_state != m_state_machine.get_state()) {
            m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
        }
        return;
    }

    // Try to receive and process handshake response
    uint8_t recv_buffer[2048];
    size_t recv_size = 0;
    protocol::PacketId packet_id;

    ClientResult result = m_tcp_client->receive_packet(
        packet_id,
        recv_buffer,
        sizeof(recv_buffer),
        recv_size,
        static_cast<int32_t>(m_config.recv_timeout_ms)
    );

    if (result == ClientResult::Success) {
        // Process the handshake response
        ConnectionState before_state = m_state_machine.get_state();
        if (process_handshake_response(packet_id, recv_buffer, recv_size)) {
            // Handshake completed - notify via state callback
            ConnectionState after_state = m_state_machine.get_state();
            if (m_state_callback && before_state != after_state) {
                m_state_callback(before_state, after_state, m_state_callback_user_data);
            }
        }
    } else if (result == ClientResult::ConnectionLost) {
        ConnectionState before_state = m_state_machine.get_state();
        m_state_machine.process_event(ConnectionEvent::ConnectionLost);
        if (m_config.auto_reconnect) {
            start_backoff();
        }
        if (m_state_callback) {
            m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
        }
    }
    // Timeout is expected - just keep waiting
}

/**
 * @brief Handle Ready state in update loop
 *
 * Processes incoming packets and sends keepalive pings.
 * Detects connection loss via ping timeout.
 *
 * @param current_time_ms Current time in milliseconds
 */
void RyuLdnClient::handle_ready_state(uint64_t current_time_ms) {
    // Normal operation - process packets and send keepalives
    process_packets();

    // Check for ping timeout (no pong received)
    if (m_pending_ping_count > 0 && m_ping_timeout_ms > 0) {
        if (current_time_ms - m_last_ping_time_ms >= m_ping_timeout_ms) {
            LOG_INFO("update(Ready): ping timeout (%u pending, %lu ms since last ping)",
                     m_pending_ping_count, current_time_ms - m_last_ping_time_ms);
            ConnectionState before_state = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::ConnectionLost);
            if (m_config.auto_reconnect) {
                start_backoff();
            }
            if (m_state_callback) {
                m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
            }
            return;
        }
    }

    // Send ping if interval elapsed
    if (m_config.ping_interval_ms > 0) {
        if (current_time_ms - m_last_ping_time_ms >= m_config.ping_interval_ms) {
            ClientOpResult ping_result = send_ping();
            if (ping_result == ClientOpResult::Success) {
                LOG_INFO("update(Ready): sent ping (pending=%u)", m_pending_ping_count + 1);
                m_last_ping_time_ms = current_time_ms;
                m_pending_ping_count++;
            } else {
                LOG_INFO("update(Ready): send_ping failed: %s",
                         client_op_result_to_string(ping_result));
            }
        }
    }
}
// ============================================================================
// State Queries
// ============================================================================

/**
 * @brief Get current connection state
 *
 * @return Current state
 */
ConnectionState RyuLdnClient::get_state() const {
    return m_state_machine.get_state();
}

/**
 * @brief Check if connected (TCP established)
 *
 * @return true if TCP is connected
 */
bool RyuLdnClient::is_connected() const {
    return m_state_machine.is_connected();
}

/**
 * @brief Check if fully ready
 *
 * @return true if ready to send/receive packets
 */
bool RyuLdnClient::is_ready() const {
    return m_state_machine.is_ready();
}

/**
 * @brief Check if in transitional state
 *
 * @return true if connecting, disconnecting, or in backoff
 */
bool RyuLdnClient::is_transitioning() const {
    return m_state_machine.is_transitioning();
}

/**
 * @brief Get retry count
 *
 * @return Number of connection attempts
 */
uint32_t RyuLdnClient::get_retry_count() const {
    return m_reconnect_manager.get_retry_count();
}

/**
 * @brief Get last error code from server
 *
 * @return Last error code (NetworkErrorCode::None if no error)
 */
protocol::NetworkErrorCode RyuLdnClient::get_last_error_code() const {
    return m_last_error_code;
}

/**
 * @brief Get last measured round-trip time
 *
 * @return RTT in milliseconds (0 if no ping completed yet)
 */
uint64_t RyuLdnClient::get_last_rtt_ms() const {
    return m_last_rtt_ms;
}

// ============================================================================
// Packet Sending
// ============================================================================

/**
 * @brief Send scan request
 *
 * @param filter Scan filter parameters
 * @return ClientOpResult indicating success or failure
 */
ClientOpResult RyuLdnClient::send_scan(const protocol::ScanFilterFull& filter) {
    if (!is_ready()) {
        return ClientOpResult::NotReady;
    }

    ClientResult result = m_tcp_client->send_scan(filter);
    if (result != ClientResult::Success) {
        LOG_INFO("send_scan: TCP send returned %d", static_cast<int>(result));
        if (result == ClientResult::ConnectionLost) {
            ConnectionState before_state = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::ConnectionLost);
            if (m_state_callback && before_state != m_state_machine.get_state()) {
                m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
            }
        }
        return ClientOpResult::SendFailed;
    }

    return ClientOpResult::Success;
}

/**
 * @brief Send create access point request
 *
 * @param request Access point parameters
 * @return ClientOpResult indicating success or failure
 */
ClientOpResult RyuLdnClient::send_create_access_point(
    const protocol::CreateAccessPointRequest& request,
    const uint8_t* advertise_data,
    size_t advertise_size) {
    if (!is_ready()) {
        return ClientOpResult::NotReady;
    }

    ClientResult result = m_tcp_client->send_create_access_point(request, advertise_data, advertise_size);
    if (result != ClientResult::Success) {
        LOG_INFO("send_create_access_point: TCP send returned %d", static_cast<int>(result));
        if (result == ClientResult::ConnectionLost) {
            ConnectionState before_state = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::ConnectionLost);
            if (m_state_callback && before_state != m_state_machine.get_state()) {
                m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
            }
        }
        return ClientOpResult::SendFailed;
    }

    return ClientOpResult::Success;
}

/**
 * @brief Send connect request
 *
 * @param request Connection parameters
 * @return ClientOpResult indicating success or failure
 */
ClientOpResult RyuLdnClient::send_connect(const protocol::ConnectRequest& request) {
    if (!is_ready()) {
        return ClientOpResult::NotReady;
    }

    ClientResult result = m_tcp_client->send_connect(request);
    if (result != ClientResult::Success) {
        LOG_INFO("send_connect: TCP send returned %d", static_cast<int>(result));
        if (result == ClientResult::ConnectionLost) {
            ConnectionState before_state = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::ConnectionLost);
            if (m_state_callback && before_state != m_state_machine.get_state()) {
                m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
            }
        }
        return ClientOpResult::SendFailed;
    }

    return ClientOpResult::Success;
}

/**
 * @brief Send create access point private request
 *
 * @param request Access point parameters with security settings
 * @return ClientOpResult indicating success or failure
 */
ClientOpResult RyuLdnClient::send_create_access_point_private(
    const protocol::CreateAccessPointPrivateRequest& request) {
    if (!is_ready()) {
        return ClientOpResult::NotReady;
    }

    ClientResult result = m_tcp_client->send_create_access_point_private(request, nullptr, 0);
    if (result != ClientResult::Success) {
        LOG_INFO("send_create_access_point_private: TCP send returned %d", static_cast<int>(result));
        if (result == ClientResult::ConnectionLost) {
            ConnectionState before_state = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::ConnectionLost);
            if (m_state_callback && before_state != m_state_machine.get_state()) {
                m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
            }
        }
        return ClientOpResult::SendFailed;
    }

    return ClientOpResult::Success;
}

/**
 * @brief Send connect private request
 *
 * @param request Connection parameters with security settings
 * @return ClientOpResult indicating success or failure
 */
ClientOpResult RyuLdnClient::send_connect_private(const protocol::ConnectPrivateRequest& request) {
    if (!is_ready()) {
        return ClientOpResult::NotReady;
    }

    ClientResult result = m_tcp_client->send_connect_private(request);
    if (result != ClientResult::Success) {
        LOG_INFO("send_connect_private: TCP send returned %d", static_cast<int>(result));
        if (result == ClientResult::ConnectionLost) {
            ConnectionState before_state = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::ConnectionLost);
            if (m_state_callback && before_state != m_state_machine.get_state()) {
                m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
            }
        }
        return ClientOpResult::SendFailed;
    }

    return ClientOpResult::Success;
}

/**
 * @brief Send proxy data
 *
 * @param header Proxy header
 * @param data Data to send
 * @param size Size of data
 * @return ClientOpResult indicating success or failure
 */
ClientOpResult RyuLdnClient::send_proxy_data(const protocol::ProxyDataHeader& header,
                                              const uint8_t* data,
                                              size_t size) {
    if (!is_ready()) {
        return ClientOpResult::NotReady;
    }

    ClientResult result = m_tcp_client->send_proxy_data(header, data, size);
    if (result != ClientResult::Success) {
        LOG_INFO("send_proxy_data: TCP send returned %d", static_cast<int>(result));
        if (result == ClientResult::ConnectionLost) {
            ConnectionState before_state = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::ConnectionLost);
            if (m_state_callback && before_state != m_state_machine.get_state()) {
                m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
            }
        }
        return ClientOpResult::SendFailed;
    }

    return ClientOpResult::Success;
}

/**
 * @brief Send ping
 *
 * @return ClientOpResult indicating success or failure
 */
ClientOpResult RyuLdnClient::send_ping() {
    if (!is_ready()) {
        return ClientOpResult::NotReady;
    }

    protocol::PingMessage msg{};
    msg.requester = 1;  // Client requesting
    msg.id = m_ping_id++;
    ClientResult result = m_tcp_client->send_ping(msg);
    if (result != ClientResult::Success) {
        if (result == ClientResult::ConnectionLost) {
            ConnectionState before_state = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::ConnectionLost);
            if (m_state_callback && before_state != m_state_machine.get_state()) {
                m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
            }
        }
        return ClientOpResult::SendFailed;
    }

    return ClientOpResult::Success;
}

ClientOpResult RyuLdnClient::send_ping_response(uint8_t ping_id) {
    if (!is_ready()) {
        return ClientOpResult::NotReady;
    }

    protocol::PingMessage msg{};
    msg.requester = 0;  // Echo back server's ping
    msg.id = ping_id;
    ClientResult result = m_tcp_client->send_ping(msg);
    if (result != ClientResult::Success) {
        if (result == ClientResult::ConnectionLost) {
            ConnectionState before_state = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::ConnectionLost);
            if (m_state_callback && before_state != m_state_machine.get_state()) {
                m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
            }
        }
        return ClientOpResult::SendFailed;
    }

    return ClientOpResult::Success;
}

ClientOpResult RyuLdnClient::send_disconnect_network() {
    if (!is_ready()) {
        return ClientOpResult::NotReady;
    }

    protocol::DisconnectMessage msg{};
    msg.disconnect_ip = 0;  // Server will fill this in
    ClientResult result = m_tcp_client->send_disconnect(msg);
    if (result != ClientResult::Success) {
        if (result == ClientResult::ConnectionLost) {
            ConnectionState before_state = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::ConnectionLost);
            if (m_state_callback && before_state != m_state_machine.get_state()) {
                m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
            }
        }
        return ClientOpResult::SendFailed;
    }

    return ClientOpResult::Success;
}

ClientOpResult RyuLdnClient::send_set_accept_policy(protocol::AcceptPolicy policy) {
    if (!is_ready()) {
        return ClientOpResult::NotReady;
    }

    protocol::SetAcceptPolicyRequest request{};
    request.accept_policy = static_cast<uint8_t>(policy);
    ClientResult result = m_tcp_client->send_set_accept_policy(request);
    if (result != ClientResult::Success) {
        if (result == ClientResult::ConnectionLost) {
            ConnectionState before_state = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::ConnectionLost);
            if (m_state_callback && before_state != m_state_machine.get_state()) {
                m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
            }
        }
        return ClientOpResult::SendFailed;
    }

    return ClientOpResult::Success;
}

ClientOpResult RyuLdnClient::send_set_advertise_data(const uint8_t* data, size_t size) {
    if (!is_ready()) {
        return ClientOpResult::NotReady;
    }

    ClientResult result = m_tcp_client->send_set_advertise_data(data, size);
    if (result != ClientResult::Success) {
        if (result == ClientResult::ConnectionLost) {
            ConnectionState before_state = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::ConnectionLost);
            if (m_state_callback && before_state != m_state_machine.get_state()) {
                m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
            }
        }
        return ClientOpResult::SendFailed;
    }

    return ClientOpResult::Success;
}

ClientOpResult RyuLdnClient::send_reject(uint32_t node_id, protocol::DisconnectReason reason) {
    if (!is_ready()) {
        return ClientOpResult::NotReady;
    }

    protocol::RejectRequest request{};
    request.node_id = node_id;
    request.disconnect_reason = static_cast<uint32_t>(reason);
    ClientResult result = m_tcp_client->send_reject(request);
    if (result != ClientResult::Success) {
        if (result == ClientResult::ConnectionLost) {
            ConnectionState before_state = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::ConnectionLost);
            if (m_state_callback && before_state != m_state_machine.get_state()) {
                m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
            }
        }
        return ClientOpResult::SendFailed;
    }

    return ClientOpResult::Success;
}

ClientOpResult RyuLdnClient::send_raw_packet(const void* data, size_t size) {
    if (!is_ready()) {
        return ClientOpResult::NotReady;
    }

    ClientResult result = m_tcp_client->send_raw(data, size);
    if (result != ClientResult::Success) {
        if (result == ClientResult::ConnectionLost) {
            ConnectionState before_state = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::ConnectionLost);
            if (m_state_callback && before_state != m_state_machine.get_state()) {
                m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
            }
        }
        return ClientOpResult::SendFailed;
    }

    return ClientOpResult::Success;
}

// ============================================================================
// Internal Methods
// ============================================================================

/**
 * @brief Attempt TCP connection
 *
 * Called when state machine enters Connecting or Retrying state.
 */
void RyuLdnClient::try_connect() {
    LOG_VERBOSE("Attempting TCP connection to %s:%u", m_config.host, m_config.port);

    ClientResult result = m_tcp_client->connect(
        m_config.host,
        m_config.port,
        m_config.connect_timeout_ms
    );

    if (result == ClientResult::Success) {
        LOG_INFO("TCP connection established");
        // Connection successful
        ConnectionState before_state = m_state_machine.get_state();
        m_state_machine.process_event(ConnectionEvent::ConnectSuccess);
        m_reconnect_manager.reset();
        if (m_state_callback) {
            m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
        }
    } else {
        LOG_WARN("TCP connection failed: %s", client_result_to_string(result));
        // Connection failed
        ConnectionState before_state = m_state_machine.get_state();
        m_state_machine.process_event(ConnectionEvent::ConnectFailed);
        m_reconnect_manager.record_failure();

        // Start backoff if auto-reconnect is enabled
        if (m_config.auto_reconnect) {
            LOG_VERBOSE("Starting backoff, retry %u", m_reconnect_manager.get_retry_count());
            start_backoff();
        }
        if (m_state_callback) {
            m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
        }
    }
}

/**
 * @brief Process received packets
 *
 * Polls TCP client for packets and handles each one.
 */
void RyuLdnClient::process_packets() {
    if (!m_tcp_client->is_connected()) {
        return;
    }

    // Try to receive packets
    uint8_t recv_buffer[2048];
    size_t recv_size = 0;
    protocol::PacketId packet_id;

    while (true) {
        ClientResult result = m_tcp_client->receive_packet(
            packet_id,
            recv_buffer,
            sizeof(recv_buffer),
            recv_size,
            m_config.recv_timeout_ms
        );

        if (result == ClientResult::Timeout) {
            // No more packets available
            break;
        }

        if (result == ClientResult::ConnectionLost) {
            LOG_INFO("process_packets: receive_packet returned ConnectionLost (server closed TCP)");
            ConnectionState before_state = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::ConnectionLost);
            if (m_config.auto_reconnect) {
                start_backoff();
            }
            if (m_state_callback && before_state != m_state_machine.get_state()) {
                m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
            }
            break;
        }

        if (result != ClientResult::Success) {
            LOG_INFO("process_packets: receive_packet returned %d (non-success), breaking",
                     static_cast<int>(result));
            break;
        }

        // Handle the packet
        handle_packet(packet_id, recv_buffer, recv_size);
    }
}

/**
 * @brief Handle a single received packet
 *
 * @param id Packet type
 * @param data Packet payload
 * @param size Payload size
 */
void RyuLdnClient::handle_packet(protocol::PacketId id,
                                  const uint8_t* data,
                                  size_t size) {
    // Handle protocol-level packets
    switch (id) {
        case protocol::PacketId::Ping: {
            // Handle ping according to RyuLDN protocol
            if (size >= sizeof(protocol::PingMessage)) {
                const auto* ping_msg =
                    reinterpret_cast<const protocol::PingMessage*>(data);

                if (ping_msg->requester == 0) {
                    // Server requested ping - echo it back immediately
                    protocol::PingMessage response = *ping_msg;
                    m_tcp_client->send_ping(response);
                    LOG_VERBOSE("Echoed ping id=%u back to server", ping_msg->id);
                } else {
                    // Response to our ping - connection is alive
                    if (m_pending_ping_count > 0) {
                        m_pending_ping_count = 0;
                    }
                    m_last_pong_time_ms = m_last_ping_time_ms;
                }
            }
            break;
        }

        case protocol::PacketId::Disconnect:
            // Server is disconnecting us
            m_state_machine.process_event(ConnectionEvent::Disconnect);
            break;

        default:
            // Pass to user callback
            if (m_packet_callback != nullptr) {
                m_packet_callback(id, data, size, m_packet_callback_user_data);
            }
            break;
    }
}

/**
 * @brief Send Initialize handshake message
 *
 * @return ClientOpResult indicating success or failure
 */
ClientOpResult RyuLdnClient::send_initialize() {
    LOG_VERBOSE("Sending Initialize handshake");

    // Send passphrase first (required by RyuLDN protocol)
    // This must be sent before Initialize, even if empty
    ClientResult passphrase_result = m_tcp_client->send_passphrase(m_config.passphrase);
    if (passphrase_result == ClientResult::ConnectionLost) {
        LOG_ERROR("Connection lost while sending Passphrase");
        ConnectionState before_state = m_state_machine.get_state();
        m_state_machine.process_event(ConnectionEvent::ConnectionLost);
        if (m_config.auto_reconnect) {
            start_backoff();
        }
        if (m_state_callback && before_state != m_state_machine.get_state()) {
            m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
        }
        return ClientOpResult::SendFailed;
    }
    if (passphrase_result != ClientResult::Success) {
        LOG_ERROR("Failed to send Passphrase: %s", client_result_to_string(passphrase_result));
        return ClientOpResult::SendFailed;
    }
    if (m_config.passphrase[0] != '\0') {
        LOG_INFO("Sent passphrase: %s", m_config.passphrase);
    } else {
        LOG_VERBOSE("Sent empty passphrase (public rooms)");
    }

    protocol::InitializeMessage msg{};

    // Generate a session ID (in real use, this would be a proper UUID)
    for (size_t i = 0; i < sizeof(msg.id.data); i++) {
        msg.id.data[i] = static_cast<uint8_t>(i ^ 0xAB);
    }

    // Copy our MAC address
    std::memcpy(msg.mac_address.data, m_mac_address.data, sizeof(msg.mac_address.data));

    ClientResult result = m_tcp_client->send_initialize(msg);
    if (result == ClientResult::ConnectionLost) {
        LOG_ERROR("Connection lost while sending Initialize");
        ConnectionState before_state = m_state_machine.get_state();
        m_state_machine.process_event(ConnectionEvent::ConnectionLost);
        if (m_config.auto_reconnect) {
            start_backoff();
        }
        if (m_state_callback && before_state != m_state_machine.get_state()) {
            m_state_callback(before_state, m_state_machine.get_state(), m_state_callback_user_data);
        }
        return ClientOpResult::SendFailed;
    }
    if (result != ClientResult::Success) {
        LOG_ERROR("Failed to send Initialize: %s", client_result_to_string(result));
        return ClientOpResult::SendFailed;
    }

    return ClientOpResult::Success;
}

/**
 * @brief Generate a unique MAC address
 *
 * Generates a statically assigned locally administered MAC address.
 * Format: X2:XX:XX:XX:XX:XX where X2 indicates locally administered.
 *
 * The address (02:00:5E:00:53:01) is hardcoded rather than random — every
 * session gets the same MAC. This matches the Ryujinx emulator behaviour
 * where all clients use the same MAC, and the server uses the node ID
 * (not the MAC) for identification.
 */
void RyuLdnClient::generate_mac_address() {
    // Statically assigned MAC matching Ryujinx convention.
    // The server identifies nodes by ID, not by MAC.
    m_mac_address.data[0] = 0x02;  // Locally administered
    m_mac_address.data[1] = 0x00;
    m_mac_address.data[2] = 0x5E;
    m_mac_address.data[3] = 0x00;
    m_mac_address.data[4] = 0x53;
    m_mac_address.data[5] = 0x01;
}

/**
 * @brief Start backoff timer
 *
 * Records the current time and calculates backoff delay.
 */
void RyuLdnClient::start_backoff() {
    // Use m_last_update_time_ms if available (called from within update()),
    // otherwise leave as 0 so is_backoff_expired() captures time on first check.
    // This handles the case where start_backoff() is called from connect()
    // (before any update() has been called) or from try_connect() (same).
    m_backoff_start_time_ms = m_last_update_time_ms;
    // Use jitter to prevent thundering herd when multiple clients reconnect
    // simultaneously (e.g., after a server restart).
    // Combine retry count and a simple counter for seed diversity.
    static uint32_t backoff_counter = 0;
    uint32_t seed = (m_reconnect_manager.get_retry_count() + 1) * 2654435761u + (++backoff_counter * 40503u);
    m_current_backoff_delay_ms = m_reconnect_manager.get_next_delay_ms_with_jitter(seed);
    LOG_INFO("start_backoff: delay=%u ms, retry=%u, start_time=%lu",
             m_current_backoff_delay_ms, m_reconnect_manager.get_retry_count(), m_backoff_start_time_ms);
}

/**
 * @brief Check if backoff has expired
 *
 * @param current_time_ms Current time in milliseconds
 * @return true if backoff period has elapsed
 */
bool RyuLdnClient::is_backoff_expired(uint64_t current_time_ms) {
    // If start_backoff() was called before the first update() (m_backoff_start_time_ms == 0),
    // capture current_time_ms as the start time now. This means the first update()
    // after start_backoff() starts the timer but doesn't expire it — expiry is checked
    // on the subsequent update() call.
    if (m_backoff_start_time_ms == 0) {
        m_backoff_start_time_ms = current_time_ms;
        return false;  // Start timer this tick, check expiry next tick
    }

    return (current_time_ms - m_backoff_start_time_ms) >= m_current_backoff_delay_ms;
}

/**
 * @brief Check if handshake has timed out
 *
 * @param current_time_ms Current time in milliseconds
 * @return true if handshake timeout has elapsed
 */
bool RyuLdnClient::is_handshake_timeout(uint64_t current_time_ms) const {
    if (m_handshake_start_time_ms == 0) {
        return false;
    }

    return (current_time_ms - m_handshake_start_time_ms) >= m_handshake_timeout_ms;
}

/**
 * @brief Process handshake response from server
 *
 * Handles the server's response to our Initialize message.
 * The RyuLDN server responds with an Initialize packet containing:
 * - Assigned session ID (16 bytes)
 * - Assigned MAC address (6 bytes)
 *
 * This matches the official Ryujinx client behavior.
 *
 * @param id Packet ID received
 * @param data Packet data
 * @param size Packet size
 * @return true if handshake completed (success or failure)
 */
bool RyuLdnClient::process_handshake_response(protocol::PacketId id,
                                               const uint8_t* data,
                                               size_t size) {
    LOG_VERBOSE("Received handshake response: packet_id=%u", static_cast<uint32_t>(id));

    switch (id) { // codeql[cpp/long-switch] natural dispatch: 4 handshake packet types
        case protocol::PacketId::Initialize: {
            // Server responds with Initialize containing assigned ID and MAC
            // This is the expected response from RyuLDN server
            if (size >= sizeof(protocol::InitializeMessage)) {
                const auto* init_msg =
                    reinterpret_cast<const protocol::InitializeMessage*>(data);

                // Store assigned session ID and MAC address
                std::memcpy(m_session_id.data, init_msg->id.data, sizeof(m_session_id.data));
                std::memcpy(m_mac_address.data, init_msg->mac_address.data, sizeof(m_mac_address.data));

                LOG_INFO("Handshake successful - assigned MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                         m_mac_address.data[0], m_mac_address.data[1], m_mac_address.data[2],
                         m_mac_address.data[3], m_mac_address.data[4], m_mac_address.data[5]);
            }

            m_last_error_code = protocol::NetworkErrorCode::None;
            ConnectionState before_hs = m_state_machine.get_state();
            m_state_machine.process_event(ConnectionEvent::HandshakeSuccess);
            if (m_state_callback) {
                m_state_callback(before_hs, m_state_machine.get_state(), m_state_callback_user_data);
            }
            return true;
        }

        case protocol::PacketId::NetworkError: {
            // Server rejected our handshake
            if (size >= sizeof(protocol::NetworkErrorMessage)) {
                const auto* error_msg =
                    reinterpret_cast<const protocol::NetworkErrorMessage*>(data);
                m_last_error_code =
                    static_cast<protocol::NetworkErrorCode>(error_msg->error_code);
            } else {
                m_last_error_code = protocol::NetworkErrorCode::InternalError;
            }

            LOG_ERROR("Handshake rejected: error_code=%u", static_cast<uint32_t>(m_last_error_code));

            // Check for version mismatch specifically
            if (m_last_error_code == protocol::NetworkErrorCode::VersionMismatch) {
                // Version mismatch is a fatal error - no point retrying
                LOG_ERROR("Version mismatch - fatal error");
                ConnectionState before_err = m_state_machine.get_state();
                m_state_machine.process_event(ConnectionEvent::FatalError);
                if (m_state_callback) {
                    m_state_callback(before_err, m_state_machine.get_state(), m_state_callback_user_data);
                }
            } else {
                // Other errors might be recoverable
                ConnectionState before_err = m_state_machine.get_state();
                m_state_machine.process_event(ConnectionEvent::HandshakeFailed);
                if (m_config.auto_reconnect) {
                    start_backoff();
                }
                if (m_state_callback) {
                    m_state_callback(before_err, m_state_machine.get_state(), m_state_callback_user_data);
                }
            }
            return true;
        }

        case protocol::PacketId::SyncNetwork: {
            // Alternative: some server versions may send SyncNetwork
            LOG_INFO("Handshake successful (SyncNetwork) - ready");
            m_last_error_code = protocol::NetworkErrorCode::None;
            {
                ConnectionState before_hs = m_state_machine.get_state();
                m_state_machine.process_event(ConnectionEvent::HandshakeSuccess);
                if (m_state_callback) {
                    m_state_callback(before_hs, m_state_machine.get_state(), m_state_callback_user_data);
                }
            }
            return true;
        }

        case protocol::PacketId::Disconnect: {
            // Server disconnected us during handshake
            LOG_WARN("Server disconnected during handshake");
            m_last_error_code = protocol::NetworkErrorCode::ConnectionRejected;
            {
                ConnectionState before_disc = m_state_machine.get_state();
                m_state_machine.process_event(ConnectionEvent::HandshakeFailed);
                if (m_config.auto_reconnect) {
                    start_backoff();
                }
                if (m_state_callback) {
                    m_state_callback(before_disc, m_state_machine.get_state(), m_state_callback_user_data);
                }
            }
            return true;
        }

        default:
            // Unexpected packet during handshake
            // Could be out-of-order delivery, just ignore and keep waiting
            LOG_VERBOSE("Unexpected packet during handshake: %u", static_cast<uint32_t>(id));
            // Pass to user callback if set
            if (m_packet_callback != nullptr) {
                m_packet_callback(id, data, size, m_packet_callback_user_data);
            }
            return false;
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Convert ClientOpResult to string
 *
 * @param result Result to convert
 * @return Human-readable string
 */
const char* client_op_result_to_string(ClientOpResult result) {
    switch (result) {
        case ClientOpResult::Success:          return "Success";
        case ClientOpResult::NotConnected:     return "NotConnected";
        case ClientOpResult::NotReady:         return "NotReady";
        case ClientOpResult::AlreadyConnected: return "AlreadyConnected";
        case ClientOpResult::ConnectionFailed: return "ConnectionFailed";
        case ClientOpResult::SendFailed:       return "SendFailed";
        case ClientOpResult::InvalidState:     return "InvalidState";
        case ClientOpResult::Timeout:          return "Timeout";
        case ClientOpResult::ProtocolError:    return "ProtocolError";
        case ClientOpResult::InternalError:    return "InternalError";
        default:                               return "Unknown";
    }
}

} // namespace network
} // namespace ryu_ldn
