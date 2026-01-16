/**
 * @file ldn_icommunication.cpp
 * @brief LDN Communication Service implementation
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ldn_icommunication.hpp"
#include "ldn_shared_state.hpp"
#include "../config/config_ipc_service.hpp"
#include "../debug/log.hpp"
#include "../bsd/proxy_socket_manager.hpp"
#include <arpa/inet.h>

namespace ams::mitm::ldn {

// =============================================================================
// Static State for BSD MITM Integration
// =============================================================================

/**
 * @brief Pointer to active ICommunicationService instance for BSD MITM callback
 *
 * The BSD MITM needs to send ProxyData through the LDN server connection.
 * This static pointer provides access to the active service's client.
 * Set during ConnectToServer, cleared during DisconnectFromServer.
 */
static ICommunicationService* g_active_ldn_service = nullptr;
static os::Mutex g_active_service_mutex{false};

/**
 * @brief Static callback for inactivity timeout
 *
 * Called when the NetworkTimeout expires (no network activity for 6 seconds).
 * Disconnects from the server to save resources.
 * Like Ryujinx _timeout callback that calls DisconnectInternal().
 */
void ICommunicationService::OnInactivityTimeout() {
    std::scoped_lock lock(g_active_service_mutex);

    if (g_active_ldn_service != nullptr && !g_active_ldn_service->m_network_connected) {
        LOG_INFO("Inactivity timeout - disconnecting from server");
        g_active_ldn_service->DisconnectFromServer();
    }
}

/**
 * @brief Callback for BSD MITM to send ProxyData through LDN server
 *
 * This function is registered with ProxySocketManager and called when
 * proxy sockets need to send data.
 *
 * @param source_ip Source IP (host byte order)
 * @param source_port Source port (host byte order)
 * @param dest_ip Destination IP (host byte order)
 * @param dest_port Destination port (host byte order)
 * @param protocol Protocol type (TCP/UDP)
 * @param data Packet payload
 * @param data_len Payload length
 * @return true if sent successfully
 */
static bool SendProxyDataCallback(uint32_t source_ip, uint16_t source_port,
                                   uint32_t dest_ip, uint16_t dest_port,
                                   ryu_ldn::bsd::ProtocolType protocol,
                                   const void* data, size_t data_len) {
    std::scoped_lock lock(g_active_service_mutex);

    if (g_active_ldn_service == nullptr) {
        return false;
    }

    // Build ProxyDataHeader
    ryu_ldn::protocol::ProxyDataHeader header{};
    header.info.source_ipv4 = source_ip;
    header.info.source_port = source_port;
    header.info.dest_ipv4 = dest_ip;
    header.info.dest_port = dest_port;

    // Convert BSD protocol type to protocol type
    switch (protocol) {
        case ryu_ldn::bsd::ProtocolType::Tcp:
            header.info.protocol = ryu_ldn::protocol::ProtocolType::Tcp;
            break;
        case ryu_ldn::bsd::ProtocolType::Udp:
            header.info.protocol = ryu_ldn::protocol::ProtocolType::Udp;
            break;
        default:
            return false;
    }

    header.data_length = static_cast<uint32_t>(data_len);

    // Send via the LDN client
    auto result = g_active_ldn_service->SendProxyDataToServer(header, data, data_len);
    return result == ryu_ldn::network::ClientOpResult::Success;
}

// Verify struct sizes match Nintendo's expectations
static_assert(sizeof(NetworkInfo) == 0x480, "sizeof(NetworkInfo) should be 0x480");
static_assert(sizeof(ConnectNetworkData) == 0x7C, "sizeof(ConnectNetworkData) should be 0x7C");
static_assert(sizeof(ScanFilter) == 0x60, "sizeof(ScanFilter) should be 0x60");

ICommunicationService::ICommunicationService()
    : m_state_machine()
    , m_error_state(0)
    , m_client_process_id(0)
    , m_network_info{}
    , m_disconnect_reason(DisconnectReason::None)
    , m_ipv4_address(0)
    , m_subnet_mask(0)
    , m_server_client(ryu_ldn::network::RyuLdnClientConfig(ryu_ldn::ipc::g_config))
    , m_server_connected(false)
    , m_node_mapper()
    , m_proxy_buffer()
    , m_response_event(os::EventClearMode_ManualClear)
    , m_scan_event(os::EventClearMode_ManualClear)
    , m_error_event(os::EventClearMode_ManualClear)
    , m_reject_event(os::EventClearMode_ManualClear)
    , m_last_response_id(ryu_ldn::protocol::PacketId::Initialize)
    , m_scan_results{}
    , m_scan_result_count(0)
    , m_advertise_data{}
    , m_advertise_data_size(0)
    , m_game_version{}
    , m_network_connected(false)
    , m_last_network_error(ryu_ldn::protocol::NetworkErrorCode::None)
    , m_use_p2p_proxy(!ryu_ldn::ipc::g_config.ldn.disable_p2p)
    , m_proxy_config{}
    , m_external_proxy_config{}
    , m_p2p_client(nullptr)
    , m_p2p_server(nullptr)
    , m_inactivity_timeout(NetworkTimeout::DEFAULT_IDLE_TIMEOUT_MS, &ICommunicationService::OnInactivityTimeout)
{
    // Configure packet callback to receive server responses
    // Use static callback with user_data to route to instance method
    m_server_client.set_packet_callback(
        [](ryu_ldn::protocol::PacketId id, const uint8_t* data, size_t size, void* user_data) {
            auto* self = static_cast<ICommunicationService*>(user_data);
            self->HandleServerPacket(id, data, size);
        },
        this
    );
}

ICommunicationService::~ICommunicationService() {
    LOG_INFO("ICommunicationService destructor called (state=%s)",
             LdnStateMachine::StateToString(m_state_machine.GetState()));
    // Stop P2P server if hosting
    StopP2pProxyServer();
    // Ensure P2P proxy client is disconnected
    DisconnectP2pProxy();
    // Ensure server is disconnected
    DisconnectFromServer();
}

// ============================================================================
// Server Connection Helpers
// ============================================================================

Result ICommunicationService::ConnectToServer() {
    if (m_server_connected) {
        LOG_VERBOSE("Already connected to server");
        R_SUCCEED();
    }

    LOG_INFO("Connecting to RyuLdn server...");

    // Attempt TCP connection
    auto result = m_server_client.connect();
    if (result != ryu_ldn::network::ClientOpResult::Success) {
        LOG_ERROR("Server connection failed: %s",
                  ryu_ldn::network::client_op_result_to_string(result));
        R_RETURN(MAKERESULT(0x10, 2)); // Connection failed
    }

    // Wait for handshake to complete (with timeout)
    constexpr uint64_t handshake_timeout_ms = 5000;
    constexpr uint64_t poll_interval_ms = 50;

    LOG_VERBOSE("Waiting for handshake...");

    uint64_t start_time_ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;
    uint64_t current_time_ms = start_time_ms;

    while (!m_server_client.is_ready() && (current_time_ms - start_time_ms) < handshake_timeout_ms) {
        // Process client state machine (sends handshake, receives response)
        m_server_client.update(current_time_ms);

        // Check if connection failed during handshake
        if (!m_server_client.is_connected()) {
            LOG_ERROR("Connection lost during handshake");
            R_RETURN(MAKERESULT(0x10, 3)); // Handshake failed
        }

        // Small delay to avoid busy-waiting
        svcSleepThread(poll_interval_ms * 1000000ULL); // Convert ms to ns
        current_time_ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;
    }

    if (!m_server_client.is_ready()) {
        LOG_ERROR("Handshake timeout");
        m_server_client.disconnect();
        R_RETURN(MAKERESULT(0x10, 4)); // Handshake timeout
    }

    m_server_connected = true;

    // Register this service for BSD MITM callback
    {
        std::scoped_lock lock(g_active_service_mutex);
        g_active_ldn_service = this;
    }

    // Register the send callback with ProxySocketManager
    mitm::bsd::ProxySocketManager::GetInstance().SetSendCallback(SendProxyDataCallback);

    LOG_INFO("Connected to RyuLdn server successfully");
    R_SUCCEED();
}

void ICommunicationService::DisconnectFromServer() {
    // Disconnect P2P proxy first if connected
    DisconnectP2pProxy();

    if (m_server_connected) {
        LOG_INFO("Disconnecting from RyuLdn server");

        // Unregister BSD MITM callback
        {
            std::scoped_lock lock(g_active_service_mutex);
            if (g_active_ldn_service == this) {
                g_active_ldn_service = nullptr;
            }
        }

        // Clear the send callback
        mitm::bsd::ProxySocketManager::GetInstance().SetSendCallback(nullptr);

        m_server_client.disconnect();
        m_server_connected = false;
    }
}

bool ICommunicationService::IsServerConnected() const {
    return m_server_connected && m_server_client.is_ready();
}

// ============================================================================
// Lifecycle Operations
// ============================================================================

Result ICommunicationService::Initialize(const ams::sf::ClientProcessId& client_process_id) {
    // Store client process ID for tracking
    m_client_process_id = client_process_id.GetValue().value;
    LOG_INFO("LDN Initialize called by process 0x%lx", m_client_process_id);

    // Transition to Initialized state
    auto result = m_state_machine.Initialize();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Reset disconnect reason on fresh initialization
    m_disconnect_reason = DisconnectReason::None;

    // Update shared state for overlay
    auto& shared_state = SharedState::GetInstance();
    shared_state.SetGameActive(true, m_client_process_id);
    shared_state.SetLdnState(static_cast<ams::mitm::ldn::CommState>(m_state_machine.GetState()));

    LOG_VERBOSE("LDN Initialized successfully");
    R_SUCCEED();
}

Result ICommunicationService::InitializeSystem2(u64 unk, const ams::sf::ClientProcessId& client_process_id) {
    m_error_state = unk;
    return Initialize(client_process_id);
}

Result ICommunicationService::Finalize() {
    LOG_INFO("Finalize() called");
    // Disconnect from RyuLdn server if connected
    DisconnectFromServer();

    // Transition back to None state
    m_state_machine.Finalize();

    // Update shared state - game is no longer active
    auto& shared_state = SharedState::GetInstance();
    shared_state.SetGameActive(false, 0);

    // Clear client info
    m_client_process_id = 0;
    m_error_state = 0;

    // Clear network state
    std::memset(&m_network_info, 0, sizeof(m_network_info));
    m_ipv4_address = 0;
    m_subnet_mask = 0;

    R_SUCCEED();
}

// ============================================================================
// Query Operations
// ============================================================================

Result ICommunicationService::GetState(ams::sf::Out<u32> state) {
    // Process incoming packets (like pings) to keep connection alive
    // This is critical because the server expects ping responses within ~6 seconds
    if (m_server_connected && m_server_client.is_connected()) {
        uint64_t current_time_ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;
        m_server_client.update(current_time_ms);
    }

    auto current_state = m_state_machine.GetState();
    LOG_INFO("GetState() called, returning state=%u (%s)",
             static_cast<u32>(current_state), LdnStateMachine::StateToString(current_state));
    state.SetValue(static_cast<u32>(current_state));

    // If error_state is set and we have a disconnect reason, return error
    if (m_error_state != 0) {
        if (m_disconnect_reason != DisconnectReason::None) {
            R_RETURN(MAKERESULT(0x10, static_cast<u32>(m_disconnect_reason)));
        }
    }

    R_SUCCEED();
}

Result ICommunicationService::GetNetworkInfo(ams::sf::Out<NetworkInfo> buffer) {
    // Process incoming packets (like pings) to keep connection alive
    if (m_server_connected && m_server_client.is_connected()) {
        uint64_t current_time_ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;
        m_server_client.update(current_time_ms);
    }

    LOG_VERBOSE("GetNetworkInfo() called, node_count=%u, max=%u",
                m_network_info.ldn.nodeCount, m_network_info.ldn.nodeCountMax);
    buffer.SetValue(m_network_info);
    R_SUCCEED();
}

Result ICommunicationService::GetIpv4Address(ams::sf::Out<u32> address, ams::sf::Out<u32> mask) {
    // If connected to RyuLdn server and we have a proxy config, return the virtual IP
    // This is critical for LDN communication - the game needs to use the proxy IP
    if (m_server_connected && m_proxy_config.proxy_ip != 0) {
        // Return the virtual IP assigned by the server (like Ryujinx LdnProxy)
        // proxy_ip is already in host byte order (e.g., 0x0A720001 = 10.114.0.1)
        address.SetValue(m_proxy_config.proxy_ip);
        mask.SetValue(m_proxy_config.proxy_subnet_mask);
        LOG_VERBOSE("GetIpv4Address: returning proxy IP 0x%08X, mask 0x%08X",
                    m_proxy_config.proxy_ip, m_proxy_config.proxy_subnet_mask);
        R_SUCCEED();
    }

    // Fallback: Get current IP from nifm service
    u32 addr, netmask, gateway, primary_dns, secondary_dns;
    Result rc = nifmGetCurrentIpConfigInfo(&addr, &netmask, &gateway, &primary_dns, &secondary_dns);

    if (R_SUCCEEDED(rc)) {
        // Convert from network byte order to host byte order
        address.SetValue(ntohl(addr));
        mask.SetValue(ntohl(netmask));
    }

    R_RETURN(rc);
}

Result ICommunicationService::GetDisconnectReason(ams::sf::Out<u32> reason) {
    reason.SetValue(static_cast<u32>(m_disconnect_reason));
    R_SUCCEED();
}

Result ICommunicationService::GetSecurityParameter(ams::sf::Out<SecurityParameter> out) {
    SecurityParameter param;
    NetworkInfo2SecurityParameter(&m_network_info, &param);
    out.SetValue(param);
    R_SUCCEED();
}

Result ICommunicationService::GetNetworkConfig(ams::sf::Out<NetworkConfig> out) {
    NetworkConfig config;
    NetworkInfo2NetworkConfig(&m_network_info, &config);
    out.SetValue(config);
    R_SUCCEED();
}

Result ICommunicationService::AttachStateChangeEvent(ams::sf::Out<ams::sf::CopyHandle> handle) {
    handle.SetValue(m_state_machine.GetStateChangeEventHandle(), false);
    R_SUCCEED();
}

Result ICommunicationService::GetNetworkInfoLatestUpdate(
    ams::sf::Out<NetworkInfo> buffer,
    ams::sf::OutArray<NodeLatestUpdate> pUpdates)
{
    buffer.SetValue(m_network_info);

    // Clear updates - no changes to report yet
    // TODO: Track node changes and report them here
    if (pUpdates.GetSize() > 0) {
        std::memset(pUpdates.GetPointer(), 0, pUpdates.GetSize() * sizeof(NodeLatestUpdate));
    }

    R_SUCCEED();
}

Result ICommunicationService::Scan(
    ams::sf::Out<u32> count,
    ams::sf::OutAutoSelectArray<NetworkInfo> buffer,
    u16 channel,
    ScanFilter filter)
{
    AMS_UNUSED(channel);

    LOG_INFO("Scan() called, local_comm_id=0x%lx", filter.networkId.intentId.localCommunicationId);

    R_UNLESS(IsServerConnected(), MAKERESULT(0x10, 2)); // Not connected

    // Reset scan results buffer and events (like Ryujinx _availableGames.Clear() and _scan.Reset())
    m_scan_result_count = 0;
    std::memset(m_scan_results, 0, sizeof(m_scan_results));
    m_scan_event.Clear();
    m_error_event.Clear();

    // Build scan filter for server
    // Convert from ams::mitm::ldn::ScanFilter to ryu_ldn::protocol::ScanFilterFull
    ryu_ldn::protocol::ScanFilterFull scan_filter{};
    scan_filter.flag = filter.flag;
    scan_filter.network_type = static_cast<uint8_t>(filter.networkType);

    // Copy network ID
    scan_filter.network_id.intent_id.local_communication_id = filter.networkId.intentId.localCommunicationId;
    scan_filter.network_id.intent_id.scene_id = filter.networkId.intentId.sceneId;
    // SessionId is stored as a 16-byte blob (high + low as two u64)
    std::memcpy(scan_filter.network_id.session_id.data, &filter.networkId.sessionId, 16);

    // Copy SSID
    scan_filter.ssid.length = filter.ssid.length;
    std::memcpy(scan_filter.ssid.name, filter.ssid.raw, sizeof(scan_filter.ssid.name));

    // Copy MAC address (BSSID)
    std::memcpy(scan_filter.mac_address.data, filter.bssid.raw, sizeof(scan_filter.mac_address.data));

    // Send scan request
    auto send_result = m_server_client.send_scan(scan_filter);
    if (send_result != ryu_ldn::network::ClientOpResult::Success) {
        LOG_ERROR("Scan: send failed");
        count.SetValue(0);
        R_RETURN(MAKERESULT(0x10, 3)); // Send failed
    }

    LOG_INFO("Scan: sent request, waiting for ScanReplyEnd...");

    // Wait for scan results with polling for network updates
    // Unlike Ryujinx which has async receive, we need to call update() to process incoming data
    constexpr uint64_t scan_timeout_ms = 1000;
    uint64_t start_time_ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;
    uint64_t current_time_ms = start_time_ms;
    bool scan_complete = false;
    bool error_received = false;

    while ((current_time_ms - start_time_ms) < scan_timeout_ms) {
        // Process incoming packets - this is required because we don't have async receive
        m_server_client.update(current_time_ms);

        // Check if scan completed or error received
        if (m_scan_event.TryWait()) {
            scan_complete = true;
            break;
        }
        if (m_error_event.TryWait()) {
            error_received = true;
            break;
        }

        // Check if connection was lost
        if (!m_server_client.is_connected()) {
            LOG_ERROR("Scan: connection lost");
            count.SetValue(0);
            R_RETURN(MAKERESULT(0x10, 4));
        }

        // Short sleep to avoid busy-waiting (but still responsive)
        svcSleepThread(5 * 1000000ULL); // 5ms
        current_time_ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;
    }

    if (error_received) {
        LOG_ERROR("Scan: error received from server");
        count.SetValue(0);
        R_RETURN(MAKERESULT(0x10, 5));
    }

    if (!scan_complete) {
        LOG_WARN("Scan: timeout waiting for ScanReplyEnd");
    }

    // Copy results to output buffer
    size_t result_count = m_scan_result_count;
    size_t max_results = buffer.GetSize();
    if (result_count > max_results) {
        result_count = max_results;
    }

    for (size_t i = 0; i < result_count; i++) {
        buffer[i] = m_scan_results[i];
    }

    count.SetValue(static_cast<u32>(result_count));
    LOG_INFO("Scan: returning %zu networks", result_count);

    // Refresh inactivity timeout after scan (like Ryujinx)
    m_inactivity_timeout.RefreshTimeout();

    R_SUCCEED();
}

// ============================================================================
// Access Point Operations
// ============================================================================

Result ICommunicationService::OpenAccessPoint() {
    auto result = m_state_machine.OpenAccessPoint();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Connect to RyuLdn server
    Result rc = ConnectToServer();
    if (R_FAILED(rc)) {
        // Rollback state on connection failure
        m_state_machine.CloseAccessPoint();
        R_RETURN(rc);
    }

    // Update shared state
    SharedState::GetInstance().SetLdnState(CommState::AccessPoint);

    R_SUCCEED();
}

Result ICommunicationService::CloseAccessPoint() {
    LOG_INFO("CloseAccessPoint() called");

    // Stop P2P server if running (host cleanup)
    StopP2pProxyServer();

    // Disconnect from server
    DisconnectFromServer();

    auto result = m_state_machine.CloseAccessPoint();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Clear network info
    std::memset(&m_network_info, 0, sizeof(m_network_info));
    m_network_connected = false;

    // Update shared state
    SharedState::GetInstance().SetLdnState(CommState::Initialized);

    R_SUCCEED();
}

Result ICommunicationService::CreateNetwork(CreateNetworkConfig data) {
    LOG_INFO("CreateNetwork called");

    R_UNLESS(IsServerConnected(), MAKERESULT(0x10, 2)); // Not connected

    auto result = m_state_machine.CreateNetwork();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Build CreateAccessPoint request from config
    // Convert from ams::mitm::ldn types to ryu_ldn::protocol types
    ryu_ldn::protocol::CreateAccessPointRequest request{};

    // Security config
    request.security_config.security_mode = data.securityConfig.securityMode;
    request.security_config.passphrase_size = data.securityConfig.passphraseSize;
    std::memcpy(request.security_config.passphrase, data.securityConfig.passphrase,
                sizeof(request.security_config.passphrase));

    // User config
    std::memcpy(request.user_config.user_name, data.userConfig.userName,
                sizeof(request.user_config.user_name));

    // Network config
    request.network_config.intent_id.local_communication_id = data.networkConfig.intentId.localCommunicationId;
    request.network_config.intent_id.scene_id = data.networkConfig.intentId.sceneId;
    request.network_config.channel = data.networkConfig.channel;
    request.network_config.node_count_max = data.networkConfig.nodeCountMax;
    request.network_config.local_communication_version = data.networkConfig.localCommunicationVersion;

    // Copy game version to RyuNetworkConfig (like Ryujinx ConfigureAccessPoint)
    // _gameVersion.AsSpan().CopyTo(request.GameVersion.AsSpan())
    std::memcpy(request.ryu_network_config.game_version, m_game_version,
                sizeof(request.ryu_network_config.game_version));

    // Start P2P proxy server for hosting (like Ryujinx CreateNetworkAsync)
    // This allows direct P2P connections from joiners
    if (m_use_p2p_proxy && StartP2pProxyServer()) {
        // Attempt UPnP NAT punch to open public port
        uint16_t public_port = m_p2p_server->NatPunch();

        // Fill RyuNetworkConfig with P2P port information
        // Like Ryujinx: request.PrivateIp = GetLocalIPv4(), request.ExternalProxyPort = public_port
        uint32_t local_ip = p2p::UpnpPortMapper::GetInstance().GetLocalIPv4();

        // Store local IP as 16-byte buffer (first 4 bytes for IPv4)
        std::memset(request.ryu_network_config.private_ip, 0, sizeof(request.ryu_network_config.private_ip));
        std::memcpy(request.ryu_network_config.private_ip, &local_ip, sizeof(local_ip));

        request.ryu_network_config.address_family = 2;  // AF_INET (IPv4)
        request.ryu_network_config.external_proxy_port = public_port;
        request.ryu_network_config.internal_proxy_port = m_p2p_server->GetPrivatePort();

        LOG_INFO("CreateNetwork: P2P enabled, local_ip=0x%08X, public_port=%u, private_port=%u",
                 local_ip, public_port, m_p2p_server->GetPrivatePort());
    } else {
        // P2P disabled or failed - zero out proxy ports
        std::memset(request.ryu_network_config.private_ip, 0, sizeof(request.ryu_network_config.private_ip));
        request.ryu_network_config.address_family = 0;
        request.ryu_network_config.external_proxy_port = 0;
        request.ryu_network_config.internal_proxy_port = 0;

        LOG_INFO("CreateNetwork: P2P disabled or failed, using relay server only");
    }

    LOG_VERBOSE("CreateNetwork: local_comm_id=0x%lx, scene_id=%u, channel=%u, max_nodes=%u",
                request.network_config.intent_id.local_communication_id,
                request.network_config.intent_id.scene_id,
                request.network_config.channel,
                request.network_config.node_count_max);

    // Send to server
    auto send_result = m_server_client.send_create_access_point(request);
    if (send_result != ryu_ldn::network::ClientOpResult::Success) {
        LOG_ERROR("CreateNetwork: send failed: %s",
                  ryu_ldn::network::client_op_result_to_string(send_result));
        // Rollback state and P2P server on send failure
        StopP2pProxyServer();
        m_state_machine.DestroyNetwork();
        R_RETURN(MAKERESULT(0x10, 3)); // Send failed
    }

    LOG_INFO("CreateNetwork: sent CreateAccessPoint to server, waiting for Connected response...");

    // Wait for Connected response from server (contains NetworkInfo)
    constexpr uint64_t response_timeout_ms = 5000;
    if (!WaitForResponse(ryu_ldn::protocol::PacketId::Connected, response_timeout_ms)) {
        LOG_ERROR("CreateNetwork: did not receive Connected response from server");
        // Rollback state and P2P server on timeout/error
        StopP2pProxyServer();
        m_state_machine.DestroyNetwork();
        R_RETURN(MAKERESULT(0x10, 5)); // Response timeout
    }

    LOG_INFO("CreateNetwork: received Connected response, network created successfully");

    // Mark as connected to network and disable inactivity timeout (like Ryujinx)
    m_network_connected = true;
    m_inactivity_timeout.DisableTimeout();

    // Update shared state
    SharedState::GetInstance().SetLdnState(CommState::AccessPointCreated);

    // Signal state change event so the game knows the network is ready
    m_state_machine.SignalStateChange();

    R_SUCCEED();
}

Result ICommunicationService::DestroyNetwork() {
    LOG_INFO("DestroyNetwork() called");

    // Stop P2P server if running (host cleanup)
    StopP2pProxyServer();

    auto result = m_state_machine.DestroyNetwork();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Server will be notified via disconnect or explicit message
    // Clear network info
    std::memset(&m_network_info, 0, sizeof(m_network_info));
    m_network_connected = false;

    // Refresh inactivity timeout after leaving network (like Ryujinx)
    m_inactivity_timeout.RefreshTimeout();

    // Update shared state
    SharedState::GetInstance().SetLdnState(CommState::AccessPoint);

    R_SUCCEED();
}

Result ICommunicationService::SetAdvertiseData(ams::sf::InAutoSelectBuffer data) {
    LOG_INFO("SetAdvertiseData() called, size=%zu", data.GetSize());

    // Store advertise data locally (like Ryujinx _advertiseData)
    m_advertise_data_size = std::min(data.GetSize(), sizeof(m_advertise_data));
    if (m_advertise_data_size > 0) {
        std::memcpy(m_advertise_data, data.GetPointer(), m_advertise_data_size);
    }

    // Like Ryujinx: only send if _networkConnected
    // if (_networkConnected) { SendAsync(...); }
    if (m_network_connected) {
        auto send_result = m_server_client.send_set_advertise_data(m_advertise_data, m_advertise_data_size);
        if (send_result != ryu_ldn::network::ClientOpResult::Success) {
            LOG_ERROR("SetAdvertiseData: send failed: %s",
                      ryu_ldn::network::client_op_result_to_string(send_result));
            R_RETURN(MAKERESULT(0x10, 3)); // Send failed
        }
        LOG_VERBOSE("SetAdvertiseData: sent to server");
    }

    R_SUCCEED();
}

Result ICommunicationService::SetStationAcceptPolicy(u8 policy) {
    LOG_INFO("SetStationAcceptPolicy() called, policy=%u", policy);

    // Like Ryujinx: only send if _networkConnected
    // if (_networkConnected) { SendAsync(...); }
    if (m_network_connected) {
        auto accept_policy = static_cast<ryu_ldn::protocol::AcceptPolicy>(policy);
        auto send_result = m_server_client.send_set_accept_policy(accept_policy);
        if (send_result != ryu_ldn::network::ClientOpResult::Success) {
            LOG_ERROR("SetStationAcceptPolicy: send failed: %s",
                      ryu_ldn::network::client_op_result_to_string(send_result));
            R_RETURN(MAKERESULT(0x10, 3)); // Send failed
        }
        LOG_VERBOSE("SetStationAcceptPolicy: sent to server");
    }

    R_SUCCEED();
}

// ============================================================================
// Station Operations
// ============================================================================

Result ICommunicationService::OpenStation() {
    auto result = m_state_machine.OpenStation();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Connect to RyuLdn server
    Result rc = ConnectToServer();
    if (R_FAILED(rc)) {
        // Rollback state on connection failure
        m_state_machine.CloseStation();
        R_RETURN(rc);
    }

    // Update shared state
    SharedState::GetInstance().SetLdnState(CommState::Station);

    R_SUCCEED();
}

Result ICommunicationService::CloseStation() {
    // Disconnect from server first
    DisconnectFromServer();

    auto result = m_state_machine.CloseStation();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Clear network info
    std::memset(&m_network_info, 0, sizeof(m_network_info));

    // Update shared state
    SharedState::GetInstance().SetLdnState(CommState::Initialized);

    R_SUCCEED();
}

Result ICommunicationService::Connect(ConnectNetworkData dat, const NetworkInfo& data) {
    R_UNLESS(IsServerConnected(), MAKERESULT(0x10, 2)); // Not connected

    auto result = m_state_machine.Connect();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Build Connect request
    // Convert from ams::mitm::ldn types to ryu_ldn::protocol types
    ryu_ldn::protocol::ConnectRequest request{};

    // Security config
    request.security_config.security_mode = dat.securityConfig.securityMode;
    request.security_config.passphrase_size = dat.securityConfig.passphraseSize;
    std::memcpy(request.security_config.passphrase, dat.securityConfig.passphrase,
                sizeof(request.security_config.passphrase));

    // User config
    std::memcpy(request.user_config.user_name, dat.userConfig.userName,
                sizeof(request.user_config.user_name));

    // Other fields
    request.local_communication_version = dat.localCommunicationVersion;
    request.option_unknown = dat.option;

    // Network info - copy the full structure (compatible layout)
    std::memcpy(&request.network_info, &data, sizeof(request.network_info));

    // Send to server
    auto send_result = m_server_client.send_connect(request);
    if (send_result != ryu_ldn::network::ClientOpResult::Success) {
        // Rollback state on send failure
        m_state_machine.Disconnect();
        R_RETURN(MAKERESULT(0x10, 3)); // Send failed
    }

    LOG_INFO("Connect: sent Connect request, waiting for Connected response...");

    // Wait for Connected response from server (like Ryujinx _apConnected.WaitOne)
    constexpr uint64_t response_timeout_ms = 4000; // FailureTimeout in Ryujinx
    if (!WaitForResponse(ryu_ldn::protocol::PacketId::Connected, response_timeout_ms)) {
        LOG_ERROR("Connect: did not receive Connected response from server");
        // Rollback state on timeout/error
        m_state_machine.Disconnect();
        R_RETURN(MAKERESULT(0x10, 5)); // Response timeout
    }

    LOG_INFO("Connect: received Connected response, connected to network");

    // Mark as connected to network and disable inactivity timeout (like Ryujinx)
    m_network_connected = true;
    m_inactivity_timeout.DisableTimeout();

    // Store network info
    std::memcpy(&m_network_info, &data, sizeof(m_network_info));

    // Update shared state
    SharedState::GetInstance().SetLdnState(CommState::StationConnected);

    // Signal state change event so the game knows we're connected
    m_state_machine.SignalStateChange();

    R_SUCCEED();
}

Result ICommunicationService::Disconnect() {
    LOG_INFO("Disconnect() called");

    auto result = m_state_machine.Disconnect();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Send disconnect notification to server (like Ryujinx DisconnectNetwork)
    if (IsServerConnected() && m_network_connected) {
        auto send_result = m_server_client.send_disconnect_network();
        if (send_result != ryu_ldn::network::ClientOpResult::Success) {
            LOG_WARN("Disconnect: failed to send disconnect to server: %s",
                     ryu_ldn::network::client_op_result_to_string(send_result));
            // Continue anyway - server will detect disconnect
        } else {
            LOG_VERBOSE("Disconnect: sent disconnect notification to server");
        }
    }

    m_network_connected = false;
    m_disconnect_reason = DisconnectReason::User;

    // Refresh inactivity timeout after leaving network (like Ryujinx)
    m_inactivity_timeout.RefreshTimeout();

    // Clear network info
    std::memset(&m_network_info, 0, sizeof(m_network_info));

    // Update shared state
    SharedState::GetInstance().SetLdnState(CommState::Station);

    R_SUCCEED();
}

// ============================================================================
// Private Network Operations
// ============================================================================

Result ICommunicationService::ScanPrivate(
        ams::sf::Out<u32> count,
        ams::sf::OutAutoSelectArray<NetworkInfo> buffer,
        u16 channel,
        ScanFilter filter) {
    // ScanPrivate is the same as Scan but for private networks
    // The filter behavior is slightly different (doesn't mask BSSID flag)
    return Scan(count, buffer, channel, filter);
}

Result ICommunicationService::CreateNetworkPrivate(
        CreateNetworkPrivateConfig data,
        ams::sf::InPointerBuffer addressList) {
    R_UNLESS(IsServerConnected(), MAKERESULT(0x10, 2)); // Not connected

    auto result = m_state_machine.CreateNetwork();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Build CreateAccessPointPrivate request from config
    ryu_ldn::protocol::CreateAccessPointPrivateRequest request{};

    // Security config
    request.security_config.security_mode = data.securityConfig.securityMode;
    request.security_config.passphrase_size = data.securityConfig.passphraseSize;
    std::memcpy(request.security_config.passphrase, data.securityConfig.passphrase,
                sizeof(request.security_config.passphrase));

    // Security parameter
    std::memcpy(request.security_parameter.data, data.securityParameter.unkRandom,
                sizeof(request.security_parameter.data));
    std::memcpy(request.security_parameter.session_id, &data.securityParameter.sessionId,
                sizeof(request.security_parameter.session_id));

    // User config
    std::memcpy(request.user_config.user_name, data.userConfig.userName,
                sizeof(request.user_config.user_name));

    // Network config
    request.network_config.intent_id.local_communication_id = data.networkConfig.intentId.localCommunicationId;
    request.network_config.intent_id.scene_id = data.networkConfig.intentId.sceneId;
    request.network_config.channel = data.networkConfig.channel;
    request.network_config.node_count_max = data.networkConfig.nodeCountMax;
    request.network_config.local_communication_version = data.networkConfig.localCommunicationVersion;

    // Address list - copy from IPC buffer
    if (addressList.GetSize() >= sizeof(ryu_ldn::protocol::AddressList)) {
        std::memcpy(&request.address_list, addressList.GetPointer(),
                    sizeof(request.address_list));
    }

    // Ryu network config - copy game version (like Ryujinx ConfigureAccessPoint)
    std::memset(&request.ryu_network_config, 0, sizeof(request.ryu_network_config));
    std::memcpy(request.ryu_network_config.game_version, m_game_version,
                sizeof(request.ryu_network_config.game_version));

    // Send to server
    auto send_result = m_server_client.send_create_access_point_private(request);
    if (send_result != ryu_ldn::network::ClientOpResult::Success) {
        // Rollback state on send failure
        m_state_machine.DestroyNetwork();
        R_RETURN(MAKERESULT(0x10, 3)); // Send failed
    }

    LOG_INFO("CreateNetworkPrivate: sent request, waiting for Connected response...");

    // Wait for Connected response from server (like Ryujinx CreateNetworkCommon)
    constexpr uint64_t response_timeout_ms = 4000; // FailureTimeout in Ryujinx
    if (!WaitForResponse(ryu_ldn::protocol::PacketId::Connected, response_timeout_ms)) {
        LOG_ERROR("CreateNetworkPrivate: did not receive Connected response from server");
        m_state_machine.DestroyNetwork();
        R_RETURN(MAKERESULT(0x10, 5)); // Response timeout
    }

    LOG_INFO("CreateNetworkPrivate: received Connected response");

    // Update shared state
    SharedState::GetInstance().SetLdnState(CommState::AccessPointCreated);

    // Signal state change event
    m_state_machine.SignalStateChange();

    R_SUCCEED();
}

Result ICommunicationService::ConnectPrivate(ConnectPrivateData data) {
    R_UNLESS(IsServerConnected(), MAKERESULT(0x10, 2)); // Not connected

    auto result = m_state_machine.Connect();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Build ConnectPrivate request
    ryu_ldn::protocol::ConnectPrivateRequest request{};

    // Security config
    request.security_config.security_mode = data.securityConfig.securityMode;
    request.security_config.passphrase_size = data.securityConfig.passphraseSize;
    std::memcpy(request.security_config.passphrase, data.securityConfig.passphrase,
                sizeof(request.security_config.passphrase));

    // Security parameter
    std::memcpy(request.security_parameter.data, data.securityParameter.unkRandom,
                sizeof(request.security_parameter.data));
    std::memcpy(request.security_parameter.session_id, &data.securityParameter.sessionId,
                sizeof(request.security_parameter.session_id));

    // User config
    std::memcpy(request.user_config.user_name, data.userConfig.userName,
                sizeof(request.user_config.user_name));

    // Other fields
    request.local_communication_version = data.localCommunicationVersion;
    request.option_unknown = data.option;

    // Network config
    request.network_config.intent_id.local_communication_id = data.networkConfig.intentId.localCommunicationId;
    request.network_config.intent_id.scene_id = data.networkConfig.intentId.sceneId;
    request.network_config.channel = data.networkConfig.channel;
    request.network_config.node_count_max = data.networkConfig.nodeCountMax;
    request.network_config.local_communication_version = data.networkConfig.localCommunicationVersion;

    // Send to server
    auto send_result = m_server_client.send_connect_private(request);
    if (send_result != ryu_ldn::network::ClientOpResult::Success) {
        // Rollback state on send failure
        m_state_machine.Disconnect();
        R_RETURN(MAKERESULT(0x10, 3)); // Send failed
    }

    LOG_INFO("ConnectPrivate: sent request, waiting for Connected response...");

    // Wait for Connected response from server (like Ryujinx ConnectCommon)
    constexpr uint64_t response_timeout_ms = 4000; // FailureTimeout in Ryujinx
    if (!WaitForResponse(ryu_ldn::protocol::PacketId::Connected, response_timeout_ms)) {
        LOG_ERROR("ConnectPrivate: did not receive Connected response from server");
        m_state_machine.Disconnect();
        R_RETURN(MAKERESULT(0x10, 5)); // Response timeout
    }

    LOG_INFO("ConnectPrivate: received Connected response");

    // Update shared state
    SharedState::GetInstance().SetLdnState(CommState::StationConnected);

    // Signal state change event
    m_state_machine.SignalStateChange();

    R_SUCCEED();
}

// ============================================================================
// Other Operations
// ============================================================================

Result ICommunicationService::SetWirelessControllerRestriction() {
    // Stub - wireless controller restriction not needed for online play
    R_SUCCEED();
}

Result ICommunicationService::Reject(u32 nodeId) {
    LOG_INFO("Reject() called, nodeId=%u", nodeId);

    // Like Ryujinx: check _networkConnected instead of just IsServerConnected
    // if (_networkConnected) { ... } else { return ResultCode.InvalidState; }
    if (!m_network_connected) {
        LOG_WARN("Reject: not in network session");
        R_RETURN(MAKERESULT(0x10, 2)); // InvalidState - not in network
    }

    // Clear reject event before sending (like Ryujinx _reject.Reset())
    m_reject_event.Clear();
    m_error_event.Clear();

    // Send reject request to server (like Ryujinx Reject)
    auto send_result = m_server_client.send_reject(nodeId, ryu_ldn::protocol::DisconnectReason::Rejected);
    if (send_result != ryu_ldn::network::ClientOpResult::Success) {
        LOG_ERROR("Reject: send failed: %s",
                  ryu_ldn::network::client_op_result_to_string(send_result));
        R_RETURN(MAKERESULT(0x10, 3)); // Send failed
    }

    // Wait for RejectReply from server
    // Like Ryujinx: int index = WaitHandle.WaitAny([_reject, _error], InactiveTimeout);
    constexpr uint64_t reject_timeout_ms = 6000; // InactiveTimeout (was 4000 FailureTimeout)
    uint64_t start_time_ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;
    uint64_t current_time_ms = start_time_ms;

    while ((current_time_ms - start_time_ms) < reject_timeout_ms) {
        m_server_client.update(current_time_ms);

        if (m_reject_event.TryWait()) {
            LOG_INFO("Reject: received RejectReply");
            // Like Ryujinx: return (ConsumeNetworkError() != NetworkError.None) ? InvalidState : Success
            if (ConsumeNetworkError() != ryu_ldn::protocol::NetworkErrorCode::None) {
                R_RETURN(MAKERESULT(0x10, 4)); // InvalidState due to error
            }
            R_SUCCEED();
        }

        if (m_error_event.TryWait()) {
            LOG_ERROR("Reject: error received");
            R_RETURN(MAKERESULT(0x10, 4)); // Error
        }

        if (!m_server_client.is_connected()) {
            LOG_ERROR("Reject: connection lost");
            R_RETURN(MAKERESULT(0x10, 5)); // Connection lost
        }

        svcSleepThread(5 * 1000000ULL); // 5ms
        current_time_ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;
    }

    // Like Ryujinx: timeout returns InvalidState
    LOG_WARN("Reject: timeout waiting for RejectReply");
    R_RETURN(MAKERESULT(0x10, 2)); // InvalidState on timeout
}

Result ICommunicationService::AddAcceptFilterEntry() {
    // Stub - accept filter not implemented
    R_SUCCEED();
}

Result ICommunicationService::ClearAcceptFilter() {
    // Stub - accept filter not implemented
    R_SUCCEED();
}

// ============================================================================
// Packet Callback Handlers
// ============================================================================

void ICommunicationService::HandleServerPacket(ryu_ldn::protocol::PacketId id, const uint8_t* data, size_t size) {
    LOG_VERBOSE("Received packet from server: type=%u, size=%zu",
                static_cast<unsigned>(id), size);

    switch (id) {
        case ryu_ldn::protocol::PacketId::Connected: {
            // Server confirms we joined/created a network - contains NetworkInfo
            if (size >= sizeof(ryu_ldn::protocol::NetworkInfo)) {
                const auto* net_info = reinterpret_cast<const ryu_ldn::protocol::NetworkInfo*>(data);

                // Copy to our local NetworkInfo (layout is compatible)
                std::memcpy(&m_network_info, net_info, sizeof(m_network_info));

                // Set network connected flag (like Ryujinx _networkConnected = true)
                m_network_connected = true;

                LOG_INFO("Received Connected: node_count=%u, max=%u",
                         m_network_info.ldn.nodeCount,
                         m_network_info.ldn.nodeCountMax);

                // Update session info in shared state
                auto& shared_state = SharedState::GetInstance();
                bool is_host = (m_network_info.ldn.nodes[0].isConnected &&
                               m_state_machine.GetState() == CommState::AccessPointCreated);
                shared_state.SetSessionInfo(
                    m_network_info.ldn.nodeCount,
                    m_network_info.ldn.nodeCountMax,
                    0, // local_node_id - TODO: determine from nodes array
                    is_host
                );
            } else {
                LOG_ERROR("Connected packet too small: %zu < %zu",
                          size, sizeof(ryu_ldn::protocol::NetworkInfo));
            }
            break;
        }

        case ryu_ldn::protocol::PacketId::SyncNetwork: {
            // Server sends updated network state - contains NetworkInfo
            if (size >= sizeof(ryu_ldn::protocol::NetworkInfo)) {
                const auto* net_info = reinterpret_cast<const ryu_ldn::protocol::NetworkInfo*>(data);
                std::memcpy(&m_network_info, net_info, sizeof(m_network_info));

                LOG_VERBOSE("Received SyncNetwork: node_count=%u",
                            m_network_info.ldn.nodeCount);

                // Update session info
                auto& shared_state = SharedState::GetInstance();
                shared_state.SetSessionInfo(
                    m_network_info.ldn.nodeCount,
                    m_network_info.ldn.nodeCountMax,
                    0, // local_node_id
                    m_state_machine.GetState() == CommState::AccessPointCreated
                );

                // Signal state change event so game knows network updated
                m_state_machine.SignalStateChange();
            }
            break;
        }

        case ryu_ldn::protocol::PacketId::Disconnect: {
            // Server notifies us of disconnection
            LOG_INFO("Received Disconnect from server");
            m_network_connected = false;
            m_disconnect_reason = DisconnectReason::SystemRequest;
            // Signal state change
            m_state_machine.SignalStateChange();
            break;
        }

        case ryu_ldn::protocol::PacketId::Reject: {
            // We received a Reject - we are being rejected/kicked from the network
            // Like Ryujinx HandleReject: _disconnectReason = reject.DisconnectReason
            if (size >= sizeof(ryu_ldn::protocol::RejectRequest)) {
                const auto* reject = reinterpret_cast<const ryu_ldn::protocol::RejectRequest*>(data);
                m_disconnect_reason = static_cast<DisconnectReason>(reject->disconnect_reason);
                LOG_INFO("Received Reject from server: reason=%u", reject->disconnect_reason);
            } else {
                m_disconnect_reason = DisconnectReason::Rejected;
                LOG_INFO("Received Reject from server (no reason provided)");
            }
            // Note: The actual disconnect will come via Disconnect packet or connection close
            break;
        }

        case ryu_ldn::protocol::PacketId::RejectReply: {
            // Server confirms our reject request was processed
            LOG_INFO("Received RejectReply from server");
            // Signal reject event (like Ryujinx _reject.Set())
            m_reject_event.Signal();
            break;
        }

        case ryu_ldn::protocol::PacketId::Ping: {
            // Echo ping back to server
            if (size >= sizeof(ryu_ldn::protocol::PingMessage)) {
                const auto* ping = reinterpret_cast<const ryu_ldn::protocol::PingMessage*>(data);
                LOG_VERBOSE("Received Ping: requester=%u, id=%u", ping->requester, ping->id);

                // Echo back if server requested
                if (ping->requester == 0) {
                    m_server_client.send_ping_response(ping->id);
                }
            }
            break;
        }

        case ryu_ldn::protocol::PacketId::NetworkError: {
            // Server reports an error - like Ryujinx HandleNetworkError
            if (size >= sizeof(ryu_ldn::protocol::NetworkErrorMessage)) {
                const auto* err = reinterpret_cast<const ryu_ldn::protocol::NetworkErrorMessage*>(data);
                auto error_code = static_cast<ryu_ldn::protocol::NetworkErrorCode>(err->error_code);

                // Like Ryujinx: special handling for PortUnreachable
                // if (error.Error == NetworkError.PortUnreachable) { _useP2pProxy = false; }
                // else { _lastError = error.Error; }
                if (error_code == ryu_ldn::protocol::NetworkErrorCode::None) {
                    // PortUnreachable equivalent - disable P2P proxy
                    // (We don't have P2P proxy yet, so just log)
                    LOG_WARN("Received NetworkError: PortUnreachable (P2P disabled)");
                } else {
                    m_last_network_error = error_code;
                    LOG_ERROR("Received NetworkError: code=%u", err->error_code);
                }
            }
            // Signal error event (like Ryujinx _error.Set())
            m_error_event.Signal();
            break;
        }

        case ryu_ldn::protocol::PacketId::ScanReply: {
            // Server sends one network info for each discovered network
            if (size >= sizeof(ryu_ldn::protocol::NetworkInfo)) {
                if (m_scan_result_count < MAX_SCAN_RESULTS) {
                    const auto* net_info = reinterpret_cast<const ryu_ldn::protocol::NetworkInfo*>(data);
                    std::memcpy(&m_scan_results[m_scan_result_count], net_info, sizeof(NetworkInfo));
                    m_scan_result_count++;
                    LOG_INFO("ScanReply: found network #%zu, node_count=%u",
                             m_scan_result_count,
                             reinterpret_cast<const NetworkInfo*>(net_info)->ldn.nodeCount);
                } else {
                    LOG_WARN("ScanReply: buffer full, ignoring network");
                }
            }
            break;
        }

        case ryu_ldn::protocol::PacketId::ScanReplyEnd: {
            // Server finished sending scan results
            LOG_INFO("ScanReplyEnd: scan complete, found %zu networks", m_scan_result_count);
            // Signal scan event (like Ryujinx _scan.Set())
            m_scan_event.Signal();
            break;
        }

        case ryu_ldn::protocol::PacketId::ProxyConfig: {
            // Server sends proxy configuration (like Ryujinx HandleProxyConfig)
            if (size >= sizeof(ryu_ldn::protocol::ProxyConfig)) {
                const auto* config = reinterpret_cast<const ryu_ldn::protocol::ProxyConfig*>(data);
                m_proxy_config = *config;
                LOG_INFO("Received ProxyConfig: ip=0x%08X, mask=0x%08X",
                         config->proxy_ip, config->proxy_subnet_mask);
                // Note: On Ryujinx this registers an LdnProxy for socket interception.
                // On Switch sysmodule, we just store the config for reference.
                // The actual proxying is handled by the game's LDN implementation.
            }
            break;
        }

        case ryu_ldn::protocol::PacketId::ExternalProxy: {
            // Server sends external proxy info for P2P (like Ryujinx HandleExternalProxy)
            if (size >= sizeof(ryu_ldn::protocol::ExternalProxyConfig)) {
                const auto* config = reinterpret_cast<const ryu_ldn::protocol::ExternalProxyConfig*>(data);
                m_external_proxy_config = *config;

                LOG_INFO("Received ExternalProxy: port=%u, family=%u",
                         config->proxy_port, config->address_family);

                // Like Ryujinx: if P2P is disabled, we don't connect to external proxy
                // The _useP2pProxy flag controls this behavior
                if (!m_use_p2p_proxy) {
                    LOG_INFO("P2P proxy disabled, ignoring ExternalProxy");
                } else {
                    // Create P2pProxyClient and connect to host (like Ryujinx HandleExternalProxy)
                    HandleExternalProxyConnect(*config);
                }
            }
            break;
        }

        case ryu_ldn::protocol::PacketId::ExternalProxyToken: {
            // Server sends token for expected P2P joiner (like Ryujinx HandleExternalProxyToken)
            // This is sent to the HOST when a joiner is about to connect via P2P
            if (size >= sizeof(ryu_ldn::protocol::ExternalProxyToken)) {
                const auto* token = reinterpret_cast<const ryu_ldn::protocol::ExternalProxyToken*>(data);
                LOG_INFO("Received ExternalProxyToken: virtual_ip=0x%08X",
                         token->virtual_ip);

                // Add token to P2P server's waiting list for authentication
                HandleExternalProxyToken(*token);
            }
            break;
        }

        case ryu_ldn::protocol::PacketId::ProxyData: {
            // Server relays game data from other players (like Ryujinx HandleProxyData)
            // Route to BSD MITM proxy sockets for transparent game socket interception
            if (size >= sizeof(ryu_ldn::protocol::ProxyDataHeader)) {
                const auto* proxy_header = reinterpret_cast<const ryu_ldn::protocol::ProxyDataHeader*>(data);
                const uint8_t* payload = data + sizeof(ryu_ldn::protocol::ProxyDataHeader);
                size_t payload_size = size - sizeof(ryu_ldn::protocol::ProxyDataHeader);

                // Validate payload size matches header
                if (payload_size >= proxy_header->data_length) {
                    LOG_VERBOSE("Received ProxyData: src=0x%08X:%u dst=0x%08X:%u proto=%u len=%u",
                                proxy_header->info.source_ipv4, proxy_header->info.source_port,
                                proxy_header->info.dest_ipv4, proxy_header->info.dest_port,
                                static_cast<unsigned>(proxy_header->info.protocol),
                                proxy_header->data_length);

                    // Convert protocol type for BSD layer
                    ryu_ldn::bsd::ProtocolType bsd_protocol;
                    bool protocol_valid = true;
                    switch (proxy_header->info.protocol) {
                        case ryu_ldn::protocol::ProtocolType::Tcp:
                            bsd_protocol = ryu_ldn::bsd::ProtocolType::Tcp;
                            break;
                        case ryu_ldn::protocol::ProtocolType::Udp:
                            bsd_protocol = ryu_ldn::bsd::ProtocolType::Udp;
                            break;
                        default:
                            LOG_WARN("ProxyData: unknown protocol type %u",
                                     static_cast<unsigned>(proxy_header->info.protocol));
                            protocol_valid = false;
                            break;
                    }

                    if (!protocol_valid) {
                        break;
                    }

                    // Route to BSD MITM proxy socket manager
                    // The manager finds the socket bound to the destination port and queues the data
                    auto& socket_manager = mitm::bsd::ProxySocketManager::GetInstance();
                    bool routed = socket_manager.RouteIncomingData(
                        proxy_header->info.source_ipv4,
                        proxy_header->info.source_port,
                        proxy_header->info.dest_ipv4,
                        proxy_header->info.dest_port,
                        bsd_protocol,
                        payload,
                        proxy_header->data_length
                    );

                    if (routed) {
                        LOG_VERBOSE("ProxyData: routed to proxy socket");
                    } else {
                        // No matching proxy socket - fallback to legacy buffer for direct reads
                        LOG_VERBOSE("ProxyData: no matching proxy socket, storing in buffer");
                        if (!m_proxy_buffer.Write(*proxy_header, payload, proxy_header->data_length)) {
                            LOG_WARN("ProxyData: buffer full, dropping packet");
                        }
                    }
                } else {
                    LOG_WARN("ProxyData: payload size mismatch (%zu < %u)",
                             payload_size, proxy_header->data_length);
                }
            }
            break;
        }

        default:
            LOG_VERBOSE("Unhandled packet type: %u", static_cast<unsigned>(id));
            break;
    }

    // Signal that we received a response (for WaitForResponse)
    m_last_response_id = id;
    m_response_event.Signal();
}

bool ICommunicationService::WaitForResponse(ryu_ldn::protocol::PacketId expected_id, uint64_t timeout_ms) {
    LOG_VERBOSE("Waiting for response: type=%u, timeout=%lu ms",
                static_cast<unsigned>(expected_id), timeout_ms);

    // Clear events before waiting
    m_response_event.Clear();
    m_error_event.Clear();
    m_last_response_id = ryu_ldn::protocol::PacketId::Initialize; // Reset to invalid

    // Wait with polling for network updates (required because we don't have async receive)
    uint64_t start_time_ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;
    uint64_t current_time_ms = start_time_ms;

    while ((current_time_ms - start_time_ms) < timeout_ms) {
        // Process incoming packets
        m_server_client.update(current_time_ms);

        // Check if we received a response
        if (m_response_event.TryWait()) {
            // Check if we got the expected response
            if (m_last_response_id == expected_id) {
                LOG_VERBOSE("Received expected response: type=%u", static_cast<unsigned>(expected_id));
                return true;
            }

            // Check for error response
            if (m_last_response_id == ryu_ldn::protocol::PacketId::NetworkError) {
                LOG_ERROR("Received NetworkError while waiting for response");
                return false;
            }

            LOG_WARN("Received unexpected response: expected=%u, got=%u",
                     static_cast<unsigned>(expected_id), static_cast<unsigned>(m_last_response_id));
            // Continue waiting for the expected response
            m_response_event.Clear();
        }

        // Check if connection was lost
        if (!m_server_client.is_connected()) {
            LOG_ERROR("Connection lost while waiting for response");
            return false;
        }

        // Short sleep to avoid busy-waiting
        svcSleepThread(5 * 1000000ULL); // 5ms
        current_time_ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;
    }

    LOG_ERROR("Timeout waiting for response: type=%u", static_cast<unsigned>(expected_id));
    return false;
}

// ============================================================================
// Helper Methods (like Ryujinx)
// ============================================================================

void ICommunicationService::SetGameVersion(const uint8_t* version) {
    // Like Ryujinx SetGameVersion - copy version and pad to 16 bytes
    if (version != nullptr) {
        std::memcpy(m_game_version, version, sizeof(m_game_version));
    } else {
        std::memset(m_game_version, 0, sizeof(m_game_version));
    }
}

ryu_ldn::protocol::NetworkErrorCode ICommunicationService::ConsumeNetworkError() {
    // Like Ryujinx ConsumeNetworkError - return and reset
    auto result = m_last_network_error;
    m_last_network_error = ryu_ldn::protocol::NetworkErrorCode::None;
    return result;
}

ryu_ldn::network::ClientOpResult ICommunicationService::SendProxyDataToServer(
    const ryu_ldn::protocol::ProxyDataHeader& header,
    const void* data,
    size_t data_len)
{
    if (!IsServerConnected()) {
        return ryu_ldn::network::ClientOpResult::NotConnected;
    }

    LOG_VERBOSE("SendProxyDataToServer: src=0x%08X:%u dst=0x%08X:%u proto=%u len=%zu",
                header.info.source_ipv4, header.info.source_port,
                header.info.dest_ipv4, header.info.dest_port,
                static_cast<unsigned>(header.info.protocol), data_len);

    // If P2P client is connected, send through P2P instead of master server
    if (m_p2p_client != nullptr && m_p2p_client->IsReady()) {
        LOG_VERBOSE("SendProxyDataToServer: routing via P2P client");
        if (m_p2p_client->SendProxyData(header, static_cast<const uint8_t*>(data), data_len)) {
            return ryu_ldn::network::ClientOpResult::Success;
        }
        // Fall through to master server if P2P send fails
        LOG_WARN("P2P send failed, falling back to master server");
    }

    return m_server_client.send_proxy_data(header, static_cast<const uint8_t*>(data), data_len);
}

// ============================================================================
// P2P Proxy Methods
// ============================================================================

void ICommunicationService::HandleExternalProxyConnect(
    const ryu_ldn::protocol::ExternalProxyConfig& config)
{
    // Like Ryujinx HandleExternalProxy - create P2pProxyClient and connect to host
    LOG_INFO("HandleExternalProxyConnect: connecting to P2P host port=%u", config.proxy_port);

    // Clean up any existing P2P client
    DisconnectP2pProxy();

    // Create callback to route P2P packets to BSD MITM
    auto packet_callback = [](ryu_ldn::protocol::PacketId type,
                              const void* data, size_t size) {
        // Route packets received from P2P host to BSD MITM
        // This is called from P2pProxyClient's receive thread
        if (type == ryu_ldn::protocol::PacketId::ProxyData) {
            if (size >= sizeof(ryu_ldn::protocol::ProxyDataHeader)) {
                const auto* proxy_header = reinterpret_cast<const ryu_ldn::protocol::ProxyDataHeader*>(data);
                const uint8_t* payload = reinterpret_cast<const uint8_t*>(data) + sizeof(ryu_ldn::protocol::ProxyDataHeader);
                size_t payload_size = size - sizeof(ryu_ldn::protocol::ProxyDataHeader);

                if (payload_size >= proxy_header->data_length) {
                    // Convert protocol type
                    ryu_ldn::bsd::ProtocolType bsd_protocol;
                    switch (proxy_header->info.protocol) {
                        case ryu_ldn::protocol::ProtocolType::Tcp:
                            bsd_protocol = ryu_ldn::bsd::ProtocolType::Tcp;
                            break;
                        case ryu_ldn::protocol::ProtocolType::Udp:
                            bsd_protocol = ryu_ldn::bsd::ProtocolType::Udp;
                            break;
                        default:
                            return;
                    }

                    // Route to BSD MITM
                    auto& socket_manager = mitm::bsd::ProxySocketManager::GetInstance();
                    socket_manager.RouteIncomingData(
                        proxy_header->info.source_ipv4,
                        proxy_header->info.source_port,
                        proxy_header->info.dest_ipv4,
                        proxy_header->info.dest_port,
                        bsd_protocol,
                        payload,
                        proxy_header->data_length
                    );
                }
            }
        }
    };

    // Create new P2P client
    m_p2p_client = new p2p::P2pProxyClient(packet_callback);

    // Connect to P2P host using IP from config
    // ExternalProxyConfig has proxy_ip[16] for IPv4/IPv6
    // address_family indicates IPv4 (2) or IPv6 (23)
    bool connected = false;
    if (config.address_family == 2) {  // AF_INET
        // IPv4 address - first 4 bytes of proxy_ip
        connected = m_p2p_client->Connect(config.proxy_ip, 4, config.proxy_port);
    } else {
        LOG_WARN("Unsupported address family: %u", config.address_family);
    }

    if (!connected) {
        LOG_ERROR("Failed to connect to P2P host");
        DisconnectP2pProxy();
        return;
    }

    // Perform authentication with ExternalProxyConfig
    if (!m_p2p_client->PerformAuth(config)) {
        LOG_ERROR("P2P authentication failed");
        DisconnectP2pProxy();
        return;
    }

    // Wait for ProxyConfig response from host
    if (!m_p2p_client->EnsureProxyReady()) {
        LOG_ERROR("P2P proxy not ready (timeout waiting for ProxyConfig)");
        DisconnectP2pProxy();
        return;
    }

    // Store P2P proxy config
    m_proxy_config = m_p2p_client->GetProxyConfig();
    LOG_INFO("P2P connection established: virtual_ip=0x%08X",
             m_proxy_config.proxy_ip);
}

void ICommunicationService::DisconnectP2pProxy() {
    if (m_p2p_client != nullptr) {
        LOG_INFO("Disconnecting P2P proxy client");
        m_p2p_client->Disconnect();
        delete m_p2p_client;
        m_p2p_client = nullptr;
    }
}

// ============================================================================
// P2P Proxy Server Methods (Host Side)
// ============================================================================

bool ICommunicationService::StartP2pProxyServer() {
    // Like Ryujinx CreateNetworkAsync - start P2P server for hosting
    LOG_INFO("StartP2pProxyServer: starting P2P server for hosting");

    // Stop any existing server first
    StopP2pProxyServer();

    // Check if P2P is disabled
    if (!m_use_p2p_proxy) {
        LOG_INFO("P2P proxy disabled, skipping server start");
        return false;
    }

    // Create server with callback to send notifications to master server
    // Like Ryujinx: _hostedProxy = new P2pProxyServer(SendAsync)
    // Use static callback with user_data pattern (cannot use lambda with capture)
    auto master_send_callback = [](const void* data, size_t size, void* user_data) {
        auto* self = static_cast<ICommunicationService*>(user_data);
        if (self->IsServerConnected()) {
            self->m_server_client.send_raw_packet(data, size);
        }
    };
    m_p2p_server = new p2p::P2pProxyServer(master_send_callback, this);

    // Start listening on an available port
    if (!m_p2p_server->Start()) {
        LOG_ERROR("StartP2pProxyServer: failed to start TCP server");
        StopP2pProxyServer();
        return false;
    }

    LOG_INFO("StartP2pProxyServer: server started on port %u",
             m_p2p_server->GetPrivatePort());
    return true;
}

void ICommunicationService::StopP2pProxyServer() {
    if (m_p2p_server != nullptr) {
        LOG_INFO("StopP2pProxyServer: stopping P2P server");

        // Release UPnP port mapping
        m_p2p_server->ReleaseNatPunch();

        // Stop server and delete
        m_p2p_server->Stop();
        delete m_p2p_server;
        m_p2p_server = nullptr;
    }
}

void ICommunicationService::HandleExternalProxyToken(
    const ryu_ldn::protocol::ExternalProxyToken& token)
{
    // Like Ryujinx HandleExternalProxyToken - add token to waiting list
    // Called when master server notifies us a joiner is about to connect
    if (m_p2p_server != nullptr && m_p2p_server->IsRunning()) {
        LOG_INFO("HandleExternalProxyToken: adding token for expected joiner");
        m_p2p_server->AddWaitingToken(token);
    } else {
        LOG_WARN("HandleExternalProxyToken: P2P server not running");
    }
}

} // namespace ams::mitm::ldn
