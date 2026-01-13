/**
 * @file ldn_icommunication.hpp
 * @brief LDN Communication Service implementation header
 *
 * This service handles all the LDN IPC commands and communicates with
 * the RyuLdn server via our TCP client.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include "ldn_types.hpp"
#include "ldn_state_machine.hpp"
#include "ldn_node_mapper.hpp"
#include "ldn_proxy_buffer.hpp"
#include "interfaces/icommunication.hpp"
#include "../network/client.hpp"

namespace ams::mitm::ldn {

/**
 * @brief LDN Communication Service implementation
 *
 * This class implements the IUserLocalCommunicationService interface,
 * handling all LDN operations and forwarding them to our RyuLdn server
 * via the network client.
 *
 * ## State Management
 *
 * The service maintains an LDN state machine:
 * - None → Initialize → Initialized
 * - Initialized → OpenAccessPoint → AccessPoint
 * - AccessPoint → CreateNetwork → AccessPointCreated
 * - Initialized → OpenStation → Station
 * - Station → Connect → StationConnected
 *
 * State changes are notified via the state change event.
 */
class ICommunicationService {
public:
    /**
     * @brief Constructor
     */
    ICommunicationService();

    /**
     * @brief Destructor
     */
    ~ICommunicationService();

    // ========================================================================
    // Query Operations
    // ========================================================================

    /**
     * @brief Get current communication state
     *
     * @param state Output state value
     * @return Result code
     */
    Result GetState(ams::sf::Out<u32> state);

    /**
     * @brief Get current network information
     *
     * @param buffer Output network info
     * @return Result code
     */
    Result GetNetworkInfo(ams::sf::Out<NetworkInfo> buffer);

    /**
     * @brief Get assigned IPv4 address
     *
     * @param address Output IP address (host byte order)
     * @param mask Output subnet mask (host byte order)
     * @return Result code
     */
    Result GetIpv4Address(ams::sf::Out<u32> address, ams::sf::Out<u32> mask);

    /**
     * @brief Get last disconnect reason
     *
     * @param reason Output disconnect reason
     * @return Result code
     */
    Result GetDisconnectReason(ams::sf::Out<u32> reason);

    /**
     * @brief Get security parameters
     *
     * @param out Output security parameter
     * @return Result code
     */
    Result GetSecurityParameter(ams::sf::Out<SecurityParameter> out);

    /**
     * @brief Get network configuration
     *
     * @param out Output network config
     * @return Result code
     */
    Result GetNetworkConfig(ams::sf::Out<NetworkConfig> out);

    /**
     * @brief Attach to state change event
     *
     * @param handle Output event handle
     * @return Result code
     */
    Result AttachStateChangeEvent(ams::sf::Out<ams::sf::CopyHandle> handle);

    /**
     * @brief Get network info with node updates
     *
     * @param buffer Output network info
     * @param pUpdates Output node update array
     * @return Result code
     */
    Result GetNetworkInfoLatestUpdate(
        ams::sf::Out<NetworkInfo> buffer,
        ams::sf::OutArray<NodeLatestUpdate> pUpdates);

    /**
     * @brief Scan for available networks
     *
     * @param count Output number of networks found
     * @param buffer Output network info array
     * @param channel Channel to scan (0 for all)
     * @param filter Scan filter
     * @return Result code
     */
    Result Scan(
        ams::sf::Out<u32> count,
        ams::sf::OutAutoSelectArray<NetworkInfo> buffer,
        u16 channel,
        ScanFilter filter);

    // ========================================================================
    // Access Point Operations
    // ========================================================================

    /**
     * @brief Open as access point (host mode)
     *
     * @return Result code
     */
    Result OpenAccessPoint();

    /**
     * @brief Close access point
     *
     * @return Result code
     */
    Result CloseAccessPoint();

    /**
     * @brief Create a network
     *
     * @param data Network creation configuration
     * @return Result code
     */
    Result CreateNetwork(CreateNetworkConfig data);

    /**
     * @brief Destroy the network
     *
     * @return Result code
     */
    Result DestroyNetwork();

    /**
     * @brief Set advertise data
     *
     * @param data Advertise data buffer
     * @return Result code
     */
    Result SetAdvertiseData(ams::sf::InAutoSelectBuffer data);

    /**
     * @brief Set station accept policy
     *
     * @param policy Accept policy
     * @return Result code
     */
    Result SetStationAcceptPolicy(u8 policy);

    // ========================================================================
    // Station Operations
    // ========================================================================

    /**
     * @brief Open as station (client mode)
     *
     * @return Result code
     */
    Result OpenStation();

    /**
     * @brief Close station
     *
     * @return Result code
     */
    Result CloseStation();

    /**
     * @brief Connect to a network
     *
     * @param dat Connection data
     * @param data Network to connect to
     * @return Result code
     */
    Result Connect(ConnectNetworkData dat, const NetworkInfo& data);

    /**
     * @brief Disconnect from network
     *
     * @return Result code
     */
    Result Disconnect();

    // ========================================================================
    // Lifecycle Operations
    // ========================================================================

    /**
     * @brief Initialize the service
     *
     * @param client_process_id Client process ID
     * @return Result code
     */
    Result Initialize(const ams::sf::ClientProcessId& client_process_id);

    /**
     * @brief Finalize the service
     *
     * @return Result code
     */
    Result Finalize();

    /**
     * @brief Initialize with system flags
     *
     * @param unk Unknown parameter
     * @param client_process_id Client process ID
     * @return Result code
     */
    Result InitializeSystem2(u64 unk, const ams::sf::ClientProcessId& client_process_id);

    // ========================================================================
    // Stub Operations (not implemented)
    // ========================================================================

    Result ScanPrivate();
    Result SetWirelessControllerRestriction();
    Result CreateNetworkPrivate();
    Result Reject();
    Result AddAcceptFilterEntry();
    Result ClearAcceptFilter();
    Result ConnectPrivate();

private:
    /**
     * @brief Connect to RyuLdn server
     * @return Result code
     */
    Result ConnectToServer();

    /**
     * @brief Disconnect from RyuLdn server
     */
    void DisconnectFromServer();

    /**
     * @brief Check if connected to server
     * @return true if connected and ready
     */
    bool IsServerConnected() const;

private:
    LdnStateMachine m_state_machine;        ///< Thread-safe state machine
    u64 m_error_state;                      ///< Error state flag
    u64 m_client_process_id;                ///< Client game process ID

    NetworkInfo m_network_info;             ///< Current network info
    DisconnectReason m_disconnect_reason;   ///< Last disconnect reason
    u32 m_ipv4_address;                     ///< Assigned IPv4 address
    u32 m_subnet_mask;                      ///< Subnet mask

    ryu_ldn::network::RyuLdnClient m_server_client; ///< Server communication client
    bool m_server_connected;                ///< Server connection status

    LdnNodeMapper m_node_mapper;            ///< Node ID to IP mapping
    LdnProxyBuffer m_proxy_buffer;          ///< Incoming proxy data buffer
};

// Verify interface compliance
static_assert(ams::mitm::ldn::IsICommunicationInterface<ICommunicationService>);

} // namespace ams::mitm::ldn
