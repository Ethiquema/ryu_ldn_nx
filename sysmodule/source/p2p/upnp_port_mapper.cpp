/**
 * @file upnp_port_mapper.cpp
 * @brief UPnP Port Mapper implementation using switch-miniupnpc
 *
 * This implementation wraps the miniupnpc library for Nintendo Switch.
 * The library is provided by devkitPro as `switch-miniupnpc`.
 *
 * ## UPnP Protocol Overview
 *
 * UPnP IGD (Internet Gateway Device) allows automatic NAT traversal:
 *
 * 1. **Discovery (SSDP)**: Multicast query to 239.255.255.250:1900
 *    to find UPnP-capable routers on the network.
 *
 * 2. **Description**: HTTP GET to retrieve device capabilities XML
 *    and find the WANIPConnection service.
 *
 * 3. **Control (SOAP)**: HTTP POST with SOAP envelope to add/delete
 *    port mappings.
 *
 * ## Ryujinx Compatibility
 *
 * This implementation matches Ryujinx's Open.NAT behavior:
 * - Discovery timeout: 2500ms
 * - Port range: 39990-39999 (TCP)
 * - Lease duration: 60 seconds
 * - Lease renewal: every 50 seconds
 *
 * ## Error Handling
 *
 * miniupnpc functions return:
 * - UPNPCOMMAND_SUCCESS (0) on success
 * - Negative values on error (see upnperrors.h)
 *
 * Common errors:
 * - 402: Invalid arguments
 * - 501: Action failed
 * - 714: No such entry (for delete)
 * - 718: Conflict (port already mapped by another host)
 * - 725: Only permanent lease supported
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "upnp_port_mapper.hpp"

#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>

#include <cstring>
#include <cstdio>

namespace ams::mitm::p2p {

// =============================================================================
// Singleton Access
// =============================================================================

UpnpPortMapper& UpnpPortMapper::GetInstance() {
    // Meyer's singleton - thread-safe in C++11 and later
    static UpnpPortMapper instance;
    return instance;
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

UpnpPortMapper::UpnpPortMapper()
    : m_urls(nullptr)
    , m_data(nullptr)
    , m_lan_addr{}
    , m_available(false)
{
    // Allocate UPnP structures on the heap
    // These are C structs from miniupnpc, we manage their lifetime
    m_urls = new UPNPUrls();
    m_data = new IGDdatas();

    // Zero-initialize (required before first use)
    std::memset(m_urls, 0, sizeof(UPNPUrls));
    std::memset(m_data, 0, sizeof(IGDdatas));
}

UpnpPortMapper::~UpnpPortMapper() {
    // Release UPnP resources
    Cleanup();

    // Free allocated structures
    delete m_urls;
    delete m_data;

    m_urls = nullptr;
    m_data = nullptr;
}

// =============================================================================
// Discovery
// =============================================================================

bool UpnpPortMapper::Discover() {
    std::scoped_lock lock(m_mutex);

    // Already discovered - return cached result
    if (m_available) {
        return true;
    }

    // Clean up any previous failed discovery attempt
    if (m_urls->controlURL) {
        FreeUPNPUrls(m_urls);
        std::memset(m_urls, 0, sizeof(UPNPUrls));
        std::memset(m_data, 0, sizeof(IGDdatas));
    }

    int error = 0;

    // ==========================================================================
    // Step 1: SSDP Discovery
    // ==========================================================================
    // Send M-SEARCH to 239.255.255.250:1900 to find UPnP devices
    //
    // Parameters:
    //   delay_ms       - Timeout for discovery (2500ms like Ryujinx)
    //   multicast_if   - Network interface for multicast (NULL = auto)
    //   minissdpsock   - Path to MiniSSDPd socket (NULL = not used)
    //   localport      - Local port for responses (0 = auto)
    //   ipv6           - Enable IPv6 (0 = disabled, Switch uses IPv4)
    //   ttl            - Time-to-live for multicast (2 = typical)
    //   error          - Output error code
    //
    UPNPDev* devlist = upnpDiscover(
        UPNP_DISCOVERY_TIMEOUT_MS,  // 2500ms timeout
        nullptr,                     // Auto-select interface
        nullptr,                     // No MiniSSDPd
        0,                           // Auto port
        0,                           // IPv4 only
        2,                           // TTL = 2
        &error
    );

    if (devlist == nullptr) {
        // No UPnP devices found on the network
        // This is normal if the router doesn't support UPnP
        return false;
    }

    // ==========================================================================
    // Step 2: Find Valid IGD (Internet Gateway Device)
    // ==========================================================================
    // Iterate through discovered devices and find one that:
    //   - Is an IGD (has WANIPConnection or WANPPPConnection service)
    //   - Is connected to the internet
    //
    // Also retrieves:
    //   - Control URLs for SOAP requests
    //   - Service type identifiers
    //   - Local IP address
    //
    int result = UPNP_GetValidIGD(
        devlist,
        m_urls,              // Output: URLs for control
        m_data,              // Output: IGD service data
        m_lan_addr,          // Output: Our local IP address
        sizeof(m_lan_addr)
    );

    // Free the device list (we've extracted what we need)
    freeUPNPDevlist(devlist);

    // ==========================================================================
    // Interpret Result
    // ==========================================================================
    // Return values from UPNP_GetValidIGD:
    //   0 = No IGD found
    //   1 = Valid connected IGD found (ideal)
    //   2 = Valid IGD found but not connected (may still work)
    //   3 = UPnP device found but not an IGD
    //
    if (result == 1 || result == 2) {
        m_available = true;
        return true;
    }

    // Failed to find usable IGD
    return false;
}

bool UpnpPortMapper::IsAvailable() const {
    std::scoped_lock lock(m_mutex);
    return m_available;
}

// =============================================================================
// Port Mapping Operations
// =============================================================================

bool UpnpPortMapper::AddPortMapping(uint16_t internal_port, uint16_t external_port,
                                     const char* description, int lease_duration) {
    std::scoped_lock lock(m_mutex);

    // Verify we have a valid IGD
    if (!m_available || m_urls->controlURL == nullptr) {
        return false;
    }

    // ==========================================================================
    // Prepare Parameters
    // ==========================================================================
    // miniupnpc uses string parameters for all values
    //
    char internal_port_str[8];
    char external_port_str[8];
    char lease_str[16];

    std::snprintf(internal_port_str, sizeof(internal_port_str), "%u", internal_port);
    std::snprintf(external_port_str, sizeof(external_port_str), "%u", external_port);
    std::snprintf(lease_str, sizeof(lease_str), "%d", lease_duration);

    // ==========================================================================
    // Send SOAP Request: AddPortMapping
    // ==========================================================================
    // This sends an HTTP POST to the router's control URL with a SOAP envelope:
    //
    // <NewRemoteHost></NewRemoteHost>           - Empty = any remote host
    // <NewExternalPort>39990</NewExternalPort>  - Public port
    // <NewProtocol>TCP</NewProtocol>            - Protocol
    // <NewInternalPort>39990</NewInternalPort>  - Local port
    // <NewInternalClient>192.168.1.x</NewInternalClient> - Our IP
    // <NewEnabled>1</NewEnabled>                - Enable mapping
    // <NewPortMappingDescription>...</NewPortMappingDescription>
    // <NewLeaseDuration>60</NewLeaseDuration>   - Seconds (0 = permanent)
    //
    int result = UPNP_AddPortMapping(
        m_urls->controlURL,         // Router control URL
        m_data->first.servicetype,  // WANIPConnection or WANPPPConnection
        external_port_str,          // External port (public)
        internal_port_str,          // Internal port (our port)
        m_lan_addr,                 // Internal client (our IP)
        description,                // Human-readable description
        "TCP",                      // Protocol (TCP for P2P server)
        nullptr,                    // Remote host (NULL = any)
        lease_str                   // Lease duration in seconds
    );

    // UPNPCOMMAND_SUCCESS = 0
    return result == UPNPCOMMAND_SUCCESS;
}

bool UpnpPortMapper::DeletePortMapping(uint16_t external_port) {
    std::scoped_lock lock(m_mutex);

    if (!m_available || m_urls->controlURL == nullptr) {
        return false;
    }

    // ==========================================================================
    // Send SOAP Request: DeletePortMapping
    // ==========================================================================
    // Removes a previously created port mapping
    //
    char external_port_str[8];
    std::snprintf(external_port_str, sizeof(external_port_str), "%u", external_port);

    int result = UPNP_DeletePortMapping(
        m_urls->controlURL,
        m_data->first.servicetype,
        external_port_str,  // External port to remove
        "TCP",              // Protocol
        nullptr             // Remote host (NULL = any, must match Add)
    );

    // Note: Returns 714 ("NoSuchEntryInArray") if mapping doesn't exist
    // We consider this success since the goal is "no mapping exists"
    return result == UPNPCOMMAND_SUCCESS || result == 714;
}

bool UpnpPortMapper::RefreshPortMapping(uint16_t internal_port, uint16_t external_port,
                                         const char* description) {
    // ==========================================================================
    // Lease Renewal Strategy
    // ==========================================================================
    // UPnP doesn't have a dedicated "refresh" action.
    // The standard approach is to re-add the mapping with the same parameters.
    // Most routers will:
    //   - Update the lease if we own the mapping
    //   - Return 718 (ConflictInMappingEntry) if another host owns it
    //
    // Ryujinx calls this every 50 seconds (PORT_LEASE_RENEW) to maintain
    // the 60-second lease (PORT_LEASE_DURATION).
    //
    return AddPortMapping(internal_port, external_port, description, PORT_LEASE_DURATION);
}

// =============================================================================
// Information Retrieval
// =============================================================================

bool UpnpPortMapper::GetExternalIPAddress(char* ip_out, size_t ip_len) {
    std::scoped_lock lock(m_mutex);

    if (!m_available || m_urls->controlURL == nullptr || ip_out == nullptr || ip_len == 0) {
        return false;
    }

    // ==========================================================================
    // Send SOAP Request: GetExternalIPAddress
    // ==========================================================================
    // Returns the router's public IP address
    // This is the IP that remote peers will use to connect to us
    //
    char external_ip[16] = {0};

    int result = UPNP_GetExternalIPAddress(
        m_urls->controlURL,
        m_data->first.servicetype,
        external_ip
    );

    if (result != UPNPCOMMAND_SUCCESS) {
        return false;
    }

    // Copy to output buffer with null termination
    std::strncpy(ip_out, external_ip, ip_len - 1);
    ip_out[ip_len - 1] = '\0';

    return true;
}

bool UpnpPortMapper::GetLocalIPAddress(char* ip_out, size_t ip_len) {
    std::scoped_lock lock(m_mutex);

    if (!m_available || ip_out == nullptr || ip_len == 0) {
        return false;
    }

    // m_lan_addr was populated during UPNP_GetValidIGD
    // This is our IP on the local network (e.g., 192.168.1.100)
    std::strncpy(ip_out, m_lan_addr, ip_len - 1);
    ip_out[ip_len - 1] = '\0';

    return true;
}

uint32_t UpnpPortMapper::GetLocalIPv4() const {
    std::scoped_lock lock(m_mutex);

    if (!m_available || m_lan_addr[0] == '\0') {
        return 0;
    }

    // ==========================================================================
    // Parse IPv4 String to uint32_t
    // ==========================================================================
    // Convert "192.168.1.100" to 0xC0A80164 (host byte order)
    //
    unsigned int a, b, c, d;
    if (std::sscanf(m_lan_addr, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return 0;
    }

    // Validate octets
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        return 0;
    }

    // Return in host byte order (most significant byte first)
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(c) << 8)  |
            static_cast<uint32_t>(d);
}

// =============================================================================
// Cleanup
// =============================================================================

void UpnpPortMapper::Cleanup() {
    std::scoped_lock lock(m_mutex);

    // ==========================================================================
    // Release miniupnpc Resources
    // ==========================================================================
    // FreeUPNPUrls frees all strings allocated in the UPNPUrls structure
    // (controlURL, rootdescURL, etc.)
    //
    if (m_urls && m_urls->controlURL) {
        FreeUPNPUrls(m_urls);
        std::memset(m_urls, 0, sizeof(UPNPUrls));
    }

    if (m_data) {
        std::memset(m_data, 0, sizeof(IGDdatas));
    }

    m_lan_addr[0] = '\0';
    m_available = false;
}

} // namespace ams::mitm::p2p
