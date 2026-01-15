/**
 * @file proxy_socket.hpp
 * @brief Proxy Socket for LDN Traffic Routing
 *
 * This file defines the ProxySocket class which represents a virtual socket
 * for LDN network communication. Instead of using real network sockets,
 * ProxySockets route data through ProxyData packets to the Ryujinx LDN server.
 *
 * ## Design
 *
 * ProxySocket mimics the behavior of a real BSD socket but:
 * - Sends data by encoding it as ProxyData packets to the server
 * - Receives data from a local queue populated by incoming ProxyData packets
 * - Tracks local/remote addresses in the virtual 10.114.x.x network
 *
 * ## Data Flow
 *
 * ```
 * Game calls Send() ─────► ProxySocket.Send() ─────► ProxyData packet ─────► Server
 *
 * Server ─────► ProxyData packet ─────► ProxySocket.IncomingData() ─────► Queue
 *                                                                           │
 * Game calls Recv() ◄───── ProxySocket.Recv() ◄─────────────────────────────┘
 * ```
 *
 * ## Thread Safety
 *
 * The receive queue is protected by a mutex. The receive event can be used
 * to block until data is available (for blocking Recv calls).
 *
 * ## Lifecycle
 *
 * 1. Create: Socket() creates a new ProxySocket (unbound, unconnected)
 * 2. Bind: Bind() assigns a local address/port
 * 3. Connect: Connect() sets the remote address (sends ProxyConnect to server)
 * 4. Data: Send()/Recv() transfer data via ProxyData packets
 * 5. Close: Close() cleans up and sends ProxyDisconnect
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include <deque>
#include <vector>
#include "bsd_types.hpp"

namespace ams::mitm::bsd {

// Forward declaration
class ProxySocketManager;

/**
 * @brief Maximum size of the receive queue per socket
 *
 * Limits memory usage per socket. If the queue is full, oldest
 * packets are dropped (UDP behavior).
 */
constexpr size_t PROXY_SOCKET_MAX_QUEUE_SIZE = 64;

/**
 * @brief Maximum payload size for a single ProxyData packet
 *
 * This matches the typical MTU minus headers. Games usually send
 * smaller packets for LDN communication.
 */
constexpr size_t PROXY_SOCKET_MAX_PAYLOAD = 1400;

/**
 * @brief State of a proxy socket
 */
enum class ProxySocketState {
    Created,        ///< Socket created but not bound
    Bound,          ///< Socket bound to local address
    Connected,      ///< Socket connected to remote (TCP) or has default dest (UDP)
    Listening,      ///< Socket listening for connections (TCP only)
    Closed,         ///< Socket closed, awaiting cleanup
};

/**
 * @brief Received packet data with source information
 *
 * Stores a received ProxyData packet along with the source address
 * for RecvFrom() calls.
 */
struct ReceivedPacket {
    std::vector<uint8_t> data;     ///< Packet payload
    ryu_ldn::bsd::SockAddrIn from; ///< Source address
};

/**
 * @brief Proxy Socket for LDN Network Communication
 *
 * This class represents a virtual socket that routes traffic through
 * the Ryujinx LDN server via ProxyData packets instead of using real
 * network sockets.
 *
 * ## Key Features
 *
 * - Mimics BSD socket API (bind, connect, send, recv)
 * - Routes data via ProxyData protocol to server
 * - Maintains receive queue for incoming data
 * - Supports both UDP and TCP semantics
 * - Thread-safe receive queue
 *
 * ## Example Usage
 *
 * ```cpp
 * // Create and bind a UDP proxy socket
 * auto socket = std::make_unique<ProxySocket>(SocketType::Dgram, ProtocolType::Udp);
 * socket->Bind(local_addr);
 *
 * // Send data to a peer
 * socket->SendTo(data, data_len, remote_addr);
 *
 * // Receive data (blocking if no data available)
 * socket->RecvFrom(buffer, buffer_size, &from_addr);
 * ```
 */
class ProxySocket {
public:
    /**
     * @brief Construct a new proxy socket
     *
     * Creates an unbound, unconnected proxy socket of the specified type.
     *
     * @param type Socket type (Stream for TCP, Dgram for UDP)
     * @param protocol Protocol type (Tcp or Udp)
     */
    ProxySocket(ryu_ldn::bsd::SocketType type, ryu_ldn::bsd::ProtocolType protocol);

    /**
     * @brief Destructor
     *
     * Cleans up resources. Does NOT send ProxyDisconnect - call Close() first.
     */
    ~ProxySocket();

    /**
     * @brief Non-copyable (socket identity is unique)
     */
    ProxySocket(const ProxySocket&) = delete;
    ProxySocket& operator=(const ProxySocket&) = delete;

    /**
     * @brief Move constructor
     */
    ProxySocket(ProxySocket&&) = default;
    ProxySocket& operator=(ProxySocket&&) = default;

    // =========================================================================
    // Socket State
    // =========================================================================

    /**
     * @brief Get the current socket state
     * @return Current state (Created, Bound, Connected, etc.)
     */
    ProxySocketState GetState() const { return m_state; }

    /**
     * @brief Get the socket type
     * @return Socket type (Stream or Dgram)
     */
    ryu_ldn::bsd::SocketType GetType() const { return m_type; }

    /**
     * @brief Get the protocol type
     * @return Protocol type (Tcp or Udp)
     */
    ryu_ldn::bsd::ProtocolType GetProtocol() const { return m_protocol; }

    /**
     * @brief Check if socket is non-blocking
     * @return true if non-blocking mode is enabled
     */
    bool IsNonBlocking() const { return m_non_blocking; }

    /**
     * @brief Set non-blocking mode
     * @param non_blocking true to enable non-blocking mode
     */
    void SetNonBlocking(bool non_blocking) { m_non_blocking = non_blocking; }

    // =========================================================================
    // Address Management
    // =========================================================================

    /**
     * @brief Bind the socket to a local address
     *
     * Assigns a local address and port to the socket. If the port is 0,
     * the caller should allocate an ephemeral port before calling this.
     *
     * @param addr Local address to bind to (must be in LDN network 10.114.x.x)
     * @return Success or error (e.g., already bound, invalid address)
     *
     * @note Does not validate that the address is actually ours - caller must check
     */
    Result Bind(const ryu_ldn::bsd::SockAddrIn& addr);

    /**
     * @brief Connect to a remote address
     *
     * For TCP, this initiates a connection to the remote peer.
     * For UDP, this sets the default destination for Send().
     *
     * @param addr Remote address to connect to
     * @return Success or error
     *
     * @note For TCP, this should trigger sending ProxyConnect to the server
     */
    Result Connect(const ryu_ldn::bsd::SockAddrIn& addr);

    /**
     * @brief Get the local address
     *
     * @param out_addr Output: local address
     * @return Success or error if not bound
     */
    Result GetSockName(ryu_ldn::bsd::SockAddrIn* out_addr) const;

    /**
     * @brief Get the remote address (connected sockets only)
     *
     * @param out_addr Output: remote address
     * @return Success or error if not connected
     */
    Result GetPeerName(ryu_ldn::bsd::SockAddrIn* out_addr) const;

    /**
     * @brief Get the local address reference
     * @return Reference to local address structure
     */
    const ryu_ldn::bsd::SockAddrIn& GetLocalAddr() const { return m_local_addr; }

    /**
     * @brief Get the remote address reference
     * @return Reference to remote address structure
     */
    const ryu_ldn::bsd::SockAddrIn& GetRemoteAddr() const { return m_remote_addr; }

    // =========================================================================
    // Data Transfer
    // =========================================================================

    /**
     * @brief Send data (connected sockets only)
     *
     * Sends data to the connected peer. Socket must be connected first.
     *
     * @param data Pointer to data to send
     * @param len Length of data
     * @param flags Send flags (currently ignored)
     * @return Bytes sent or negative errno on error
     */
    s32 Send(const void* data, size_t len, s32 flags);

    /**
     * @brief Send data to a specific address
     *
     * Sends data to the specified destination. Does not require connection.
     *
     * @param data Pointer to data to send
     * @param len Length of data
     * @param flags Send flags (currently ignored)
     * @param dest Destination address
     * @return Bytes sent or negative errno on error
     */
    s32 SendTo(const void* data, size_t len, s32 flags, const ryu_ldn::bsd::SockAddrIn& dest);

    /**
     * @brief Receive data (connected sockets only)
     *
     * Receives data from the connected peer.
     *
     * @param buffer Buffer to receive into
     * @param len Buffer size
     * @param flags Receive flags (currently supports MSG_PEEK, MSG_DONTWAIT)
     * @return Bytes received, 0 if connection closed, or negative errno on error
     */
    s32 Recv(void* buffer, size_t len, s32 flags);

    /**
     * @brief Receive data with source address
     *
     * Receives data and returns the source address.
     *
     * @param buffer Buffer to receive into
     * @param len Buffer size
     * @param flags Receive flags
     * @param from Output: source address (can be nullptr)
     * @return Bytes received, 0 if connection closed, or negative errno on error
     */
    s32 RecvFrom(void* buffer, size_t len, s32 flags, ryu_ldn::bsd::SockAddrIn* from);

    /**
     * @brief Queue incoming data from a ProxyData packet
     *
     * Called by the ProxySocketManager when a ProxyData packet arrives
     * that matches this socket.
     *
     * @param data Packet payload
     * @param len Payload length
     * @param from Source address
     *
     * @note Thread-safe. Signals the receive event.
     */
    void IncomingData(const void* data, size_t len, const ryu_ldn::bsd::SockAddrIn& from);

    // =========================================================================
    // Socket Options
    // =========================================================================

    /**
     * @brief Set a socket option
     *
     * Most options are stored locally and don't affect real network behavior
     * since we're proxying through the server.
     *
     * @param level Option level (SOL_SOCKET, etc.)
     * @param optname Option name
     * @param optval Option value
     * @param optlen Option value length
     * @return Success or error
     */
    Result SetSockOpt(s32 level, s32 optname, const void* optval, size_t optlen);

    /**
     * @brief Get a socket option
     *
     * @param level Option level
     * @param optname Option name
     * @param optval Output: option value
     * @param optlen Input/Output: option value length
     * @return Success or error
     */
    Result GetSockOpt(s32 level, s32 optname, void* optval, size_t* optlen) const;

    // =========================================================================
    // TCP-specific Operations
    // =========================================================================

    /**
     * @brief Start listening for connections (TCP only)
     *
     * @param backlog Maximum pending connections (currently ignored)
     * @return Success or error if not TCP or not bound
     */
    Result Listen(s32 backlog);

    /**
     * @brief Accept a connection (TCP only)
     *
     * Blocks until a connection is available (unless non-blocking).
     *
     * @param out_addr Output: connecting peer's address
     * @return New connected ProxySocket or nullptr on error
     *
     * @note The returned socket is owned by the caller
     */
    std::unique_ptr<ProxySocket> Accept(ryu_ldn::bsd::SockAddrIn* out_addr);

    // =========================================================================
    // Shutdown and Close
    // =========================================================================

    /**
     * @brief Shutdown the socket
     *
     * @param how How to shutdown (read, write, or both)
     * @return Success or error
     */
    Result Shutdown(ryu_ldn::bsd::ShutdownHow how);

    /**
     * @brief Close the socket
     *
     * Marks the socket as closed and releases resources.
     * For TCP, this may send ProxyDisconnect to the server.
     *
     * @return Success or error
     */
    Result Close();

    // =========================================================================
    // Event Handling
    // =========================================================================

    /**
     * @brief Check if data is available to read
     * @return true if receive queue is not empty
     */
    bool HasPendingData() const;

    /**
     * @brief Get the number of bytes available to read
     * @return Total bytes in receive queue
     */
    size_t GetPendingDataSize() const;

    /**
     * @brief Wait for data to be available
     *
     * Blocks until data is available or timeout expires.
     *
     * @param timeout_ms Timeout in milliseconds (0 = infinite)
     * @return true if data is available, false if timeout
     */
    bool WaitForData(u64 timeout_ms);

    /**
     * @brief Get the receive event handle
     *
     * Can be used with poll/select to wait for data.
     *
     * @return Reference to the receive event
     */
    os::Event& GetReceiveEvent() { return m_receive_event; }

private:
    /**
     * @brief Pop the front packet from the receive queue
     *
     * @param peek If true, don't remove from queue (MSG_PEEK)
     * @return The front packet, or empty if queue is empty
     *
     * @note Caller must hold m_queue_mutex
     */
    ReceivedPacket PopFrontPacket(bool peek);

    /**
     * @brief Socket type (Stream or Dgram)
     */
    ryu_ldn::bsd::SocketType m_type;

    /**
     * @brief Protocol type (Tcp or Udp)
     */
    ryu_ldn::bsd::ProtocolType m_protocol;

    /**
     * @brief Current socket state
     */
    ProxySocketState m_state{ProxySocketState::Created};

    /**
     * @brief Non-blocking mode flag
     */
    bool m_non_blocking{false};

    /**
     * @brief Shutdown flags
     */
    bool m_shutdown_read{false};
    bool m_shutdown_write{false};

    /**
     * @brief Local address (set by Bind)
     */
    ryu_ldn::bsd::SockAddrIn m_local_addr{};

    /**
     * @brief Remote address (set by Connect)
     */
    ryu_ldn::bsd::SockAddrIn m_remote_addr{};

    /**
     * @brief Receive queue mutex
     */
    mutable os::Mutex m_queue_mutex{false};

    /**
     * @brief Receive queue (incoming packets)
     */
    std::deque<ReceivedPacket> m_receive_queue;

    /**
     * @brief Event signaled when data is available
     */
    os::Event m_receive_event{os::EventClearMode_ManualClear};

    /**
     * @brief TCP accept queue (pending connections)
     */
    std::deque<std::unique_ptr<ProxySocket>> m_accept_queue;

    /**
     * @brief Socket options storage
     *
     * Most options are stored locally since we're proxying.
     * Format: map of (level << 16 | optname) -> value
     */
    // TODO: Add options storage if needed
};

} // namespace ams::mitm::bsd
