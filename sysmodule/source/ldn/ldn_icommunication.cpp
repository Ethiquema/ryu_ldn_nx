/**
 * @file ldn_icommunication.cpp
 * @brief LDN Communication Service implementation
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ldn_icommunication.hpp"
#include "../debug/log.hpp"
#include <arpa/inet.h>

namespace ams::mitm::ldn {

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
    , m_server_client()
    , m_server_connected(false)
{
    // State machine handles event creation internally
}

ICommunicationService::~ICommunicationService() {
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

    // Attempt connection
    auto result = m_server_client.connect();
    if (result != ryu_ldn::network::ClientOpResult::Success) {
        LOG_ERROR("Server connection failed: %s",
                  ryu_ldn::network::client_op_result_to_string(result));
        R_RETURN(MAKERESULT(0x10, 2)); // Connection failed
    }

    m_server_connected = true;
    LOG_INFO("Connected to RyuLdn server successfully");
    R_SUCCEED();
}

void ICommunicationService::DisconnectFromServer() {
    if (m_server_connected) {
        LOG_INFO("Disconnecting from RyuLdn server");
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

    LOG_VERBOSE("LDN Initialized successfully");
    R_SUCCEED();
}

Result ICommunicationService::InitializeSystem2(u64 unk, const ams::sf::ClientProcessId& client_process_id) {
    m_error_state = unk;
    return Initialize(client_process_id);
}

Result ICommunicationService::Finalize() {
    // Disconnect from RyuLdn server if connected
    DisconnectFromServer();

    // Transition back to None state
    m_state_machine.Finalize();

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
    state.SetValue(static_cast<u32>(m_state_machine.GetState()));

    // If error_state is set and we have a disconnect reason, return error
    if (m_error_state != 0) {
        if (m_disconnect_reason != DisconnectReason::None) {
            R_RETURN(MAKERESULT(0x10, static_cast<u32>(m_disconnect_reason)));
        }
    }

    R_SUCCEED();
}

Result ICommunicationService::GetNetworkInfo(ams::sf::Out<NetworkInfo> buffer) {
    buffer.SetValue(m_network_info);
    R_SUCCEED();
}

Result ICommunicationService::GetIpv4Address(ams::sf::Out<u32> address, ams::sf::Out<u32> mask) {
    // Get current IP from nifm service
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
    AMS_UNUSED(buffer);
    AMS_UNUSED(channel);

    R_UNLESS(IsServerConnected(), MAKERESULT(0x10, 2)); // Not connected

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
        count.SetValue(0);
        R_RETURN(MAKERESULT(0x10, 3)); // Send failed
    }

    // TODO: Wait for scan results from server callback
    // For now, return empty list - actual results will come via packet callback
    count.SetValue(0);
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

    R_SUCCEED();
}

Result ICommunicationService::CloseAccessPoint() {
    // Disconnect from server first
    DisconnectFromServer();

    auto result = m_state_machine.CloseAccessPoint();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Clear network info
    std::memset(&m_network_info, 0, sizeof(m_network_info));

    R_SUCCEED();
}

Result ICommunicationService::CreateNetwork(CreateNetworkConfig data) {
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

    // Send to server
    auto send_result = m_server_client.send_create_access_point(request);
    if (send_result != ryu_ldn::network::ClientOpResult::Success) {
        // Rollback state on send failure
        m_state_machine.DestroyNetwork();
        R_RETURN(MAKERESULT(0x10, 3)); // Send failed
    }

    R_SUCCEED();
}

Result ICommunicationService::DestroyNetwork() {
    auto result = m_state_machine.DestroyNetwork();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Server will be notified via disconnect or explicit message
    // Clear network info
    std::memset(&m_network_info, 0, sizeof(m_network_info));

    R_SUCCEED();
}

Result ICommunicationService::SetAdvertiseData(ams::sf::InAutoSelectBuffer data) {
    // Advertise data is sent as part of network info updates
    // Store locally for now - will be sent with CreateNetwork
    AMS_UNUSED(data);

    // TODO: If network already created, send update to server

    R_SUCCEED();
}

Result ICommunicationService::SetStationAcceptPolicy(u8 policy) {
    AMS_UNUSED(policy);
    // TODO: Implement station accept policy
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

    R_SUCCEED();
}

Result ICommunicationService::CloseStation() {
    // Disconnect from server first
    DisconnectFromServer();

    auto result = m_state_machine.CloseStation();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Clear network info
    std::memset(&m_network_info, 0, sizeof(m_network_info));

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

    // Store network info
    std::memcpy(&m_network_info, &data, sizeof(m_network_info));

    R_SUCCEED();
}

Result ICommunicationService::Disconnect() {
    auto result = m_state_machine.Disconnect();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Server disconnect is handled by CloseStation or connection loss
    m_disconnect_reason = DisconnectReason::User;

    // Clear network info
    std::memset(&m_network_info, 0, sizeof(m_network_info));

    R_SUCCEED();
}

// ============================================================================
// Stub Operations
// ============================================================================

Result ICommunicationService::ScanPrivate() {
    R_SUCCEED();
}

Result ICommunicationService::SetWirelessControllerRestriction() {
    R_SUCCEED();
}

Result ICommunicationService::CreateNetworkPrivate() {
    R_SUCCEED();
}

Result ICommunicationService::Reject() {
    R_SUCCEED();
}

Result ICommunicationService::AddAcceptFilterEntry() {
    R_SUCCEED();
}

Result ICommunicationService::ClearAcceptFilter() {
    R_SUCCEED();
}

Result ICommunicationService::ConnectPrivate() {
    R_SUCCEED();
}

} // namespace ams::mitm::ldn
