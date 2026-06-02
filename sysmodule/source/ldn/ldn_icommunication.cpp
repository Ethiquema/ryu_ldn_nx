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
#include "../bsd/bsd_mitm_service.hpp"
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

// Background thread stack - allocated statically to avoid bloating class size
alignas(os::ThreadStackAlignment) static u8 g_background_thread_stack[0x4000];

// Async ExternalProxy connect thread stack — see m_p2p_connect_thread comment
// in the header. The connect+auth+EnsureProxyReady chain takes 1–4 s and used
// to run inline inside HandleServerPacket from the WaitForResponse poll loop,
// freezing further packet dispatch (including the master server's `Connected`
// reply, which arrives right after `ExternalProxy`). Ryujinx runs the same
// HandleExternalProxy on the receive thread (NetCoreServer.OnReceived); we
// approximate that by spawning a dedicated worker once per ExternalProxy.
alignas(os::ThreadStackAlignment) static u8 g_p2p_connect_thread_stack[0x4000];

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
        LOG_WARN("SendProxyDataCallback: g_active_ldn_service=nullptr, src=0x%08X:%u dst=0x%08X:%u",
                 source_ip, source_port, dest_ip, dest_port);
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
            LOG_WARN("SendProxyDataCallback: unsupported protocol=%d", static_cast<int>(protocol));
            return false;
    }

    header.data_length = static_cast<uint32_t>(data_len);

    // Send via the LDN client
    auto result = g_active_ldn_service->SendProxyDataToServer(header, data, data_len);
    if (result != ryu_ldn::network::ClientOpResult::Success) {
        LOG_WARN("SendProxyDataCallback: SendProxyDataToServer failed: %s (src=0x%08X:%u dst=0x%08X:%u len=%zu)",
                 ryu_ldn::network::client_op_result_to_string(result),
                 source_ip, source_port, dest_ip, dest_port, data_len);
    }
    return result == ryu_ldn::network::ClientOpResult::Success;
}

// Inbound data plane for the P2P host: P2pProxyServer hands us each ProxyData
// payload that targets either the broadcast address or the host's vIP, so we
// can deliver it to the same BSD MITM sink relay-mode ProxyData uses
// (ProxySocketManager::RouteIncomingData). This is the in-process replacement
// for Ryujinx's "host dials its own P2pProxyServer over TCP" loopback, which
// we deliberately skip on Switch (TCP loopback for talking to ourselves costs
// +1 connection, +2 worker threads, +128 KB recv buffers and +2 bsd:s IPC
// sessions — too much for the sysmodule budget).
static void HostP2pInboundDataCallback(uint32_t source_ip, uint16_t source_port,
                                       uint32_t dest_ip, uint16_t dest_port,
                                       ryu_ldn::protocol::ProtocolType protocol,
                                       const uint8_t* data, size_t data_len,
                                       void* /*user_data*/) {
    ryu_ldn::bsd::ProtocolType bsd_protocol;
    switch (protocol) {
        case ryu_ldn::protocol::ProtocolType::Tcp: bsd_protocol = ryu_ldn::bsd::ProtocolType::Tcp; break;
        case ryu_ldn::protocol::ProtocolType::Udp: bsd_protocol = ryu_ldn::bsd::ProtocolType::Udp; break;
        default:
            LOG_WARN("HostP2pInboundDataCallback: unsupported protocol=%u",
                     static_cast<unsigned>(protocol));
            return;
    }
    auto& socket_manager = mitm::bsd::ProxySocketManager::GetInstance();
    socket_manager.RouteIncomingData(source_ip, source_port,
                                     dest_ip,   dest_port,
                                     bsd_protocol,
                                     data, data_len);
}

// Verify struct sizes match Nintendo's expectations
static_assert(sizeof(NetworkInfo) == 0x480, "sizeof(NetworkInfo) should be 0x480");
static_assert(sizeof(ConnectNetworkData) == 0x7C, "sizeof(ConnectNetworkData) should be 0x7C");
static_assert(sizeof(ScanFilter) == 0x60, "sizeof(ScanFilter) should be 0x60");

ICommunicationService::ICommunicationService(ncm::ProgramId program_id)
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
    , m_response_event(os::EventClearMode_AutoClear)
    , m_scan_event(os::EventClearMode_AutoClear)
    , m_error_event(os::EventClearMode_ManualClear)
    , m_reject_event(os::EventClearMode_AutoClear)
    , m_handshake_event(os::EventClearMode_AutoClear)
    , m_last_response_id(ryu_ldn::protocol::PacketId::Initialize)
    , m_scan_results{}
    , m_scan_result_count(0)
    , m_advertise_data{}
    , m_advertise_data_size(0)
    , m_game_version{}
    , m_network_connected(false)
    , m_prev_node_connected{}
    , m_last_network_error(ryu_ldn::protocol::NetworkErrorCode::None)
    , m_use_p2p_proxy(!ryu_ldn::ipc::g_config.ldn.disable_p2p)
    , m_proxy_config{}
    , m_external_proxy_config{}
    , m_p2p_client(nullptr)
    , m_p2p_server(nullptr)
    , m_inactivity_timeout(NetworkTimeout::DEFAULT_IDLE_TIMEOUT_MS, &ICommunicationService::OnInactivityTimeout)
    , m_recv_thread{}
    , m_recv_thread_running(false)
    , m_shared_mutex{}
    , m_program_id(program_id)
    , m_local_communication_id(0)
    , m_expected_scene_id(0)
{
    LOG_INFO("ICommunicationService created with program_id=0x%016lx", m_program_id.value);

    // Use program_id as LocalCommunicationId
    // NOTE: Technically LocalCommunicationId can differ from program_id (stored in NACP),
    // but reading NACP via nsGetApplicationControlData() causes deadlocks in MITM context.
    // For most games, program_id == LocalCommunicationId, and the server will accept either.
    m_local_communication_id = m_program_id.value;
    LOG_INFO("LocalCommunicationId: 0x%016lx (using program_id)", m_local_communication_id);

    // Configure packet callback to receive server responses
    // Use static callback with user_data to route to instance method
    m_server_client.set_packet_callback(
        [](ryu_ldn::protocol::PacketId id, const uint8_t* data, size_t size, void* user_data) {
            auto* self = static_cast<ICommunicationService*>(user_data);
            self->HandleServerPacket(id, data, size);
        },
        this
    );

    // Configure state callback so the receive thread can signal
    // m_handshake_event when RyuLdnClient reaches Ready state.
    // This replaces the old polling loop in ConnectToServer().
    m_server_client.set_state_callback(
        [](ryu_ldn::network::ConnectionState /*old_state*/,
           ryu_ldn::network::ConnectionState new_state,
           void* user_data) {
            auto* self = static_cast<ICommunicationService*>(user_data);
            if (new_state == ryu_ldn::network::ConnectionState::Ready) {
                self->m_handshake_event.Signal();
            }
        },
        this
    );

    // Start dedicated receive thread for asynchronous packet dispatch.
    // This mirrors Ryujinx's NetCoreServer pattern: packets are received and
    // dispatched immediately on a background thread, while IPC handlers wait
    // on os::Event objects (via TimedWaitAny) for specific responses.
    m_recv_thread_running = true;
    R_ABORT_UNLESS(os::CreateThread(
        &m_recv_thread,
        ReceiveThreadEntry,
        this,
        g_background_thread_stack,
        sizeof(g_background_thread_stack),
        16  // Higher priority than before (lower number = higher priority)
            // so receive thread preempts IPC handlers for lower latency
    ));
    os::SetThreadNamePointer(&m_recv_thread, "ldn_recv");
    os::StartThread(&m_recv_thread);
}

ICommunicationService::~ICommunicationService() {
    LOG_INFO("ICommunicationService destructor called (state=%s)",
             LdnStateMachine::StateToString(m_state_machine.GetState()));

    // Stop receive thread first
    m_recv_thread_running = false;
    // Signal error event to unblock receive thread if it's waiting
    m_error_event.Signal();
    os::WaitThread(&m_recv_thread);
    os::DestroyThread(&m_recv_thread);

    // Wait for any in-flight async P2P connect worker before tearing down
    // m_p2p_client / m_external_proxy_config underneath it.
    if (m_p2p_connect_thread_initialized) {
        os::WaitThread(&m_p2p_connect_thread);
        os::DestroyThread(&m_p2p_connect_thread);
        m_p2p_connect_thread_initialized = false;
    }

    // Stop P2P server if hosting
    StopP2pProxyServer();
    // Ensure P2P proxy client is disconnected
    DisconnectP2pProxy();
    // Ensure server is disconnected
    DisconnectFromServer();

    // NOTE: Do NOT clear LDN PID here!
    // The game may open BSD sockets between LDN sessions (e.g., after connection
    // failure and retry). If we clear the PID here, BSD MITM won't intercept
    // those sockets. The PID remains set for the lifetime of the game process.
    // When the game closes, the PID becomes stale but harmless (new processes
    // will have different PIDs).
}

// ============================================================================
// Server Connection Helpers
// ============================================================================

Result ICommunicationService::ConnectToServer() {
    // Check if already connected and connection is still alive
    if (m_server_connected && m_server_client.is_ready()) {
        LOG_VERBOSE("Already connected to server");
        R_SUCCEED();
    }

    // If m_server_connected is true but connection died, clean up first
    if (m_server_connected && !m_server_client.is_ready()) {
        LOG_INFO("Previous connection dead, reconnecting...");
        m_server_client.disconnect();
        m_server_connected = false;
    }

    LOG_INFO("Connecting to RyuLdn server...");

    // Attempt TCP connection.
    // RyuLdnClient::connect() starts the connection process and returns Success
    // if the attempt was initiated, even if the underlying TCP connection failed.
    // After connect() returns, check that the TCP socket is actually connected
    // before proceeding to the handshake wait — otherwise we wait 5 seconds for
    // a handshake that can never succeed.
    {
        auto result = m_server_client.connect();
        if (result != ryu_ldn::network::ClientOpResult::Success) {
            LOG_ERROR("Server connection initiation failed: %s",
                      ryu_ldn::network::client_op_result_to_string(result));
            R_RETURN(MAKERESULT(0x10, 2)); // Connection initiation failed
        }

        // connect() returns Success even when try_connect() fails (wrong state
        // machine path, DNS failure, TCP refused, etc.). Verify TCP established.
        if (!m_server_client.is_connected()) {
            LOG_ERROR("TCP connection to server failed (state=%d)",
                      static_cast<int>(m_server_client.get_state()));
            R_RETURN(MAKERESULT(0x10, 2)); // TCP connection failed
        }
    }

    // Wait for handshake to complete using event-driven wait.
    // The receive thread processes the Initialize response internally in
    // RyuLdnClient::update() and invokes the state callback when the state
    // machine transitions to Ready, which signals m_handshake_event.
    // This is analogous to Ryujinx's _connected.WaitOne(FailureTimeout).
    {
        constexpr uint64_t handshake_timeout_ms = 5000;

        LOG_VERBOSE("Waiting for handshake...");

        // Clear events before waiting
        m_handshake_event.Clear();
        m_error_event.Clear();

        os::MultiWaitType multi_wait;
        os::InitializeMultiWait(std::addressof(multi_wait));

        os::MultiWaitHolderType handshake_holder;
        os::InitializeMultiWaitHolder(std::addressof(handshake_holder), m_handshake_event.GetBase());
        os::LinkMultiWaitHolder(std::addressof(multi_wait), std::addressof(handshake_holder));

        os::MultiWaitHolderType error_holder;
        os::InitializeMultiWaitHolder(std::addressof(error_holder), m_error_event.GetBase());
        os::LinkMultiWaitHolder(std::addressof(multi_wait), std::addressof(error_holder));

        os::MultiWaitHolderType* signaled = os::TimedWaitAny(
            std::addressof(multi_wait), TimeSpan::FromMilliSeconds(static_cast<s64>(handshake_timeout_ms)));

        bool handshake_ok = false;
        if (signaled == std::addressof(handshake_holder)) {
            // State callback signaled Ready state
            handshake_ok = m_server_client.is_ready();
            if (!handshake_ok) {
                LOG_ERROR("Handshake event signaled but client not in Ready state");
            }
        } else if (signaled == std::addressof(error_holder)) {
            LOG_ERROR("Error event signaled during handshake");
        }

        os::UnlinkAllMultiWaitHolder(std::addressof(multi_wait));
        os::FinalizeMultiWaitHolder(std::addressof(handshake_holder));
        os::FinalizeMultiWaitHolder(std::addressof(error_holder));
        os::FinalizeMultiWait(std::addressof(multi_wait));

        if (!handshake_ok) {
            // Check if already ready (race: handshake completed just before we waited)
            if (m_server_client.is_ready()) {
                LOG_VERBOSE("Handshake completed (checked after wait)");
            } else {
                LOG_ERROR("Handshake timeout or failed");
                m_server_client.disconnect();
                R_RETURN(MAKERESULT(0x10, 4)); // Handshake timeout
            }
        }
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

        // Reset the ProxySocketManager - clears all sockets, pending packets, callbacks
        // This prevents memory leaks when the game disconnects and reconnects
        mitm::bsd::ProxySocketManager::GetInstance().Reset();

        // Clean up abandoned BSD forward services (sessions that never got RegisterClient)
        // This is safe to do now because we're disconnecting from LDN
        mitm::bsd::BsdMitmService::CleanupAbandonedServices();

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

    // Re-set LDN PID to enable BSD MITM interception
    // This is critical for retry scenarios: after Finalize() clears the PID,
    // a subsequent Initialize() must re-enable BSD interception
    shared_state.SetLdnPid(m_client_process_id);

    LOG_VERBOSE("LDN Initialized successfully (LdnPid=%lu)", m_client_process_id);
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
    shared_state.SetLdnPid(0);  // Clear so BSD MITM stops intercepting
    LOG_INFO("Finalize: cleared LDN PID");

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
    // No update() call needed — the receive thread processes packets
    // asynchronously and updates state via HandleServerPacket + events.

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
    // No update() call needed — the receive thread processes packets
    // asynchronously and updates m_network_info via HandleServerPacket.

    // Take a snapshot under the shared mutex — the receive thread may
    // be writing m_network_info right now from a SyncNetwork packet.
    {
        std::scoped_lock lock(m_shared_mutex);
        buffer.SetValue(m_network_info);
    }
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
    std::scoped_lock lock(m_shared_mutex);
    NetworkInfo2SecurityParameter(&m_network_info, &param);
    out.SetValue(param);
    R_SUCCEED();
}

Result ICommunicationService::GetNetworkConfig(ams::sf::Out<NetworkConfig> out) {
    NetworkConfig config;
    std::scoped_lock lock(m_shared_mutex);
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
    LOG_INFO("GetNetworkInfoLatestUpdate() called, node_count=%u, update_buf_size=%zu",
             m_network_info.ldn.nodeCount, pUpdates.GetSize());

    // Snapshot m_network_info under the shared mutex
    {
        std::scoped_lock lock(m_shared_mutex);
        buffer.SetValue(m_network_info);
    }

    // Report node state changes since last call
    if (pUpdates.GetSize() > 0) {
        NodeLatestUpdate* updates = pUpdates.GetPointer();
        size_t update_count = std::min(pUpdates.GetSize(), static_cast<size_t>(NodeCountMax));

        for (size_t i = 0; i < update_count; i++) {
            bool current_connected = (i < NodeCountMax) && m_network_info.ldn.nodes[i].isConnected;
            bool prev_connected = m_prev_node_connected[i];

            if (current_connected && !prev_connected) {
                updates[i].stateChange = static_cast<u8>(NodeStateChange::Connect);
                LOG_INFO("  Node[%zu]: stateChange=Connect (was disconnected, now connected)", i);
            } else if (!current_connected && prev_connected) {
                updates[i].stateChange = static_cast<u8>(NodeStateChange::Disconnect);
                LOG_INFO("  Node[%zu]: stateChange=Disconnect (was connected, now disconnected)", i);
            } else {
                updates[i].stateChange = static_cast<u8>(NodeStateChange::None);
            }
            std::memset(updates[i]._unk, 0, sizeof(updates[i]._unk));

            // Update previous state for next call
            m_prev_node_connected[i] = current_connected;
        }

        // Zero remaining entries if buffer is larger than NodeCountMax
        for (size_t i = update_count; i < pUpdates.GetSize(); i++) {
            std::memset(&updates[i], 0, sizeof(NodeLatestUpdate));
        }
    }

    R_SUCCEED();
}

Result ICommunicationService::Scan(
    ams::sf::Out<u32> count,
    ams::sf::OutAutoSelectArray<NetworkInfo> buffer,
    u16 channel,
    const ScanFilter &filter)
{
    AMS_UNUSED(channel);

    // Replace LocalCommunicationId=-1 or 0 with real LocalCommunicationId from NACP
    // Nintendo SDK does this internally, but we intercept before that happens
    // See Ryujinx NeedsRealId handling - uses NACP LocalCommunicationId[0], not program_id
    u64 local_comm_id = filter.networkId.intentId.localCommunicationId;
    if (local_comm_id == static_cast<u64>(-1) || local_comm_id == 0) {
        local_comm_id = m_local_communication_id;
        LOG_INFO("Scan() replacing local_comm_id with NACP LocalCommunicationId=0x%016lx", local_comm_id);
    }

    LOG_INFO("Scan() called, local_comm_id=0x%016lx, scene_id=%u, flags=0x%x, networkType=0x%x",
             local_comm_id, filter.networkId.intentId.sceneId, filter.flag, filter.networkType);

    // Debug: dump raw filter bytes to understand what the game sends
    {
        const uint8_t* raw = reinterpret_cast<const uint8_t*>(&filter);
        LOG_INFO("ScanFilter raw[0-31]: %08X %08X %08X %08X %08X %08X %08X %08X",
                 *reinterpret_cast<const uint32_t*>(raw + 0),
                 *reinterpret_cast<const uint32_t*>(raw + 4),
                 *reinterpret_cast<const uint32_t*>(raw + 8),
                 *reinterpret_cast<const uint32_t*>(raw + 12),
                 *reinterpret_cast<const uint32_t*>(raw + 16),
                 *reinterpret_cast<const uint32_t*>(raw + 20),
                 *reinterpret_cast<const uint32_t*>(raw + 24),
                 *reinterpret_cast<const uint32_t*>(raw + 28));
        LOG_INFO("ScanFilter raw[32-63]: %08X %08X %08X %08X %08X %08X %08X %08X",
                 *reinterpret_cast<const uint32_t*>(raw + 32),
                 *reinterpret_cast<const uint32_t*>(raw + 36),
                 *reinterpret_cast<const uint32_t*>(raw + 40),
                 *reinterpret_cast<const uint32_t*>(raw + 44),
                 *reinterpret_cast<const uint32_t*>(raw + 48),
                 *reinterpret_cast<const uint32_t*>(raw + 52),
                 *reinterpret_cast<const uint32_t*>(raw + 56),
                 *reinterpret_cast<const uint32_t*>(raw + 60));
    }

    R_UNLESS(IsServerConnected(), MAKERESULT(0x10, 2)); // Not connected

    // Reset scan results buffer and events (like Ryujinx _availableGames.Clear() and _scan.Reset())
    m_scan_result_count = 0;
    std::memset(m_scan_results, 0, sizeof(m_scan_results));
    m_scan_event.Clear();
    m_error_event.Clear();

    // Build scan filter for server
    // Convert from ams::mitm::ldn::ScanFilter to ryu_ldn::protocol::ScanFilterFull
    ryu_ldn::protocol::ScanFilterFull scan_filter{};

    // Force LocalCommunicationId filter to ensure we only receive rooms for this game
    // Some games don't set this flag, causing the server to return ALL rooms from ALL games
    scan_filter.flag = filter.flag | ScanFilterFlag_LocalCommunicationId;
    scan_filter.network_type = filter.networkType;

    // Copy network ID (use potentially replaced local_comm_id)
    scan_filter.network_id.intent_id.local_communication_id = local_comm_id;
    scan_filter.network_id.intent_id.scene_id = filter.networkId.intentId.sceneId;

    // Store expected scene_id - Ryujinx may create networks with sceneId=0
    // We need to fix this in Connected/SyncNetwork handlers so the game sees its expected sceneId
    m_expected_scene_id = filter.networkId.intentId.sceneId;
    LOG_INFO("Scan: storing expected scene_id=%u for NetworkInfo correction", m_expected_scene_id);

    // SessionId is stored as a 16-byte blob
    std::memcpy(scan_filter.network_id.session_id.data, &filter.networkId.sessionId, 16);

    // Copy SSID
    scan_filter.ssid.length = filter.ssid.length;
    std::memcpy(scan_filter.ssid.name, filter.ssid.raw, sizeof(scan_filter.ssid.name));

    // Copy MAC address (BSSID)
    std::memcpy(scan_filter.mac_address.data, filter.bssid.raw, sizeof(scan_filter.mac_address.data));

    LOG_INFO("Scan: sending to server with flag=0x%x, local_comm_id=0x%016llx",
             scan_filter.flag,
             static_cast<unsigned long long>(scan_filter.network_id.intent_id.local_communication_id));

    // Send scan request
    auto send_result = m_server_client.send_scan(scan_filter);
    if (send_result != ryu_ldn::network::ClientOpResult::Success) {
        LOG_ERROR("Scan: send failed");
        count.SetValue(0);
        R_RETURN(MAKERESULT(0x10, 3)); // Send failed
    }

    LOG_INFO("Scan: sent request, waiting for ScanReplyEnd...");

    // Wait for scan completion using event-driven wait (like Ryujinx's
    // WaitHandle.WaitAny([_scan, _error], ScanTimeout)). The receive thread
    // calls HandleServerPacket which signals m_scan_event on ScanReplyEnd
    // and m_error_event on NetworkError.
    os::MultiWaitType multi_wait;
    os::InitializeMultiWait(std::addressof(multi_wait));

    os::MultiWaitHolderType scan_holder;
    os::InitializeMultiWaitHolder(std::addressof(scan_holder), m_scan_event.GetBase());
    os::LinkMultiWaitHolder(std::addressof(multi_wait), std::addressof(scan_holder));

    os::MultiWaitHolderType error_holder;
    os::InitializeMultiWaitHolder(std::addressof(error_holder), m_error_event.GetBase());
    os::LinkMultiWaitHolder(std::addressof(multi_wait), std::addressof(error_holder));

    os::MultiWaitHolderType* signaled = os::TimedWaitAny(
        std::addressof(multi_wait), TimeSpan::FromMilliSeconds(1000));

    bool scan_complete = false;
    bool error_received = false;

    if (signaled == std::addressof(scan_holder)) {
        scan_complete = true;
    } else if (signaled == std::addressof(error_holder)) {
        error_received = true;
    }

    os::UnlinkAllMultiWaitHolder(std::addressof(multi_wait));
    os::FinalizeMultiWaitHolder(std::addressof(scan_holder));
    os::FinalizeMultiWaitHolder(std::addressof(error_holder));
    os::FinalizeMultiWait(std::addressof(multi_wait));

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
    LOG_INFO("OpenAccessPoint() called (state before=%s)",
             LdnStateMachine::StateToString(m_state_machine.GetState()));

    auto result = m_state_machine.OpenAccessPoint();
    if (result != StateTransitionResult::Success) {
        LOG_INFO("OpenAccessPoint: state transition failed: %s",
                 LdnStateMachine::ResultToString(result));
    }
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    LOG_INFO("OpenAccessPoint: state transitioned to AccessPoint");

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

Result ICommunicationService::CreateNetwork(const CreateNetworkConfig &data) {
    // Replace LocalCommunicationId=-1 with real LocalCommunicationId from NACP
    // See Ryujinx NeedsRealId handling - uses NACP LocalCommunicationId[0], not program_id
    u64 local_comm_id = data.networkConfig.intentId.localCommunicationId;
    if (local_comm_id == static_cast<u64>(-1) || local_comm_id == 0) {
        local_comm_id = m_local_communication_id;
        LOG_INFO("CreateNetwork() replacing local_comm_id with NACP LocalCommunicationId=0x%016lx", local_comm_id);
    }

    LOG_INFO("CreateNetwork called, local_comm_id=0x%016lx (state before=%s)",
             local_comm_id,
             LdnStateMachine::StateToString(m_state_machine.GetState()));

    R_UNLESS(IsServerConnected(), MAKERESULT(0x10, 2)); // Not connected

    // Mirror Ryujinx LdnMasterProxyClient.CreateNetwork: disable the inactivity
    // timeout *before* doing any work that may take a few seconds. The whole
    // CreateAccessPoint round-trip (UPnP NatPunch + send + WaitForResponse) can
    // straddle the 6 s NetworkTimeout window, and if we wait until after the
    // Connected response to call DisableTimeout(), OnInactivityTimeout fires
    // mid-CreateNetwork, sees `m_network_connected == false` (we set it later
    // in this same function), and disconnects the master TCP — leaving the
    // sysmodule with an "AccessPointCreated" state but no master link.
    m_inactivity_timeout.DisableTimeout();

    auto result = m_state_machine.CreateNetwork();
    if (result != StateTransitionResult::Success) {
        LOG_INFO("CreateNetwork: state transition refused: %s (state=%s)",
                 LdnStateMachine::ResultToString(result),
                 LdnStateMachine::StateToString(m_state_machine.GetState()));
    }
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));
    LOG_INFO("CreateNetwork: state transitioned to AccessPointCreated, building request");

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

    // Network config (use potentially replaced local_comm_id)
    request.network_config.intent_id.local_communication_id = local_comm_id;
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
    LOG_INFO("CreateNetwork: about to evaluate P2P proxy (m_use_p2p_proxy=%d)",
             static_cast<int>(m_use_p2p_proxy));
    if (m_use_p2p_proxy && StartP2pProxyServer()) {
        LOG_INFO("CreateNetwork: P2P proxy started successfully, calling NatPunch()");
        // Attempt UPnP NAT punch to open public port
        uint16_t public_port = m_p2p_server->NatPunch();
        LOG_INFO("CreateNetwork: NatPunch returned public_port=%u", public_port);

        // Fill RyuNetworkConfig with P2P port information
        // Like Ryujinx: request.PrivateIp = GetLocalIPv4(), request.ExternalProxyPort = public_port
        LOG_INFO("CreateNetwork: calling GetLocalIPv4()");
        uint32_t local_ip = p2p::UpnpPortMapper::GetInstance().GetLocalIPv4();
        LOG_INFO("CreateNetwork: GetLocalIPv4 returned 0x%08X", local_ip);

        // Store local IP as 16-byte buffer (first 4 bytes for IPv4).
        //
        // Wire format: network byte order (big-endian), to match what Ryujinx
        // sends — its LdnMasterProxyClient does
        //   `unicastAddress.Address.GetAddressBytes().AsSpan().CopyTo(request.PrivateIp.AsSpan());`
        // and IPAddress.GetAddressBytes() returns big-endian. The receiver
        // (sysmodule HandleExternalProxyConnect or another Ryujinx joiner)
        // memcpys the bytes straight into sockaddr_in::sin_addr.s_addr,
        // which itself expects network byte order — so omitting the htonl
        // here made the host advertise its own IPv4 with the octets
        // reversed (192.168.1.25 → 25.1.168.192) and the joiner timed out
        // trying to connect.
        const uint32_t local_ip_net = htonl(local_ip);
        std::memset(request.ryu_network_config.private_ip, 0, sizeof(request.ryu_network_config.private_ip));
        std::memcpy(request.ryu_network_config.private_ip, &local_ip_net, sizeof(local_ip_net));

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

    // Send to server. Mirror Ryujinx LdnMasterProxyClient.CreateNetwork —
    // the wire payload is `request | advertiseData[N]`. The game called
    // SetAdvertiseData() before CreateNetwork(), but at that point
    // m_network_connected was false so the SetAdvertiseData IPC didn't
    // forward anything to the server. We carry the buffered data here so
    // NetworkInfo.advertiseDataSize comes back populated and the host's
    // lobby finishes loading instead of hanging.
    auto send_result = m_server_client.send_create_access_point(
        request,
        m_advertise_data_size > 0 ? m_advertise_data : nullptr,
        m_advertise_data_size);
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

    // Mirror upstream CreateNetworkCommon (LdnMasterProxyClient.cs:452-457):
    //   if (!_useP2pProxy && _hostedProxy != null) {
    //       Logger.Warning("Locally hosted proxy server was not externally reachable.
    //                       Proxying through the master server instead.");
    //       DisposeProxy();
    //   }
    // The HandleNetworkError path on the master TCP recv thread only flips
    // m_use_p2p_proxy (matches upstream HandleNetworkError); the actual
    // proxy teardown happens here on the IPC thread once we've cleared the
    // Connected handshake. Doing it on master TCP recv would block the
    // master's read loop on os::WaitThread for the accept thread to exit.
    if (!m_use_p2p_proxy && m_p2p_server != nullptr) {
        LOG_WARN("CreateNetwork: locally hosted proxy was not externally reachable, falling back to relay");
        StopP2pProxyServer();
    }

    // Predict the virtual LDN IP for the host so the game's bind(INADDR_ANY)
    // can be rewritten immediately, even if the P2P async worker hasn't yet
    // received its ProxyConfig. The Ryujinx server's VirtualDhcp always
    // returns `baseAddress + 1` for the first allocated IP (see
    // VirtualDhcp.cs : `_nextIp = baseAddress + 1` then RequestIpV4 returns
    // _nextIp on first call), and the host is *always* the first node added
    // to its own game (HostedGame.Connect right after SetOwner).
    //
    // Setting these here, instead of waiting on m_p2p_connect_thread_active,
    // mirrors the timing of the RELAY mode where the master server pushes
    // ProxyConfig before Connected and ICommunicationService::CreateNetwork
    // returns to the game in ~100 ms. Blocking the IPC for 1–4 s on the P2P
    // worker made the game stop polling GetState/GetNetworkInfo after
    // CreateNetwork — the loading screen on MK8DX never advanced. The P2P
    // worker still runs and overwrites these fields with the real config
    // when its handshake completes; the values match anyway.
    constexpr uint32_t kPredictedHostIp     = 0x0A720001;  // 10.114.0.1
    constexpr uint32_t kPredictedSubnetMask = 0xFFFF0000;  // /16
    if (m_use_p2p_proxy && m_ipv4_address == 0) {
        m_ipv4_address = kPredictedHostIp;
        m_subnet_mask  = kPredictedSubnetMask;
        m_proxy_config.proxy_ip          = kPredictedHostIp;
        m_proxy_config.proxy_subnet_mask = kPredictedSubnetMask;

        auto& socket_manager = mitm::bsd::ProxySocketManager::GetInstance();
        socket_manager.SetLocalIp(kPredictedHostIp);

        LOG_INFO("CreateNetwork: pre-set host virtual IP 0x%08X (P2P async will confirm)",
                 kPredictedHostIp);
    }

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
    LOG_INFO("OpenStation() called");
    auto result = m_state_machine.OpenStation();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    LOG_INFO("OpenStation: state transitioned to Station");

    // Connect to RyuLdn server
    Result rc = ConnectToServer();
    if (R_FAILED(rc)) {
        // Rollback state on connection failure
        LOG_WARN("OpenStation: ConnectToServer failed, rolling back state");
        m_state_machine.CloseStation();
        R_RETURN(rc);
    }

    // Update shared state
    SharedState::GetInstance().SetLdnState(CommState::Station);
    LOG_INFO("OpenStation: completed successfully");

    R_SUCCEED();
}

Result ICommunicationService::CloseStation() {
    LOG_INFO("CloseStation() called");

    // NOTE: Do NOT disconnect from server here!
    // The game may call OpenStation/CloseStation many times during scanning.
    // Disconnecting each time hits the server's firewall rate limit (10/min).
    // Keep the connection alive - NetworkTimeout will disconnect after 6s of inactivity.
    // This matches Ryujinx behavior where connection persists until Finalize().

    auto result = m_state_machine.CloseStation();
    R_UNLESS(result == StateTransitionResult::Success, MAKERESULT(0x10, 1));

    // Clear network info
    std::memset(&m_network_info, 0, sizeof(m_network_info));

    // Update shared state
    SharedState::GetInstance().SetLdnState(CommState::Initialized);

    LOG_INFO("CloseStation: state transitioned to Initialized");

    R_SUCCEED();
}

Result ICommunicationService::Connect(const ConnectNetworkData &dat, const NetworkInfo& data) {
    // Replace LocalCommunicationId=-1 with real LocalCommunicationId from NACP
    // See Ryujinx NeedsRealId handling - uses NACP LocalCommunicationId[0], not program_id
    u64 local_comm_id = data.networkId.intentId.localCommunicationId;
    if (local_comm_id == static_cast<u64>(-1) || local_comm_id == 0) {
        local_comm_id = m_local_communication_id;
        LOG_INFO("Connect() replacing local_comm_id with NACP LocalCommunicationId=0x%016lx", local_comm_id);
    }

    // Log SessionId for debugging (server uses this to find the game)
    LOG_INFO("Connect called, local_comm_id=0x%016lx, session_id=%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
             local_comm_id,
             data.networkId.sessionId.raw[0], data.networkId.sessionId.raw[1],
             data.networkId.sessionId.raw[2], data.networkId.sessionId.raw[3],
             data.networkId.sessionId.raw[4], data.networkId.sessionId.raw[5],
             data.networkId.sessionId.raw[6], data.networkId.sessionId.raw[7],
             data.networkId.sessionId.raw[8], data.networkId.sessionId.raw[9],
             data.networkId.sessionId.raw[10], data.networkId.sessionId.raw[11],
             data.networkId.sessionId.raw[12], data.networkId.sessionId.raw[13],
             data.networkId.sessionId.raw[14], data.networkId.sessionId.raw[15]);
    LOG_INFO("Connect: userName='%.32s', version=%u",
             dat.userConfig.userName, dat.localCommunicationVersion);

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

    // Network info - copy the full structure, then fix LocalCommunicationId
    std::memcpy(&request.network_info, &data, sizeof(request.network_info));
    request.network_info.network_id.intent_id.local_communication_id = local_comm_id;

    // Send Connect request (12-byte header)
    LOG_INFO("Connect: sending request...");

    auto send_result = m_server_client.send_connect(request);
    if (send_result != ryu_ldn::network::ClientOpResult::Success) {
        LOG_ERROR("Connect: failed to send request: %s",
                  ryu_ldn::network::client_op_result_to_string(send_result));
        m_state_machine.Disconnect();
        R_RETURN(MAKERESULT(0x10, 5));
    }

    LOG_INFO("Connect: sent request, waiting for Connected response...");
    if (!WaitForResponse(ryu_ldn::protocol::PacketId::Connected, 5000)) {
        LOG_ERROR("Connect: did not receive Connected response from server");
        m_state_machine.Disconnect();
        R_RETURN(MAKERESULT(0x10, 5)); // Response timeout
    }

    LOG_INFO("Connect: received Connected response, connected to network");

    // Mark as connected to network and disable inactivity timeout (like Ryujinx)
    m_network_connected = true;
    m_inactivity_timeout.DisableTimeout();

    // Note: m_network_info is already set by the Connected packet handler
    // with the updated node list from the server. Don't overwrite it with
    // the scan results passed by the game (which only has node_count=1).

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
        const ScanFilter &filter) {
    // ScanPrivate is the same as Scan but for private networks
    // The filter behavior is slightly different (doesn't mask BSSID flag)
    return Scan(count, buffer, channel, filter);
}

Result ICommunicationService::CreateNetworkPrivate(
        const CreateNetworkPrivateConfig &data,
        ams::sf::InPointerBuffer addressList) {
    LOG_INFO("CreateNetworkPrivate called (state before=%s, addr_list_size=%zu)",
             LdnStateMachine::StateToString(m_state_machine.GetState()),
             addressList.GetSize());

    R_UNLESS(IsServerConnected(), MAKERESULT(0x10, 2)); // Not connected

    // Same reason as CreateNetwork: disable the inactivity timeout up front
    // so OnInactivityTimeout doesn't fire mid-handshake and tear down the
    // master TCP while m_network_connected is still false.
    m_inactivity_timeout.DisableTimeout();

    auto result = m_state_machine.CreateNetwork();
    if (result != StateTransitionResult::Success) {
        LOG_INFO("CreateNetworkPrivate: state transition refused: %s",
                 LdnStateMachine::ResultToString(result));
    }
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

Result ICommunicationService::ConnectPrivate(const ConnectPrivateData &data) {
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

    // Wait for RejectReply from server using event-driven wait
    // (like Ryujinx: WaitHandle.WaitAny([_reject, _error], InactiveTimeout))
    {
        constexpr uint64_t reject_timeout_ms = 6000;  // InactiveTimeout (was 4000 FailureTimeout)

        os::MultiWaitType multi_wait;
        os::InitializeMultiWait(std::addressof(multi_wait));

        os::MultiWaitHolderType reject_holder;
        os::InitializeMultiWaitHolder(std::addressof(reject_holder), m_reject_event.GetBase());
        os::LinkMultiWaitHolder(std::addressof(multi_wait), std::addressof(reject_holder));

        os::MultiWaitHolderType error_holder;
        os::InitializeMultiWaitHolder(std::addressof(error_holder), m_error_event.GetBase());
        os::LinkMultiWaitHolder(std::addressof(multi_wait), std::addressof(error_holder));

        os::MultiWaitHolderType* signaled = os::TimedWaitAny(
            std::addressof(multi_wait), TimeSpan::FromMilliSeconds(static_cast<s64>(reject_timeout_ms)));

        if (signaled == std::addressof(reject_holder)) {
            LOG_INFO("Reject: received RejectReply");
            if (ConsumeNetworkError() != ryu_ldn::protocol::NetworkErrorCode::None) {
                os::UnlinkAllMultiWaitHolder(std::addressof(multi_wait));
                os::FinalizeMultiWaitHolder(std::addressof(reject_holder));
                os::FinalizeMultiWaitHolder(std::addressof(error_holder));
                os::FinalizeMultiWait(std::addressof(multi_wait));
                R_RETURN(MAKERESULT(0x10, 4)); // InvalidState due to error
            }
            os::UnlinkAllMultiWaitHolder(std::addressof(multi_wait));
            os::FinalizeMultiWaitHolder(std::addressof(reject_holder));
            os::FinalizeMultiWaitHolder(std::addressof(error_holder));
            os::FinalizeMultiWait(std::addressof(multi_wait));
            R_SUCCEED();
        }

        if (signaled == std::addressof(error_holder)) {
            LOG_ERROR("Reject: error received");
        }

        os::UnlinkAllMultiWaitHolder(std::addressof(multi_wait));
        os::FinalizeMultiWaitHolder(std::addressof(reject_holder));
        os::FinalizeMultiWaitHolder(std::addressof(error_holder));
        os::FinalizeMultiWait(std::addressof(multi_wait));
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

    // Lock shared state for the entire handler — we write m_network_info,
    // m_network_connected, m_scan_results, m_last_response_id, etc.
    // Event Signal() calls are safe outside this lock (kernel handles
    // synchronization internally). We hold the lock briefly to avoid
    // contention with IPC readers like GetNetworkInfo.
    std::scoped_lock lock(m_shared_mutex);

    switch (id) {
        case ryu_ldn::protocol::PacketId::Connected: {
            HandleConnectedPacket(data, size);
            break;
        }

        case ryu_ldn::protocol::PacketId::SyncNetwork: {
            HandleSyncNetworkPacket(data, size);
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
            HandleNetworkErrorPacket(data, size);
            break;
        }

        case ryu_ldn::protocol::PacketId::ScanReply: {
            HandleScanReplyPacket(data, size);
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

                // CRITICAL: Update m_ipv4_address and m_subnet_mask!
                // These are used by FindLocalNodeId() to identify our node in NetworkInfo.
                // Keep in Ryujinx format (big-endian uint32) - same format as:
                // - GetIpv4Address() returns to the game
                // - NetworkInfo.nodes[].ipv4Address (no longer bswapped)
                // - ProxySocketManager local IP
                m_ipv4_address = config->proxy_ip;
                m_subnet_mask = config->proxy_subnet_mask;

                LOG_INFO("Received ProxyConfig: ip=0x%08X, mask=0x%08X",
                         m_ipv4_address, m_subnet_mask);

                // Set local IP in ProxySocketManager for BSD MITM
                // This is used when creating proxy sockets for INADDR_ANY binds
                // Keep in Ryujinx format for ProxySocketManager (it expects Ryujinx format)
                auto& socket_manager = mitm::bsd::ProxySocketManager::GetInstance();
                socket_manager.SetLocalIp(config->proxy_ip);
                LOG_INFO("ProxySocketManager: local IP set to 0x%08X", config->proxy_ip);
            }
            break;
        }

        case ryu_ldn::protocol::PacketId::ExternalProxy: {
            HandleExternalProxyPacket(data, size);
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

        case ryu_ldn::protocol::PacketId::ExternalProxyState: {
            // Master tells the host that a joiner's connection state changed.
            // Mirrors Ryujinx P2pProxyServer.HandleStateChange — when
            // Connected=false we have to drop the matching waiting token
            // (so the slot doesn't stay reserved for someone who never
            // showed up) and disconnect any session already authenticated
            // under that vIP. Without this handler, a joiner that fails
            // its TCP-side auth on us leaves the master holding a stale
            // _players entry for them while we keep the token forever,
            // and the next CreateNetwork retry can't reuse the slot.
            if (size >= sizeof(ryu_ldn::protocol::ExternalProxyConnectionState)) {
                const auto* state = reinterpret_cast<const ryu_ldn::protocol::ExternalProxyConnectionState*>(data);
                LOG_INFO("Received ExternalProxyState: vIP=0x%08X connected=%u",
                         state->ip_address, static_cast<unsigned>(state->connected));
                if (m_p2p_server != nullptr) {
                    m_p2p_server->HandleExternalProxyStateChange(state->ip_address,
                                                                 state->connected != 0);
                }
            }
            break;
        }

        case ryu_ldn::protocol::PacketId::ProxyData: {
            HandleProxyDataPacket(data, size);
            break;
        }

        default:
            LOG_VERBOSE("Unhandled packet type: %u", static_cast<unsigned>(id));
            break;
    }

    // Only signal m_response_event for packets that are responses to
    // explicit requests (Connected, ScanReply, ScanReplyEnd, RejectReply,
    // ProxyConnectReply, NetworkError). Push/async packets (ProxyConfig,
    // ProxyData, ExternalProxy, SyncNetwork, Ping) arrive unsolicited and
    // should not wake WaitForResponse — doing so causes spurious wake-ups
    // that can lead to false timeout errors when the expected response
    // arrives shortly after an async packet.
    const bool is_response = (id == ryu_ldn::protocol::PacketId::Connected ||
                               id == ryu_ldn::protocol::PacketId::ScanReply ||
                               id == ryu_ldn::protocol::PacketId::ScanReplyEnd ||
                               id == ryu_ldn::protocol::PacketId::RejectReply ||
                               id == ryu_ldn::protocol::PacketId::ProxyConnectReply ||
                               id == ryu_ldn::protocol::PacketId::NetworkError);
    if (is_response) {
        m_last_response_id = id;
        m_response_event.Signal();
    }
}



// ============================================================================
// Extracted Packet Handler Methods (from HandleServerPacket switch)
// ============================================================================

void ICommunicationService::HandleConnectedPacket(const uint8_t* data, size_t size) {
    if (size >= sizeof(ryu_ldn::protocol::NetworkInfo)) {
        const auto* net_info = reinterpret_cast<const ryu_ldn::protocol::NetworkInfo*>(data);

        // Copy to our local NetworkInfo (layout is compatible)
        std::memcpy(&m_network_info, net_info, sizeof(m_network_info));

        // NOTE: Keep node IPs in Ryujinx format (big-endian uint32, e.g., 10.114.0.1 = 0x0A720001).
        // GetIpv4Address() returns proxy_ip in Ryujinx format, so NetworkInfo nodes must
        // also be in Ryujinx format for the game to match its own IP in the player list.
        // DO NOT bswap - Ryujinx doesn't convert these either.

        // Fix sceneId - Ryujinx may create networks with sceneId=0 but the game
        // expects its original sceneId (stored during Scan). This mismatch can cause
        // the game to ignore UDP packets thinking they're from a different scene.
        if (m_expected_scene_id != 0 && m_network_info.networkId.intentId.sceneId != m_expected_scene_id) {
            LOG_INFO("Connected: fixing sceneId %u -> %u (Ryujinx compatibility)",
                     m_network_info.networkId.intentId.sceneId, m_expected_scene_id);
            m_network_info.networkId.intentId.sceneId = m_expected_scene_id;
        }

        // Also fix localCommunicationId if needed (like in Connect request)
        if (m_local_communication_id != 0 &&
            m_network_info.networkId.intentId.localCommunicationId != m_local_communication_id) {
            LOG_INFO("Connected: fixing localCommId 0x%016llX -> 0x%016lx",
                     m_network_info.networkId.intentId.localCommunicationId, m_local_communication_id);
            m_network_info.networkId.intentId.localCommunicationId = m_local_communication_id;
        }

        // Force a non-zero Wi-Fi channel — same trick as ldn_mitm's
        // LANDiscovery::createNetwork (`if (channel == 0) channel = 6`).
        // The Ryujinx server forwards `request.NetworkConfig.Channel`
        // verbatim and Switch games (e.g. MK8DX) sometimes pass 0
        // there. On real hardware the LDN service would never report
        // channel 0 back to the game, and the game refuses to leave
        // its "Création de partie privée" loading screen if it sees
        // channel 0 in the returned NetworkInfo.
        if (m_network_info.common.channel == 0) {
            LOG_INFO("Connected: fixing channel 0 -> 6 (LDN can't be on channel 0)");
            m_network_info.common.channel = 6;
        }

        // Set network connected flag (like Ryujinx _networkConnected = true)
        m_network_connected = true;

        LOG_INFO("Received Connected: node_count=%u, max=%u",
                 m_network_info.ldn.nodeCount,
                 m_network_info.ldn.nodeCountMax);

        // Debug: log NetworkInfo common fields
        LOG_INFO("  NetworkInfo.common: bssid=%02X:%02X:%02X:%02X:%02X:%02X, channel=%d",
                 m_network_info.common.bssid.raw[0], m_network_info.common.bssid.raw[1],
                 m_network_info.common.bssid.raw[2], m_network_info.common.bssid.raw[3],
                 m_network_info.common.bssid.raw[4], m_network_info.common.bssid.raw[5],
                 m_network_info.common.channel);
        LOG_INFO("  NetworkInfo.common: linkLevel=%d, netType=%u",
                 m_network_info.common.linkLevel, m_network_info.common.networkType);

        // Debug: log intent ID (localCommunicationId and sceneId)
        LOG_INFO("  NetworkInfo.networkId: localCommId=0x%016llX, sceneId=%u",
                 m_network_info.networkId.intentId.localCommunicationId,
                 m_network_info.networkId.intentId.sceneId);
        LOG_INFO("  NetworkInfo.ldn: advertiseDataSize=%u (max 384)",
                 m_network_info.ldn.advertiseDataSize);

        // Debug: log first 32 bytes of advertiseData if present
        if (m_network_info.ldn.advertiseDataSize > 0) {
            size_t dump_len = std::min<size_t>(m_network_info.ldn.advertiseDataSize, 32);
            char hex_buf[97] = {0};
            for (size_t j = 0; j < dump_len; j++) {
                snprintf(hex_buf + j*3, 4, "%02X ", m_network_info.ldn.advertiseData[j]);
            }
            LOG_INFO("  NetworkInfo.ldn.advertiseData[0-%zu]: %s", dump_len-1, hex_buf);
        }

        // Debug: log each node's info including MAC and username
        for (u8 i = 0; i < m_network_info.ldn.nodeCount && i < 8; i++) {
            const auto& node = m_network_info.ldn.nodes[i];
            LOG_INFO("  Node[%u]: ip=0x%08X, nodeId=%u, isConnected=%u, MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                     i, node.ipv4Address, node.nodeId, node.isConnected,
                     node.macAddress.raw[0], node.macAddress.raw[1], node.macAddress.raw[2],
                     node.macAddress.raw[3], node.macAddress.raw[4], node.macAddress.raw[5]);
            // Log first 16 chars of username (null-terminated)
            char username[17] = {0};
            std::memcpy(username, node.userName, 16);
            LOG_INFO("  Node[%u]: username='%s', localCommVer=%u",
                     i, username, node.localCommunicationVersion);
        }

        // Update session info in shared state
        auto& shared_state = SharedState::GetInstance();
        bool is_host = (m_network_info.ldn.nodes[0].isConnected &&
                       m_state_machine.GetState() == CommState::AccessPointCreated);
        shared_state.SetSessionInfo(
            m_network_info.ldn.nodeCount,
            m_network_info.ldn.nodeCountMax,
            FindLocalNodeId(),
            is_host
        );

        // Mirror Ryujinx HandleConnected: invoke NetworkChange event,
        // which over there hits AccessPoint.NetworkChanged → calls
        // `_parent.SetState(NetworkState.AccessPointCreated)` and that
        // signals the state-change event. The game's polling thread
        // wakes up *while* the CreateNetwork IPC is still in flight
        // and starts its GetState/GetNetworkInfo loop in time, so by
        // the time CreateNetwork returns the lobby flow is already
        // running. Without this, our lone signal at the end of
        // CreateNetwork landed too late on the P2P path (CreateNetwork
        // takes ~3 s because of the synchronous UPnP NatPunch) and
        // the game stopped polling — it stuck on the loading screen
        // even though state/info were correct. RELAY mode happened to
        // work only because CreateNetwork there returned in ~500 ms.
        m_state_machine.SignalStateChange();
    } else {
        LOG_ERROR("Connected packet too small: %zu < %zu",
                  size, sizeof(ryu_ldn::protocol::NetworkInfo));
    }
}

void ICommunicationService::HandleSyncNetworkPacket(const uint8_t* data, size_t size) {
    LOG_INFO("HandleServerPacket: SyncNetwork ENTRY (size=%zu)", size);
    ryu_ldn::debug::g_logger.flush();
    if (size >= sizeof(ryu_ldn::protocol::NetworkInfo)) {
        const auto* net_info = reinterpret_cast<const ryu_ldn::protocol::NetworkInfo*>(data);
        LOG_INFO("SyncNetwork step1: about to memcpy NetworkInfo (sizeof=%zu, src=%p, dst=%p)",
                 sizeof(m_network_info),
                 static_cast<const void*>(net_info),
                 static_cast<void*>(&m_network_info));
        ryu_ldn::debug::g_logger.flush();
        std::memcpy(&m_network_info, net_info, sizeof(m_network_info));
        LOG_INFO("SyncNetwork step2: memcpy done, node_count=%u, max=%u",
                 m_network_info.ldn.nodeCount,
                 m_network_info.ldn.nodeCountMax);
        ryu_ldn::debug::g_logger.flush();

        // Fix sceneId and localCommunicationId (same as Connected handler)
        if (m_expected_scene_id != 0 && m_network_info.networkId.intentId.sceneId != m_expected_scene_id) {
            m_network_info.networkId.intentId.sceneId = m_expected_scene_id;
        }
        if (m_local_communication_id != 0 &&
            m_network_info.networkId.intentId.localCommunicationId != m_local_communication_id) {
            m_network_info.networkId.intentId.localCommunicationId = m_local_communication_id;
        }
        LOG_INFO("SyncNetwork step3: scene/comm-id fix done");
        ryu_ldn::debug::g_logger.flush();

        // Update session info
        LOG_INFO("SyncNetwork step4: calling FindLocalNodeId()");
        ryu_ldn::debug::g_logger.flush();
        int local_id = FindLocalNodeId();
        LOG_INFO("SyncNetwork step4b: FindLocalNodeId() returned %d", local_id);
        ryu_ldn::debug::g_logger.flush();

        LOG_INFO("SyncNetwork step5: getting SharedState::GetInstance()");
        ryu_ldn::debug::g_logger.flush();
        auto& shared_state = SharedState::GetInstance();
        LOG_INFO("SyncNetwork step6: calling SetSessionInfo");
        ryu_ldn::debug::g_logger.flush();
        shared_state.SetSessionInfo(
            m_network_info.ldn.nodeCount,
            m_network_info.ldn.nodeCountMax,
            local_id,
            m_state_machine.GetState() == CommState::AccessPointCreated
        );
        LOG_INFO("SyncNetwork step7: SetSessionInfo done");
        ryu_ldn::debug::g_logger.flush();

        // Signal state change event so game knows network updated
        m_state_machine.SignalStateChange();
        LOG_INFO("SyncNetwork step8: SignalStateChange done");
        ryu_ldn::debug::g_logger.flush();
    }
}

void ICommunicationService::HandleExternalProxyPacket(const uint8_t* data, size_t size) {
    if (size >= sizeof(ryu_ldn::protocol::ExternalProxyConfig)) {
        const auto* config = reinterpret_cast<const ryu_ldn::protocol::ExternalProxyConfig*>(data);
        m_external_proxy_config = *config;

        LOG_INFO("Received ExternalProxy: port=%u, family=%u",
                 config->proxy_port, config->address_family);

        // Skip the self-loopback when this ExternalProxy targets our
        // own hosted P2pProxyServer. The Ryujinx server sends an
        // ExternalProxy to every player joining a P2P game including
        // the host (CreateAccessPoint -> game.Connect(this, myInfo) ->
        // InitExternalProxy -> session.SendAsync(ExternalProxy)).
        // Upstream PC Ryujinx then dials its own server over TCP
        // loopback so the host registers itself in `_players` and
        // shares the same data-plane code as joiners. On Switch we
        // have a cheaper shortcut: P2pProxyServer::BroadcastFromHost
        // outbound + HostP2pInboundDataCallback inbound, both
        // wired in StartP2pProxyServer. Doing the loopback in
        // addition would double the routing (and was the original
        // cause of the "loading screen spins forever" — the loopback
        // session shared CPU/scheduler with the game's polling
        // thread). The match is loose: the master sends us
        // _privateConfig with the LAN port, _externalConfig with
        // the UPnP-mapped public port, or both depending on whether
        // it sees us on the same physical IP — accept either as
        // "this points at us".
        const bool is_self_proxy = (m_p2p_server != nullptr &&
                                    m_p2p_server->IsRunning() &&
                                    (config->proxy_port == m_p2p_server->GetPublicPort() ||
                                     config->proxy_port == m_p2p_server->GetPrivatePort()));
        if (is_self_proxy) {
            LOG_INFO("ExternalProxy targets our own hosted P2P server (port=%u) — skipping loopback (in-process shortcut active)",
                     config->proxy_port);
        } else if (!m_use_p2p_proxy) {
            LOG_INFO("P2P proxy disabled, ignoring ExternalProxy");
        } else {
            // Spawn a dedicated worker for the connect+auth+ready chain
            // (Ryujinx runs HandleExternalProxy on its async receive
            // thread; we're called from the WaitForResponse poll loop,
            // so doing this inline would block the dispatch of the
            // `Connected` reply that arrives right after).
            if (!m_p2p_connect_thread_active.exchange(true)) {
                if (m_p2p_connect_thread_initialized) {
                    os::WaitThread(&m_p2p_connect_thread);
                    os::DestroyThread(&m_p2p_connect_thread);
                }
                m_pending_p2p_config = *config;
                Result rc = os::CreateThread(
                    &m_p2p_connect_thread,
                    ICommunicationService::P2pConnectThreadEntry,
                    this,
                    g_p2p_connect_thread_stack,
                    sizeof(g_p2p_connect_thread_stack),
                    7);
                if (R_FAILED(rc)) {
                    LOG_ERROR("Failed to spawn P2P connect thread: rc=0x%X",
                              rc.GetValue());
                    m_p2p_connect_thread_active = false;
                } else {
                    os::SetThreadNamePointer(&m_p2p_connect_thread, "p2p_connect");
                    os::StartThread(&m_p2p_connect_thread);
                    m_p2p_connect_thread_initialized = true;
                }
            } else {
                LOG_WARN("ExternalProxy received while previous P2P connect still in progress, dropping");
            }
        }
    }
}

void ICommunicationService::HandleProxyDataPacket(const uint8_t* data, size_t size) {
    // Route to BSD MITM proxy sockets for transparent game socket interception
    LOG_INFO("HandleServerPacket: ProxyData ENTRY (size=%zu)", size);
    ryu_ldn::debug::g_logger.flush();
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
                return;
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
}

void ICommunicationService::HandleNetworkErrorPacket(const uint8_t* data, size_t size) {
    // Server reports an error - like Ryujinx HandleNetworkError
    if (size >= sizeof(ryu_ldn::protocol::NetworkErrorMessage)) {
        const auto* err = reinterpret_cast<const ryu_ldn::protocol::NetworkErrorMessage*>(data);
        auto error_code = static_cast<ryu_ldn::protocol::NetworkErrorCode>(err->error_code);

        // PortUnreachable from the upstream NetworkError enum
        // (LdnServer/Network/Types/NetworkError.cs in ryuldn-server):
        //   None=0, PortUnreachable=1, TooManyPlayers=2, ...
        // Our local NetworkErrorCode happens to use 1 for an
        // unrelated `VersionMismatch`, so match against the wire
        // value directly to stay correct against the upstream
        // protocol regardless of our local naming.
        constexpr uint32_t kPortUnreachableWire = 1;

        if (err->error_code == kPortUnreachableWire) {
            // Mirror Ryujinx LdnMasterProxyClient.HandleNetworkError
            // exactly: just flag P2P off. Don't touch the proxy
            // server here — calling Stop() synchronously from the
            // master TCP recv thread joins the P2pProxyServer accept
            // thread, and on Switch close(listen_fd) does not always
            // wake the blocked accept() the way it does on Linux,
            // leaving the loop spinning on errno=113 forever. The
            // proxy cleanup is handled later in the CreateNetwork
            // flow on the game IPC thread (mirrors upstream's
            // CreateNetworkCommon `if (!_useP2pProxy && _hostedProxy != null) DisposeProxy();`).
            LOG_WARN("Received NetworkError: PortUnreachable — disabling P2P (cleanup deferred)");
            m_use_p2p_proxy = false;
        } else {
            m_last_network_error = error_code;
            LOG_ERROR("Received NetworkError: code=%u", err->error_code);
        }
    }
    // Signal error event (like Ryujinx _error.Set())
    m_error_event.Signal();
}


void ICommunicationService::HandleScanReplyPacket(const uint8_t* data, size_t size) {
    // Server sends one network info for each discovered network
    if (size >= sizeof(ryu_ldn::protocol::NetworkInfo)) {
        if (m_scan_result_count < MAX_SCAN_RESULTS) {
            const auto* net_info = reinterpret_cast<const ryu_ldn::protocol::NetworkInfo*>(data);
            std::memcpy(&m_scan_results[m_scan_result_count], net_info, sizeof(NetworkInfo));

            // Fix sceneId and localCommunicationId in scan results (Ryujinx compatibility)
            // Ryujinx creates networks with sceneId=0, but the game expects its own sceneId
            auto& result = m_scan_results[m_scan_result_count];
            if (m_expected_scene_id != 0 && result.networkId.intentId.sceneId != m_expected_scene_id) {
                LOG_INFO("ScanReply: fixing sceneId %u -> %u",
                         result.networkId.intentId.sceneId, m_expected_scene_id);
                result.networkId.intentId.sceneId = m_expected_scene_id;
            }
            if (m_local_communication_id != 0 &&
                result.networkId.intentId.localCommunicationId != m_local_communication_id) {
                result.networkId.intentId.localCommunicationId = m_local_communication_id;
            }

            m_scan_result_count++;
            // Log SessionId so we can compare with Connect request
            const auto& sid = net_info->network_id.session_id;
            LOG_INFO("ScanReply: found network #%zu, node_count=%u, session_id=%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
                     m_scan_result_count,
                     reinterpret_cast<const NetworkInfo*>(net_info)->ldn.nodeCount,
                     sid.data[0], sid.data[1], sid.data[2], sid.data[3],
                     sid.data[4], sid.data[5], sid.data[6], sid.data[7],
                     sid.data[8], sid.data[9], sid.data[10], sid.data[11],
                     sid.data[12], sid.data[13], sid.data[14], sid.data[15]);
        } else {
            LOG_WARN("ScanReply: buffer full, ignoring network");
        }
    }
}



bool ICommunicationService::WaitForResponse(ryu_ldn::protocol::PacketId expected_id, uint64_t timeout_ms) {
    LOG_VERBOSE("Waiting for response: type=%u, timeout=%lu ms",
                static_cast<unsigned>(expected_id), timeout_ms);

    // Special case: if waiting for Connected and m_network_connected is already true,
    // the Connected packet was processed by the receive thread before this call
    {
        std::scoped_lock lock(m_shared_mutex);
        if (expected_id == ryu_ldn::protocol::PacketId::Connected && m_network_connected) {
            LOG_VERBOSE("Already received Connected response (m_network_connected=true)");
            return true;
        }
        if (m_last_response_id == expected_id) {
            LOG_VERBOSE("Already have expected response: type=%u", static_cast<unsigned>(expected_id));
            return true;
        }
    }

    // Clear events before waiting. m_response_event and m_reject_event are
    // AutoClear, so they reset to unsignaled. m_error_event stays ManualClear
    // because it signals a persistent error condition (like Ryujinx's _error).
    m_response_event.Clear();
    m_error_event.Clear();

    // Use os::MultiWait for event-driven wait — the kernel wakes this thread
    // immediately when the receive thread signals the event, instead of the
    // old polling loop that slept 5 ms between update() calls.
    os::MultiWaitType multi_wait;
    os::InitializeMultiWait(std::addressof(multi_wait));

    os::MultiWaitHolderType response_holder;
    os::InitializeMultiWaitHolder(std::addressof(response_holder), m_response_event.GetBase());
    os::LinkMultiWaitHolder(std::addressof(multi_wait), std::addressof(response_holder));

    os::MultiWaitHolderType error_holder;
    os::InitializeMultiWaitHolder(std::addressof(error_holder), m_error_event.GetBase());
    os::LinkMultiWaitHolder(std::addressof(multi_wait), std::addressof(error_holder));

    const uint64_t start_time_ns = armTicksToNs(armGetSystemTick());
    const int64_t timeout_ns = static_cast<int64_t>(timeout_ms) * 1000000LL;
    bool result = false;

    while (true) {
        // Calculate remaining timeout
        const int64_t elapsed_ns = static_cast<int64_t>(armTicksToNs(armGetSystemTick())) - static_cast<int64_t>(start_time_ns);
        const int64_t remaining_ns = timeout_ns - elapsed_ns;
        if (remaining_ns <= 0) {
            LOG_ERROR("Timeout waiting for response: type=%u", static_cast<unsigned>(expected_id));
            break;
        }

        // Wait for either a response or an error, with remaining timeout
        os::MultiWaitHolderType* signaled = os::TimedWaitAny(
            std::addressof(multi_wait), TimeSpan::FromNanoSeconds(remaining_ns));

        if (signaled == std::addressof(response_holder)) {
            // A response packet was received — check if it's the one we want.
            // m_last_response_id is set by HandleServerPacket on the receive thread,
            // protected by m_shared_mutex.
            ryu_ldn::protocol::PacketId last_id;
            {
                std::scoped_lock lock(m_shared_mutex);
                last_id = m_last_response_id;
            }

            if (last_id == expected_id) {
                LOG_VERBOSE("Received expected response: type=%u", static_cast<unsigned>(expected_id));
                result = true;
                break;
            }

            if (last_id == ryu_ldn::protocol::PacketId::NetworkError) {
                LOG_ERROR("Received NetworkError while waiting for response");
                break;
            }

            // Only response-type packets signal m_response_event now.
            // ScanReply/ScanReplyEnd can arrive during WaitForResponse for Connected
            // (server sends scan results while we wait for a response to CreateNetwork).
            // Don't warn for these — they're legitimate interleaved responses.
            if (last_id == ryu_ldn::protocol::PacketId::ScanReply ||
                last_id == ryu_ldn::protocol::PacketId::ScanReplyEnd) {
                // Expected interleaved responses — continue waiting
                continue;
            }
        }

        if (signaled == std::addressof(error_holder)) {
            LOG_ERROR("Received error event while waiting for response");
            break;
        }

        // Timeout (signaled == nullptr)
        LOG_ERROR("Timeout waiting for response: type=%u", static_cast<unsigned>(expected_id));
        break;
    }

    // Special check: Connected may have arrived but was processed as a
    // different m_last_response_id (e.g., the Connected handler directly
    // sets m_network_connected=true and then signals m_response_event
    // separately — check one last time under the mutex).
    if (!result && expected_id == ryu_ldn::protocol::PacketId::Connected) {
        std::scoped_lock lock(m_shared_mutex);
        if (m_network_connected) {
            LOG_VERBOSE("Connected was processed during wait");
            result = true;
        }
    }

    // Check connection loss
    if (!result && !m_server_client.is_connected()) {
        LOG_ERROR("Connection lost while waiting for response");
    }

    os::UnlinkAllMultiWaitHolder(std::addressof(multi_wait));
    os::FinalizeMultiWaitHolder(std::addressof(response_holder));
    os::FinalizeMultiWaitHolder(std::addressof(error_holder));
    os::FinalizeMultiWait(std::addressof(multi_wait));

    return result;
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

    // If we're the host of a P2P network, the joiners are sitting on our
    // P2pProxyServer waiting for ProxyData over their dedicated TCP — the
    // master TCP path is wasted because the joiners' LdnProxy is registered
    // against the P2P protocol instance, not the master one. Broadcast over
    // P2P first; that's the only sink the joiner game listens to. This is
    // the in-process replacement for Ryujinx's TCP loopback design (the
    // host would otherwise dial its own P2pProxyServer to register itself
    // in _players and reuse the same SendAsync path as joiners).
    if (m_p2p_server != nullptr && m_p2p_server->IsRunning()) {
        if (m_p2p_server->BroadcastFromHost(const_cast<ryu_ldn::protocol::ProxyDataHeader&>(header),
                                             static_cast<const uint8_t*>(data), data_len)) {
            return ryu_ldn::network::ClientOpResult::Success;
        }
        // Empty room (no authenticated joiners yet) is the common case here;
        // don't fall back to the master because the master only relays for
        // RELAY-mode joiners which by construction don't exist when we're
        // hosting via P2P.
        return ryu_ldn::network::ClientOpResult::Success;
    }

    // If P2P client is connected, send through P2P instead of master server.
    // Mirrors Ryujinx LdnProxy.SendTo → _parent.SendAsync (where _parent is
    // the P2pProxyClient when in P2P mode) — for the host, _parent is the
    // P2pProxyClient that loops back to its own P2pProxyServer, so this
    // single path covers both joiner and host data plane.
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

    // Mirror the master-server PacketId::ProxyConfig handler (ldn_icommunication.cpp,
    // case ProxyConfig). In RELAY mode the master sends ProxyConfig directly and
    // we land in that case; in P2P mode (IsP2P=true on the server) the master
    // does NOT send ProxyConfig — it travels via the P2pProxyClient instead —
    // so this is the *only* place where these fields get populated for the host.
    //
    // Why all four fields matter:
    // - m_ipv4_address: returned by GetIpv4Address() IPC (the game asks "what is my LDN IP")
    // - m_subnet_mask:  returned alongside it
    // - SocketHelpers/ProxySocketManager local IP: rewrites bind(INADDR_ANY)
    //   so the game's gameplay UDP socket binds on 10.114.0.1:12345 instead
    //   of 0.0.0.0:12345 and actually receives matching proxy traffic.
    // Forgetting the first two on the P2P path made the game stay on the
    // "Création de partie privée" loading screen because GetIpv4Address
    // returned 0 and the game treated the network as not yet usable.
    m_ipv4_address = m_proxy_config.proxy_ip;
    m_subnet_mask  = m_proxy_config.proxy_subnet_mask;

    auto& socket_manager = mitm::bsd::ProxySocketManager::GetInstance();
    socket_manager.SetLocalIp(m_proxy_config.proxy_ip);
    LOG_INFO("ProxySocketManager: local IP set to 0x%08X (via P2P), m_ipv4_address=0x%08X",
             m_proxy_config.proxy_ip, m_ipv4_address);
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

    // Wire the in-process host data plane: the host's vIP is the predicted
    // value pre-set in CreateNetwork (the Ryujinx server's VirtualDhcp always
    // gives baseAddress+1 to the first connector, and the host always
    // self-connects first via game.Connect right after SetOwner). Joiner
    // ProxyData destined to that vIP — or to the broadcast address — is fed
    // to ProxySocketManager::RouteIncomingData via the static callback below
    // (mirrors the relay-mode ProxyData sink in HandleServerPacket).
    constexpr uint32_t kPredictedHostIp = 0x0A720001;
    m_p2p_server->SetHostVirtualIp(kPredictedHostIp);
    m_p2p_server->SetHostDataCallback(HostP2pInboundDataCallback, this);

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

void ICommunicationService::P2pConnectThreadEntry(void* arg) {
    auto* self = static_cast<ICommunicationService*>(arg);
    LOG_INFO("P2pConnectThreadEntry: starting async ExternalProxy connect");
    self->HandleExternalProxyConnect(self->m_pending_p2p_config);
    LOG_INFO("P2pConnectThreadEntry: done");
    self->m_p2p_connect_thread_active = false;
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

// ============================================================================
// Receive Thread (event-driven packet dispatch, like NetCoreServer OnReceived)
// ============================================================================

void ICommunicationService::ReceiveThreadEntry(void* arg) {
    auto* self = static_cast<ICommunicationService*>(arg);
    self->ReceiveThreadFunc();
}

void ICommunicationService::ReceiveThreadFunc() {
    LOG_INFO("Receive thread started");

    while (m_recv_thread_running.load()) {
        // Drive the client's state machine whenever the TCP connection is
        // alive — this covers the handshake phase (Connected/Handshaking)
        // as well as the Ready phase.  m_server_connected is only set
        // *after* ConnectToServer() completes, so checking it here would
        // skip the handshake entirely and deadlock the IPC thread waiting
        // on m_handshake_event.
        if (!m_server_client.is_connected() && !m_server_client.is_transitioning()) {
            // Not connected and not connecting — sleep and retry
            svcSleepThread(10 * 1000000ULL);  // 10 ms
            continue;
        }

        // Read packets from TCP and dispatch immediately via HandleServerPacket.
        // TcpClient::update() calls process_packets() which calls handle_packet()
        // which calls our callback, which calls HandleServerPacket. The callback
        // holds m_shared_mutex during HandleServerPacket processing.
        uint64_t current_time_ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;
        m_server_client.update(current_time_ms);

        // Check inactivity timeout (like Ryujinx _timeout.RefreshTimeout()
        // called from HandleConnected, and _timeout.CheckTimeout() in update loop)
        m_inactivity_timeout.CheckTimeout(current_time_ms);

        // Brief sleep to yield CPU when no data is available.
        // The TCP recv_timeout inside update() already blocks for ~20ms
        // when no packets arrive, so this just prevents a tight loop
        // when update() returns immediately (e.g., during burst processing).
        svcSleepThread(1 * 1000000ULL);  // 1 ms
    }

    LOG_INFO("Receive thread stopped");
}

u8 ICommunicationService::FindLocalNodeId() const {
    // Search nodes array for our IP address
    for (u8 i = 0; i < NodeCountMax; i++) {
        const auto& node = m_network_info.ldn.nodes[i];
        if (node.isConnected && node.ipv4Address == m_ipv4_address) {
            return i;
        }
    }
    return 0xFF; // Not found
}

} // namespace ams::mitm::ldn
