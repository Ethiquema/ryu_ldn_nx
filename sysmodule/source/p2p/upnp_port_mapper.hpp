/**
 * @file upnp_port_mapper.hpp
 * @brief UPnP Port Mapper - NAT Traversal for P2P Connections
 *
 * This file defines the UpnpPortMapper class which handles UPnP IGD
 * (Internet Gateway Device) discovery and port mapping for P2P connections.
 *
 * ## Purpose
 *
 * When hosting a P2P game session, the Switch needs to be reachable from
 * the internet. UPnP allows automatic port forwarding on compatible routers.
 *
 * ## Usage
 *
 * ```cpp
 * auto& mapper = UpnpPortMapper::GetInstance();
 *
 * // Discover UPnP gateway (blocking, ~2.5s timeout)
 * if (mapper.Discover()) {
 *     // Open port for P2P server
 *     if (mapper.AddPortMapping(39990, 39990, "ryu_ldn_nx P2P")) {
 *         // Port is now forwarded
 *         char external_ip[16];
 *         mapper.GetExternalIPAddress(external_ip, sizeof(external_ip));
 *     }
 * }
 *
 * // Cleanup when done
 * mapper.DeletePortMapping(39990);
 * ```
 *
 * ## Ryujinx Compatibility
 *
 * Constants and behavior match Ryujinx's Open.NAT implementation:
 * - Port range: 39990-39999
 * - Lease duration: 60 seconds
 * - Lease renewal: every 50 seconds
 * - Discovery timeout: 2500ms
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>

// Forward declarations for miniupnpc types
struct UPNPUrls;
struct IGDdatas;
struct UPNPDev;

namespace ams::mitm::p2p {

/**
 * @brief P2P port range base (matches Ryujinx)
 */
constexpr uint16_t P2P_PORT_BASE = 39990;

/**
 * @brief P2P port range size
 */
constexpr int P2P_PORT_RANGE = 10;

/**
 * @brief UPnP discovery timeout in milliseconds
 */
constexpr int UPNP_DISCOVERY_TIMEOUT_MS = 2500;

/**
 * @brief Port mapping lease duration in seconds
 */
constexpr int PORT_LEASE_DURATION = 60;

/**
 * @brief Port mapping renewal interval in seconds
 */
constexpr int PORT_LEASE_RENEW = 50;

/**
 * @brief UPnP Port Mapper
 *
 * Manages UPnP IGD discovery and port mapping for P2P connections.
 * This is a singleton class as there's only one network interface.
 *
 * ## Thread Safety
 *
 * All methods are thread-safe. Discovery and port mapping operations
 * are protected by a mutex.
 *
 * ## Error Handling
 *
 * All methods return bool indicating success/failure. Detailed error
 * information is logged but not exposed to callers.
 */
class UpnpPortMapper {
public:
    /**
     * @brief Get the singleton instance
     * @return Reference to the global UpnpPortMapper
     */
    static UpnpPortMapper& GetInstance();

    /**
     * @brief Deleted copy constructor
     */
    UpnpPortMapper(const UpnpPortMapper&) = delete;
    UpnpPortMapper& operator=(const UpnpPortMapper&) = delete;

    // =========================================================================
    // Discovery
    // =========================================================================

    /**
     * @brief Discover UPnP IGD on the network
     *
     * Performs SSDP discovery to find a UPnP-enabled router.
     * This is a blocking operation with a 2.5 second timeout.
     *
     * @return true if an IGD was found, false otherwise
     *
     * @note Thread-safe
     * @note Must be called before any port mapping operations
     */
    bool Discover();

    /**
     * @brief Check if UPnP is available
     *
     * @return true if Discover() succeeded and IGD is available
     */
    bool IsAvailable() const;

    // =========================================================================
    // Port Mapping
    // =========================================================================

    /**
     * @brief Add a TCP port mapping
     *
     * Opens a port on the router, forwarding external traffic to this device.
     *
     * @param internal_port Local port to forward to (host byte order)
     * @param external_port External port to open (host byte order)
     * @param description Human-readable description for the mapping
     * @param lease_duration Mapping duration in seconds (0 = permanent)
     * @return true if mapping was created, false on error
     *
     * @note Thread-safe
     * @note Requires Discover() to have succeeded
     */
    bool AddPortMapping(uint16_t internal_port, uint16_t external_port,
                        const char* description, int lease_duration = PORT_LEASE_DURATION);

    /**
     * @brief Delete a TCP port mapping
     *
     * Removes a previously created port mapping.
     *
     * @param external_port External port to close (host byte order)
     * @return true if mapping was deleted, false on error
     *
     * @note Thread-safe
     */
    bool DeletePortMapping(uint16_t external_port);

    /**
     * @brief Refresh an existing port mapping lease
     *
     * Should be called periodically (every PORT_LEASE_RENEW seconds)
     * to keep the mapping active.
     *
     * @param internal_port Local port
     * @param external_port External port
     * @param description Description (must match original)
     * @return true if lease was refreshed, false on error
     */
    bool RefreshPortMapping(uint16_t internal_port, uint16_t external_port,
                            const char* description);

    // =========================================================================
    // Information
    // =========================================================================

    /**
     * @brief Get the external (public) IP address
     *
     * @param ip_out Buffer to receive IP address string
     * @param ip_len Size of buffer (should be at least 16 bytes)
     * @return true if IP was retrieved, false on error
     *
     * @note Thread-safe
     */
    bool GetExternalIPAddress(char* ip_out, size_t ip_len);

    /**
     * @brief Get the local (LAN) IP address
     *
     * @param ip_out Buffer to receive IP address string
     * @param ip_len Size of buffer (should be at least 16 bytes)
     * @return true if IP was retrieved, false on error
     */
    bool GetLocalIPAddress(char* ip_out, size_t ip_len);

    /**
     * @brief Get the local IP as a 32-bit value
     *
     * @return Local IP in host byte order, or 0 if not available
     */
    uint32_t GetLocalIPv4() const;

    // =========================================================================
    // Cleanup
    // =========================================================================

    /**
     * @brief Release UPnP resources
     *
     * Frees internal UPnP structures. Called automatically on destruction
     * but can be called manually to release resources early.
     *
     * @note Thread-safe
     */
    void Cleanup();

private:
    /**
     * @brief Private constructor (singleton)
     */
    UpnpPortMapper();

    /**
     * @brief Destructor
     */
    ~UpnpPortMapper();

    /**
     * @brief Mutex for thread safety
     */
    mutable os::Mutex m_mutex{false};

    /**
     * @brief UPnP URLs structure (allocated dynamically)
     */
    UPNPUrls* m_urls;

    /**
     * @brief IGD data structure (allocated dynamically)
     */
    IGDdatas* m_data;

    /**
     * @brief Local IP address string
     */
    char m_lan_addr[64];

    /**
     * @brief Whether discovery succeeded
     */
    bool m_available;
};

} // namespace ams::mitm::p2p
