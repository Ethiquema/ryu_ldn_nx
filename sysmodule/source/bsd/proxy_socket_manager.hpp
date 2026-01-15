/**
 * @file proxy_socket_manager.hpp
 * @brief Proxy Socket Manager - Central Registry for LDN Proxy Sockets
 *
 * This file defines the ProxySocketManager class which manages all proxy sockets
 * for LDN communication. It acts as a bridge between the BSD MITM service and
 * the LDN protocol layer.
 *
 * ## Responsibilities
 *
 * 1. **Socket Registry**: Maps file descriptors to ProxySocket instances
 * 2. **Port Allocation**: Manages ephemeral port pool for proxy sockets
 * 3. **Data Routing**: Routes incoming ProxyData packets to the correct socket
 * 4. **LDN Detection**: Determines if an address belongs to the LDN network
 *
 * ## Architecture
 *
 * ```
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │ BSD MITM Service                                                        │
 * │   Bind(fd, 10.114.x.x) ──┐                                              │
 * │   Connect(fd, 10.114.x.x)├──► ProxySocketManager                        │
 * │   Send(fd) ──────────────┤      │                                       │
 * │   Recv(fd) ◄─────────────┘      │                                       │
 * │                                 ▼                                       │
 * │                           ┌──────────┐                                  │
 * │                           │ Registry │ fd → ProxySocket                 │
 * │                           └──────────┘                                  │
 * │                                 │                                       │
 * │                                 ▼                                       │
 * │                           ┌────────────────┐                            │
 * │                           │ EphemeralPorts │                            │
 * │                           └────────────────┘                            │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │ LDN MITM Service                                                        │
 * │   ProxyData packet ────────► ProxySocketManager.RouteIncomingData()     │
 * │                                     │                                   │
 * │                                     ▼                                   │
 * │                              ProxySocket.IncomingData()                 │
 * └─────────────────────────────────────────────────────────────────────────┘
 * ```
 *
 * ## Thread Safety
 *
 * All public methods are thread-safe. The manager uses a mutex to protect
 * the socket registry. Individual ProxySocket instances have their own
 * synchronization for receive queues.
 *
 * ## Singleton Pattern
 *
 * The manager is a singleton because:
 * - There's only one BSD service being MITMed
 * - The LDN MITM needs to route data to the same registry
 * - File descriptors are global to the process
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include <unordered_map>
#include <memory>
#include "proxy_socket.hpp"
#include "ephemeral_port_pool.hpp"
#include "bsd_types.hpp"
#include "../protocol/types.hpp"

namespace ams::mitm::bsd {

/**
 * @brief Maximum number of concurrent proxy sockets
 *
 * Limits memory usage. Games typically don't need many sockets for LDN.
 */
constexpr size_t MAX_PROXY_SOCKETS = 64;

/**
 * @brief Invalid file descriptor sentinel
 */
constexpr s32 INVALID_FD = -1;

/**
 * @brief Proxy Socket Manager
 *
 * Central registry for all proxy sockets. Manages the mapping between
 * BSD file descriptors and ProxySocket instances.
 *
 * ## Key Operations
 *
 * - CreateProxySocket: Allocate a new proxy socket for an fd
 * - GetProxySocket: Look up a proxy socket by fd
 * - CloseProxySocket: Clean up a proxy socket
 * - RouteIncomingData: Route ProxyData packets to the correct socket
 *
 * ## File Descriptor Strategy
 *
 * We use the game's real file descriptors (from Socket() calls to the
 * real BSD service) as keys. When we detect an LDN address in Bind/Connect,
 * we create a ProxySocket associated with that fd. Subsequent Send/Recv
 * calls check if the fd has an associated ProxySocket and route accordingly.
 */
class ProxySocketManager {
public:
    /**
     * @brief Get the singleton instance
     * @return Reference to the global ProxySocketManager
     */
    static ProxySocketManager& GetInstance();

    /**
     * @brief Deleted copy constructor
     */
    ProxySocketManager(const ProxySocketManager&) = delete;
    ProxySocketManager& operator=(const ProxySocketManager&) = delete;

    // =========================================================================
    // Socket Management
    // =========================================================================

    /**
     * @brief Create a new proxy socket for the given file descriptor
     *
     * Called when we detect that a socket should be proxied (LDN address
     * in Bind or Connect).
     *
     * @param fd File descriptor from the real BSD service
     * @param type Socket type (Stream or Dgram)
     * @param protocol Protocol type (Tcp or Udp)
     * @return Pointer to the created ProxySocket, or nullptr if failed
     *
     * @note Thread-safe
     * @note Returns nullptr if fd already has a proxy socket
     */
    ProxySocket* CreateProxySocket(s32 fd, ryu_ldn::bsd::SocketType type, ryu_ldn::bsd::ProtocolType protocol);

    /**
     * @brief Get the proxy socket for a file descriptor
     *
     * @param fd File descriptor to look up
     * @return Pointer to the ProxySocket, or nullptr if not a proxy socket
     *
     * @note Thread-safe
     * @note The returned pointer is valid as long as the socket is not closed
     */
    ProxySocket* GetProxySocket(s32 fd);

    /**
     * @brief Check if a file descriptor has an associated proxy socket
     *
     * @param fd File descriptor to check
     * @return true if fd is a proxy socket, false otherwise
     *
     * @note Thread-safe
     */
    bool IsProxySocket(s32 fd) const;

    /**
     * @brief Close and remove a proxy socket
     *
     * Called when the game closes the socket.
     *
     * @param fd File descriptor to close
     * @return true if a proxy socket was closed, false if not found
     *
     * @note Thread-safe
     * @note Also releases any allocated ephemeral port
     */
    bool CloseProxySocket(s32 fd);

    /**
     * @brief Close all proxy sockets
     *
     * Called when the LDN session ends or during cleanup.
     *
     * @note Thread-safe
     */
    void CloseAllProxySockets();

    // =========================================================================
    // Port Management
    // =========================================================================

    /**
     * @brief Allocate an ephemeral port
     *
     * @param protocol Protocol type (Tcp or Udp)
     * @return Allocated port in host byte order, or 0 if none available
     *
     * @note Thread-safe (delegated to EphemeralPortPool)
     */
    uint16_t AllocatePort(ryu_ldn::bsd::ProtocolType protocol);

    /**
     * @brief Reserve a specific port
     *
     * Used when the game binds to a specific port.
     *
     * @param port Port number in host byte order
     * @param protocol Protocol type
     * @return true if reserved, false if already in use
     *
     * @note Thread-safe
     */
    bool ReservePort(uint16_t port, ryu_ldn::bsd::ProtocolType protocol);

    /**
     * @brief Release a port back to the pool
     *
     * @param port Port number in host byte order
     * @param protocol Protocol type
     *
     * @note Thread-safe
     */
    void ReleasePort(uint16_t port, ryu_ldn::bsd::ProtocolType protocol);

    // =========================================================================
    // Data Routing
    // =========================================================================

    /**
     * @brief Route incoming ProxyData to the correct socket
     *
     * Called by the LDN MITM service when a ProxyData packet is received.
     * Finds the socket that matches the destination address/port and queues
     * the data for that socket.
     *
     * @param source_ip Source IP (host byte order)
     * @param source_port Source port (host byte order)
     * @param dest_ip Destination IP (host byte order)
     * @param dest_port Destination port (host byte order)
     * @param protocol Protocol type
     * @param data Packet payload
     * @param data_len Payload length
     * @return true if data was routed to a socket, false if no matching socket
     *
     * @note Thread-safe
     */
    bool RouteIncomingData(uint32_t source_ip, uint16_t source_port,
                           uint32_t dest_ip, uint16_t dest_port,
                           ryu_ldn::bsd::ProtocolType protocol,
                           const void* data, size_t data_len);

    // =========================================================================
    // Outgoing Data
    // =========================================================================

    /**
     * @brief Callback type for sending ProxyData to the LDN server
     *
     * This callback is invoked when a proxy socket needs to send data.
     * The LDN MITM service registers this callback to handle sending.
     *
     * @param source_ip Source IP (host byte order)
     * @param source_port Source port (host byte order)
     * @param dest_ip Destination IP (host byte order)
     * @param dest_port Destination port (host byte order)
     * @param protocol Protocol type (TCP/UDP)
     * @param data Packet payload
     * @param data_len Payload length
     * @return true if data was sent successfully, false otherwise
     */
    using SendProxyDataCallback = bool (*)(uint32_t source_ip, uint16_t source_port,
                                           uint32_t dest_ip, uint16_t dest_port,
                                           ryu_ldn::bsd::ProtocolType protocol,
                                           const void* data, size_t data_len);

    /**
     * @brief Set the callback for sending ProxyData to the LDN server
     *
     * Called by the LDN MITM service during initialization.
     *
     * @param callback Function to call when proxy sockets need to send data
     *
     * @note Thread-safe
     */
    void SetSendCallback(SendProxyDataCallback callback);

    /**
     * @brief Send data through a proxy socket
     *
     * Called by ProxySocket::SendTo to actually transmit data via LDN.
     *
     * @param source_ip Source IP (host byte order)
     * @param source_port Source port (host byte order)
     * @param dest_ip Destination IP (host byte order)
     * @param dest_port Destination port (host byte order)
     * @param protocol Protocol type
     * @param data Packet payload
     * @param data_len Payload length
     * @return true if data was sent, false if no callback registered or send failed
     *
     * @note Thread-safe
     */
    bool SendProxyData(uint32_t source_ip, uint16_t source_port,
                       uint32_t dest_ip, uint16_t dest_port,
                       ryu_ldn::bsd::ProtocolType protocol,
                       const void* data, size_t data_len);

    /**
     * @brief Callback type for sending ProxyConnect to the LDN server
     *
     * This callback is invoked when a TCP proxy socket calls Connect().
     *
     * @param source_ip Source IP (host byte order)
     * @param source_port Source port (host byte order)
     * @param dest_ip Destination IP (host byte order)
     * @param dest_port Destination port (host byte order)
     * @param protocol Protocol type (TCP)
     * @return true if request was sent successfully, false otherwise
     */
    using SendProxyConnectCallback = bool (*)(uint32_t source_ip, uint16_t source_port,
                                               uint32_t dest_ip, uint16_t dest_port,
                                               ryu_ldn::bsd::ProtocolType protocol);

    /**
     * @brief Set the callback for sending ProxyConnect to the LDN server
     *
     * Called by the LDN MITM service during initialization.
     *
     * @param callback Function to call when TCP proxy sockets call Connect()
     *
     * @note Thread-safe
     */
    void SetProxyConnectCallback(SendProxyConnectCallback callback);

    /**
     * @brief Send ProxyConnect request for TCP connection handshake
     *
     * Called by ProxySocket::Connect for TCP sockets.
     *
     * @param source_ip Source IP (host byte order)
     * @param source_port Source port (host byte order)
     * @param dest_ip Destination IP (host byte order)
     * @param dest_port Destination port (host byte order)
     * @param protocol Protocol type (TCP)
     * @return true if request was sent, false if no callback or send failed
     *
     * @note Thread-safe
     */
    bool SendProxyConnect(uint32_t source_ip, uint16_t source_port,
                          uint32_t dest_ip, uint16_t dest_port,
                          ryu_ldn::bsd::ProtocolType protocol);

    /**
     * @brief Route incoming ProxyConnectReply to the connecting socket
     *
     * Called by the LDN MITM service when a ProxyConnectReply packet arrives.
     *
     * @param response The connect response
     * @return true if routed successfully, false if no matching socket
     *
     * @note Thread-safe
     */
    bool RouteConnectResponse(const ryu_ldn::protocol::ProxyConnectResponse& response);

    /**
     * @brief Route incoming ProxyConnect to a listening socket (accept queue)
     *
     * Called by the LDN MITM service when a ProxyConnect packet arrives
     * for a listening socket (incoming TCP connection).
     *
     * @param request The connect request
     * @return true if routed successfully, false if no matching listener
     *
     * @note Thread-safe
     */
    bool RouteConnectRequest(const ryu_ldn::protocol::ProxyConnectRequest& request);

    // =========================================================================
    // LDN Network Configuration
    // =========================================================================

    /**
     * @brief Set the local LDN IP address
     *
     * Called when the game receives its IP from GetIpv4Address.
     *
     * @param ip Local IP address in host byte order (e.g., 0x0A720001 for 10.114.0.1)
     *
     * @note Thread-safe
     */
    void SetLocalIp(uint32_t ip);

    /**
     * @brief Get the local LDN IP address
     *
     * @return Local IP in host byte order, or 0 if not set
     *
     * @note Thread-safe
     */
    uint32_t GetLocalIp() const;

    /**
     * @brief Check if an IP address is in the LDN network
     *
     * @param ip IP address in host byte order
     * @return true if ip is in 10.114.0.0/16
     */
    static bool IsLdnAddress(uint32_t ip) {
        return ryu_ldn::bsd::IsLdnAddress(ip);
    }

    /**
     * @brief Check if a sockaddr_in is in the LDN network
     *
     * @param addr Address to check
     * @return true if address is in 10.114.0.0/16
     */
    static bool IsLdnAddress(const ryu_ldn::bsd::SockAddrIn& addr) {
        return addr.IsLdnAddress();
    }

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Get the number of active proxy sockets
     * @return Number of proxy sockets currently managed
     */
    size_t GetActiveSocketCount() const;

    /**
     * @brief Get the number of available ephemeral ports
     *
     * @param protocol Protocol type
     * @return Available port count
     */
    size_t GetAvailablePortCount(ryu_ldn::bsd::ProtocolType protocol) const;

private:
    /**
     * @brief Private constructor (singleton)
     */
    ProxySocketManager() = default;

    /**
     * @brief Destructor
     */
    ~ProxySocketManager() = default;

    /**
     * @brief Find a socket matching the given destination
     *
     * @param dest_ip Destination IP (host byte order)
     * @param dest_port Destination port (host byte order)
     * @param protocol Protocol type
     * @return Pointer to matching socket, or nullptr if not found
     *
     * @note Caller must hold m_mutex
     */
    ProxySocket* FindSocketByDestination(uint32_t dest_ip, uint16_t dest_port,
                                          ryu_ldn::bsd::ProtocolType protocol);

    /**
     * @brief Mutex for thread safety
     */
    mutable os::Mutex m_mutex{false};

    /**
     * @brief Map of file descriptor to ProxySocket
     */
    std::unordered_map<s32, std::unique_ptr<ProxySocket>> m_sockets;

    /**
     * @brief Ephemeral port pool
     */
    EphemeralPortPool m_port_pool;

    /**
     * @brief Local LDN IP address (host byte order)
     */
    uint32_t m_local_ip{0};

    /**
     * @brief Callback for sending ProxyData to LDN server
     */
    SendProxyDataCallback m_send_callback{nullptr};

    /**
     * @brief Callback for sending ProxyConnect to LDN server (TCP handshake)
     */
    SendProxyConnectCallback m_proxy_connect_callback{nullptr};
};

} // namespace ams::mitm::bsd
