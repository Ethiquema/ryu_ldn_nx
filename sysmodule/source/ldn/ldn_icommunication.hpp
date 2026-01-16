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
#include <atomic>
#include "ldn_types.hpp"
#include "ldn_state_machine.hpp"
#include "ldn_node_mapper.hpp"
#include "ldn_proxy_buffer.hpp"
#include "ldn_network_timeout.hpp"
#include "interfaces/icommunication.hpp"
#include "../network/client.hpp"
#include "../p2p/p2p_proxy_client.hpp"
#include "../p2p/p2p_proxy_server.hpp"

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
     *
     * @param program_id Program ID of the client process (used to replace LocalCommunicationId=-1)
     */
    explicit ICommunicationService(ncm::ProgramId program_id);

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
    // Private Network Operations
    // ========================================================================

    /**
     * @brief Scan for private networks
     *
     * Same as Scan() but includes private networks in results.
     *
     * @param count Output number of networks found
     * @param buffer Output network info array
     * @param channel Channel to scan (0 for all)
     * @param filter Scan filter
     * @return Result code
     */
    Result ScanPrivate(
        ams::sf::Out<u32> count,
        ams::sf::OutAutoSelectArray<NetworkInfo> buffer,
        u16 channel,
        ScanFilter filter);

    /**
     * @brief Create a private (password-protected) network
     *
     * @param data Network configuration including security parameter
     * @param addressList Address list buffer
     * @return Result code
     */
    Result CreateNetworkPrivate(
        CreateNetworkPrivateConfig data,
        ams::sf::InPointerBuffer addressList);

    /**
     * @brief Connect to a private network
     *
     * @param data Connection data including security parameter
     * @return Result code
     */
    Result ConnectPrivate(ConnectPrivateData data);

    // ========================================================================
    // Other Operations
    // ========================================================================

    /**
     * @brief Set wireless controller restriction
     * @return Result code (stub)
     */
    Result SetWirelessControllerRestriction();

    /**
     * @brief Reject a node from the network
     *
     * @param nodeId Node ID to reject
     * @return Result code
     */
    Result Reject(u32 nodeId);

    /**
     * @brief Add entry to accept filter
     * @return Result code (stub)
     */
    Result AddAcceptFilterEntry();

    /**
     * @brief Clear accept filter
     * @return Result code (stub)
     */
    Result ClearAcceptFilter();

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
    /**
     * @brief Handle packet received from server
     * @param id Packet type
     * @param data Packet payload
     * @param size Payload size
     */
    void HandleServerPacket(ryu_ldn::protocol::PacketId id, const uint8_t* data, size_t size);

    /**
     * @brief Wait for a specific packet response from server
     * @param expected_id Expected packet type
     * @param timeout_ms Timeout in milliseconds
     * @return true if packet received, false on timeout
     */
    bool WaitForResponse(ryu_ldn::protocol::PacketId expected_id, uint64_t timeout_ms);

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

    // Response handling with events (like Ryujinx ManualResetEvent pattern)
    os::Event m_response_event;             ///< Signaled when any response received
    os::Event m_scan_event;                 ///< Signaled when scan completes
    os::Event m_error_event;                ///< Signaled on network error
    os::Event m_reject_event;               ///< Signaled when reject reply received
    ryu_ldn::protocol::PacketId m_last_response_id; ///< Last received packet ID

    // Scan results buffer
    static constexpr size_t MAX_SCAN_RESULTS = 24;  ///< Max networks from scan
    NetworkInfo m_scan_results[MAX_SCAN_RESULTS];   ///< Scan results buffer
    size_t m_scan_result_count;                     ///< Number of scan results

    // Advertise data buffer
    uint8_t m_advertise_data[384];          ///< Stored advertise data
    size_t m_advertise_data_size;           ///< Size of advertise data

    // Game version (like Ryujinx _gameVersion)
    uint8_t m_game_version[16];             ///< Game version string for CreateAccessPoint

    // Network connected flag (like Ryujinx _networkConnected)
    bool m_network_connected;               ///< True when in active network session

    // Last network error (like Ryujinx _lastError)
    ryu_ldn::protocol::NetworkErrorCode m_last_network_error; ///< Last error from server

    // P2P Proxy support (like Ryujinx _useP2pProxy, _connectedProxy, _hostedProxy, Config)
    bool m_use_p2p_proxy;                                   ///< True if P2P proxy enabled
    ryu_ldn::protocol::ProxyConfig m_proxy_config;          ///< Current proxy configuration
    ryu_ldn::protocol::ExternalProxyConfig m_external_proxy_config; ///< External proxy config
    p2p::P2pProxyClient* m_p2p_client;                      ///< Connected P2P proxy client (joiner side)
    p2p::P2pProxyServer* m_p2p_server;                      ///< Hosted P2P proxy server (host side)

    // Inactivity timeout (like Ryujinx _timeout)
    NetworkTimeout m_inactivity_timeout;                    ///< Auto-disconnect after idle period

    // Background thread for processing server pings between game operations
    os::ThreadType m_background_thread;                     ///< Background packet processing thread
    std::atomic<bool> m_background_thread_running;          ///< Thread running flag
    os::Mutex m_client_mutex;                               ///< Mutex for m_server_client access

    /**
     * @brief Background thread entry point
     * @param arg Pointer to ICommunicationService instance
     */
    static void BackgroundThreadEntry(void* arg);

    /**
     * @brief Background thread main loop - processes server pings
     */
    void BackgroundThreadFunc();

    // Program ID for LocalCommunicationId replacement (like Ryujinx NeedsRealId handling)
    ncm::ProgramId m_program_id;                            ///< Client program ID (title ID)
    u64 m_local_communication_id;                           ///< LocalCommunicationId from NACP (for LDN filtering)

    /**
     * @brief Load LocalCommunicationId from NACP
     *
     * Reads the application's NACP to get the first LocalCommunicationId.
     * This is the ID used by LDN for game filtering, which may differ from program_id.
     *
     * @return First LocalCommunicationId from NACP, or 0 on failure
     */
    u64 LoadLocalCommunicationIdFromNacp();

    /**
     * @brief Static callback for inactivity timeout
     *
     * Called when the timeout expires. Disconnects from server.
     */
    static void OnInactivityTimeout();

    /**
     * @brief Set game version from local_communication_version
     *
     * Converts version number to string format for RyuNetworkConfig.
     *
     * @param version Version buffer (16 bytes)
     */
    void SetGameVersion(const uint8_t* version);

    /**
     * @brief Consume last network error (like Ryujinx ConsumeNetworkError)
     *
     * Returns the last error and resets it to None.
     *
     * @return Last network error code
     */
    ryu_ldn::protocol::NetworkErrorCode ConsumeNetworkError();

    /**
     * @brief Handle ExternalProxy packet - connect to P2P host
     *
     * Called when server sends ExternalProxyConfig indicating a P2P host
     * is available. Creates P2pProxyClient and establishes direct connection.
     *
     * @param config ExternalProxyConfig from master server
     */
    void HandleExternalProxyConnect(const ryu_ldn::protocol::ExternalProxyConfig& config);

    /**
     * @brief Disconnect from P2P proxy if connected
     */
    void DisconnectP2pProxy();

    /**
     * @brief Start P2P proxy server for hosting
     *
     * Called when creating a network. Starts P2pProxyServer and attempts
     * UPnP NAT punch to allow direct P2P connections.
     *
     * @return true if server started (UPnP may or may not succeed)
     */
    bool StartP2pProxyServer();

    /**
     * @brief Stop P2P proxy server if running
     */
    void StopP2pProxyServer();

    /**
     * @brief Handle ExternalProxyToken from master server
     *
     * Called when master server notifies us a joiner is about to connect.
     * Adds token to waiting list for authentication.
     *
     * @param token Token for the expected joiner
     */
    void HandleExternalProxyToken(const ryu_ldn::protocol::ExternalProxyToken& token);

public:
    /**
     * @brief Send ProxyData to server (for BSD MITM callback)
     *
     * This method is called by the BSD MITM layer to send game socket data
     * through the LDN server connection.
     *
     * @param header ProxyData header with addressing info
     * @param data Packet payload
     * @param data_len Payload length
     * @return ClientOpResult indicating success or failure
     */
    ryu_ldn::network::ClientOpResult SendProxyDataToServer(
        const ryu_ldn::protocol::ProxyDataHeader& header,
        const void* data,
        size_t data_len);
};

// Verify interface compliance
static_assert(ams::mitm::ldn::IsICommunicationInterface<ICommunicationService>);

} // namespace ams::mitm::ldn
