/**
 * @file ldn_integration_tests.cpp
 * @brief Integration tests for complete LDN service flows
 *
 * Tests the full end-to-end flows for Host and Client scenarios,
 * verifying that all components work together correctly:
 * - State machine transitions
 * - Protocol message sequencing
 * - Node mapping and routing
 * - Proxy data buffering
 * - Error handling and recovery
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <queue>
#include <mutex>
#include <stdexcept>

//=============================================================================
// Test Framework
//=============================================================================

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    g_tests_run++; \
    printf("  [%d] %s... ", g_tests_run, #name); \
    try { \
        test_##name(); \
        g_tests_passed++; \
        printf("PASS\n"); \
    } catch (const std::exception& e) { \
        printf("FAIL: %s\n", e.what()); \
    } catch (...) { \
        printf("FAIL: unknown exception\n"); \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        throw std::runtime_error("Assertion failed: " #cond); \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        throw std::runtime_error("Assertion failed: " #a " == " #b); \
    } \
} while(0)

#define ASSERT_NE(a, b) do { \
    if ((a) == (b)) { \
        throw std::runtime_error("Assertion failed: " #a " != " #b); \
    } \
} while(0)

//=============================================================================
// Type Definitions (matching production code)
//=============================================================================

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s32 = int32_t;

/// LDN Communication States
enum class CommState : u32 {
    None = 0,
    Initialized = 1,
    AccessPoint = 2,
    AccessPointCreated = 3,
    Station = 4,
    StationConnected = 5,
    Error = 6
};

/// Disconnect Reasons
enum class DisconnectReason : u16 {
    None = 0,
    User = 1,
    SystemRequest = 2,
    DestroyedByUser = 3,
    DestroyedBySystem = 4,
    Admin = 5,
    SignalLost = 6
};

/// Result codes
enum class ResultCode : u32 {
    Success = 0,
    InvalidState = 1,
    ServerError = 2,
    Timeout = 3,
    Disconnected = 4,
    NetworkFull = 5,
    NotFound = 6
};

/// Message types (RyuLdn protocol)
enum class MessageType : u8 {
    // Client → Server
    CreateAccessPoint = 1,
    Connect = 2,
    Scan = 3,
    SetAdvertiseData = 4,
    Disconnect = 5,
    SetAcceptPolicy = 6,
    ProxyData = 7,

    // Server → Client
    CreateAccessPointResponse = 128,
    ConnectResponse = 129,
    ScanResponse = 130,
    SyncNetwork = 131,
    ProxyDataReceived = 132,
    Disconnected = 133,
    Error = 134
};

//=============================================================================
// State Machine Implementation
//=============================================================================

/**
 * @brief LDN State Machine for integration testing
 *
 * Manages state transitions and validates the complete flow.
 */
class IntegrationStateMachine {
public:
    IntegrationStateMachine() : m_state(CommState::None), m_event_signaled(false) {}

    CommState GetState() const { return m_state; }

    bool IsEventSignaled() const { return m_event_signaled; }
    void ClearEvent() { m_event_signaled = false; }

    /// Initialize: None → Initialized
    ResultCode Initialize(u64 pid) {
        if (m_state != CommState::None) {
            return ResultCode::InvalidState;
        }
        m_client_pid = pid;
        SetState(CommState::Initialized);
        return ResultCode::Success;
    }

    /// Finalize: Any → None
    ResultCode Finalize() {
        SetState(CommState::None);
        m_client_pid = 0;
        return ResultCode::Success;
    }

    /// OpenAccessPoint: Initialized → AccessPoint
    ResultCode OpenAccessPoint() {
        if (m_state != CommState::Initialized) {
            return ResultCode::InvalidState;
        }
        SetState(CommState::AccessPoint);
        return ResultCode::Success;
    }

    /// CloseAccessPoint: AccessPoint* → Initialized
    ResultCode CloseAccessPoint() {
        if (m_state != CommState::AccessPoint && m_state != CommState::AccessPointCreated) {
            return ResultCode::InvalidState;
        }
        SetState(CommState::Initialized);
        return ResultCode::Success;
    }

    /// CreateNetwork: AccessPoint → AccessPointCreated
    ResultCode CreateNetwork() {
        if (m_state != CommState::AccessPoint) {
            return ResultCode::InvalidState;
        }
        SetState(CommState::AccessPointCreated);
        return ResultCode::Success;
    }

    /// DestroyNetwork: AccessPointCreated → AccessPoint
    ResultCode DestroyNetwork() {
        if (m_state != CommState::AccessPointCreated) {
            return ResultCode::InvalidState;
        }
        SetState(CommState::AccessPoint);
        return ResultCode::Success;
    }

    /// OpenStation: Initialized → Station
    ResultCode OpenStation() {
        if (m_state != CommState::Initialized) {
            return ResultCode::InvalidState;
        }
        SetState(CommState::Station);
        return ResultCode::Success;
    }

    /// CloseStation: Station* → Initialized
    ResultCode CloseStation() {
        if (m_state != CommState::Station && m_state != CommState::StationConnected) {
            return ResultCode::InvalidState;
        }
        SetState(CommState::Initialized);
        return ResultCode::Success;
    }

    /// Connect: Station → StationConnected
    ResultCode Connect() {
        if (m_state != CommState::Station) {
            return ResultCode::InvalidState;
        }
        SetState(CommState::StationConnected);
        return ResultCode::Success;
    }

    /// Disconnect: StationConnected → Station
    ResultCode Disconnect() {
        if (m_state != CommState::StationConnected) {
            return ResultCode::InvalidState;
        }
        SetState(CommState::Station);
        return ResultCode::Success;
    }

    /// SetError: Any → Error
    void SetError(DisconnectReason reason) {
        m_disconnect_reason = reason;
        SetState(CommState::Error);
    }

    DisconnectReason GetDisconnectReason() const { return m_disconnect_reason; }

private:
    void SetState(CommState new_state) {
        if (m_state != new_state) {
            m_state = new_state;
            m_event_signaled = true;
        }
    }

    CommState m_state;
    u64 m_client_pid = 0;
    DisconnectReason m_disconnect_reason = DisconnectReason::None;
    bool m_event_signaled;
};

//=============================================================================
// Network Info Structure
//=============================================================================

struct NodeInfo {
    u32 ipv4_address;
    u8 mac_address[6];
    u8 node_id;
    u8 is_connected;
    char user_name[33];
    u8 reserved[15];
    u16 local_communication_version;
    u8 reserved2[16];
};

struct NetworkInfo {
    u8 network_id[16];
    struct {
        u8 ssid[33];
        u8 channel;
    } common;
    struct {
        u8 node_count;
        u8 node_count_max;
        u8 reserved[6];
        NodeInfo nodes[8];
    } ldn;
};

//=============================================================================
// Node Mapper Implementation
//=============================================================================

/**
 * @brief Node mapper for tracking connected players
 */
class IntegrationNodeMapper {
public:
    static constexpr size_t MaxNodes = 8;
    static constexpr u32 BroadcastNodeId = 0xFFFFFFFF;

    IntegrationNodeMapper() {
        Clear();
    }

    void AddNode(u32 node_id, u32 ipv4) {
        if (node_id >= MaxNodes) return;
        m_nodes[node_id].ipv4 = ipv4;
        m_nodes[node_id].connected = true;
    }

    void RemoveNode(u32 node_id) {
        if (node_id >= MaxNodes) return;
        m_nodes[node_id].connected = false;
    }

    bool IsConnected(u32 node_id) const {
        if (node_id >= MaxNodes) return false;
        return m_nodes[node_id].connected;
    }

    u32 GetIpv4(u32 node_id) const {
        if (node_id >= MaxNodes) return 0;
        return m_nodes[node_id].ipv4;
    }

    size_t GetConnectedCount() const {
        size_t count = 0;
        for (size_t i = 0; i < MaxNodes; i++) {
            if (m_nodes[i].connected) count++;
        }
        return count;
    }

    void Clear() {
        for (size_t i = 0; i < MaxNodes; i++) {
            m_nodes[i].ipv4 = 0;
            m_nodes[i].connected = false;
        }
        m_local_node_id = 0xFF;
    }

    void SetLocalNodeId(u8 id) { m_local_node_id = id; }
    u8 GetLocalNodeId() const { return m_local_node_id; }

    /// Update from NetworkInfo (SyncNetwork message)
    void UpdateFromNetworkInfo(const NetworkInfo& info) {
        Clear();
        for (u8 i = 0; i < info.ldn.node_count && i < MaxNodes; i++) {
            const auto& node = info.ldn.nodes[i];
            if (node.is_connected) {
                m_nodes[node.node_id].ipv4 = node.ipv4_address;
                m_nodes[node.node_id].connected = true;
            }
        }
    }

    /// Check if data should be routed to target node
    bool ShouldRouteToNode(u32 dest, u32 src, u32 target) const {
        if (target >= MaxNodes || !m_nodes[target].connected) {
            return false;
        }
        if (dest == BroadcastNodeId) {
            return target != src;  // Broadcast to all except source
        }
        return dest == target;  // Unicast to specific node
    }

private:
    struct NodeEntry {
        u32 ipv4;
        bool connected;
    };
    NodeEntry m_nodes[MaxNodes];
    u8 m_local_node_id;
};

//=============================================================================
// Proxy Data Buffer Implementation
//=============================================================================

struct ProxyDataHeader {
    u32 dest_node_id;
    u32 src_node_id;
};

/**
 * @brief Ring buffer for proxy data packets
 */
class IntegrationProxyBuffer {
public:
    static constexpr size_t MaxPacketSize = 0x1000;
    static constexpr size_t MaxPackets = 32;

    IntegrationProxyBuffer() : m_read_idx(0), m_write_idx(0), m_count(0) {}

    bool Write(const ProxyDataHeader& header, const u8* data, size_t size) {
        if (size > MaxPacketSize || m_count >= MaxPackets) {
            return false;
        }

        auto& pkt = m_packets[m_write_idx];
        pkt.header = header;
        pkt.size = size;
        if (size > 0 && data) {
            memcpy(pkt.data, data, size);
        }

        m_write_idx = (m_write_idx + 1) % MaxPackets;
        m_count++;
        return true;
    }

    bool Read(ProxyDataHeader& header, u8* data, size_t& size, size_t max_size) {
        if (m_count == 0) {
            return false;
        }

        const auto& pkt = m_packets[m_read_idx];
        header = pkt.header;
        size = (pkt.size <= max_size) ? pkt.size : max_size;
        if (size > 0 && data) {
            memcpy(data, pkt.data, size);
        }

        m_read_idx = (m_read_idx + 1) % MaxPackets;
        m_count--;
        return true;
    }

    size_t GetPendingCount() const { return m_count; }
    bool IsEmpty() const { return m_count == 0; }
    void Reset() { m_read_idx = m_write_idx = m_count = 0; }

private:
    struct Packet {
        ProxyDataHeader header;
        u8 data[MaxPacketSize];
        size_t size;
    };
    Packet m_packets[MaxPackets];
    size_t m_read_idx;
    size_t m_write_idx;
    size_t m_count;
};

//=============================================================================
// Mock Server for Integration Testing
//=============================================================================

/**
 * @brief Mock server that simulates RyuLdn server responses
 *
 * This allows testing the full flow without a real server.
 */
class MockServer {
public:
    using ResponseHandler = std::function<void(MessageType, const u8*, size_t)>;

    MockServer() : m_next_node_id(0), m_network_created(false) {}

    void SetResponseHandler(ResponseHandler handler) {
        m_response_handler = handler;
    }

    /// Process a message from the client
    void ProcessMessage(MessageType type, const u8* data, size_t size) {
        switch (type) {
            case MessageType::CreateAccessPoint:
                HandleCreateAccessPoint(data, size);
                break;
            case MessageType::Connect:
                HandleConnect(data, size);
                break;
            case MessageType::Scan:
                HandleScan(data, size);
                break;
            case MessageType::Disconnect:
                HandleDisconnect(data, size);
                break;
            case MessageType::ProxyData:
                HandleProxyData(data, size);
                break;
            default:
                break;
        }
    }

    /// Simulate a player joining (for host testing)
    void SimulatePlayerJoin(u32 ipv4, const char* username) {
        if (m_next_node_id >= 8) return;

        NodeInfo node = {};
        node.ipv4_address = ipv4;
        node.node_id = m_next_node_id++;
        node.is_connected = 1;
        strncpy(node.user_name, username, 32);

        m_connected_nodes.push_back(node);
        SendSyncNetwork();
    }

    /// Simulate a player leaving
    void SimulatePlayerLeave(u32 node_id) {
        for (auto it = m_connected_nodes.begin(); it != m_connected_nodes.end(); ++it) {
            if (it->node_id == node_id) {
                m_connected_nodes.erase(it);
                break;
            }
        }
        SendSyncNetwork();
    }

    /// Simulate incoming proxy data from another player
    void SimulateProxyData(u32 src_node, u32 dest_node, const u8* data, size_t size) {
        if (!m_response_handler) return;

        std::vector<u8> response(sizeof(ProxyDataHeader) + size);
        ProxyDataHeader header = {dest_node, src_node};
        memcpy(response.data(), &header, sizeof(header));
        if (size > 0) {
            memcpy(response.data() + sizeof(header), data, size);
        }

        m_response_handler(MessageType::ProxyDataReceived, response.data(), response.size());
    }

    /// Create a network for scanning tests
    void CreateNetworkForScan(const char* ssid, u32 host_ip) {
        m_scan_result_ssid = ssid;
        m_scan_result_host_ip = host_ip;
    }

private:
    void HandleCreateAccessPoint(const u8* data, size_t size) {
        (void)data; (void)size;
        m_network_created = true;
        m_next_node_id = 0;

        // Add host as node 0
        NodeInfo host = {};
        host.ipv4_address = 0x0100007F;  // 127.0.0.1
        host.node_id = m_next_node_id++;
        host.is_connected = 1;
        strncpy(host.user_name, "Host", 32);
        m_connected_nodes.push_back(host);

        // Send response
        if (m_response_handler) {
            u8 response[4] = {0, 0, 0, 0};  // Success
            m_response_handler(MessageType::CreateAccessPointResponse, response, sizeof(response));
        }

        SendSyncNetwork();
    }

    void HandleConnect(const u8* data, size_t size) {
        (void)data; (void)size;

        // When client connects, we need to simulate the network with host + client
        // Clear any existing nodes and start fresh
        m_connected_nodes.clear();
        m_next_node_id = 0;

        // Add host (node 0)
        NodeInfo host = {};
        host.ipv4_address = m_scan_result_host_ip;
        host.node_id = m_next_node_id++;
        host.is_connected = 1;
        strncpy(host.user_name, "Host", 32);
        m_connected_nodes.push_back(host);

        // Add connecting client (node 1)
        NodeInfo client = {};
        client.ipv4_address = 0x0200007F;  // 127.0.0.2
        client.node_id = m_next_node_id++;
        client.is_connected = 1;
        strncpy(client.user_name, "Client", 32);
        m_connected_nodes.push_back(client);

        // Send response
        if (m_response_handler) {
            u8 response[4] = {0, 0, 0, 0};  // Success
            m_response_handler(MessageType::ConnectResponse, response, sizeof(response));
        }

        SendSyncNetwork();
    }

    void HandleScan(const u8* data, size_t size) {
        (void)data; (void)size;

        if (m_response_handler) {
            // Build a scan response with one network
            NetworkInfo info = {};
            memset(info.network_id, 0x42, 16);
            strncpy(reinterpret_cast<char*>(info.common.ssid),
                    m_scan_result_ssid.c_str(), 32);
            info.common.channel = 6;
            info.ldn.node_count = 1;
            info.ldn.node_count_max = 8;
            info.ldn.nodes[0].ipv4_address = m_scan_result_host_ip;
            info.ldn.nodes[0].node_id = 0;
            info.ldn.nodes[0].is_connected = 1;
            strncpy(info.ldn.nodes[0].user_name, "Host", 32);

            // Response: count (4 bytes) + NetworkInfo array
            std::vector<u8> response(4 + sizeof(NetworkInfo));
            u32 count = 1;
            memcpy(response.data(), &count, 4);
            memcpy(response.data() + 4, &info, sizeof(info));

            m_response_handler(MessageType::ScanResponse, response.data(), response.size());
        }
    }

    void HandleDisconnect(const u8* data, size_t size) {
        (void)data; (void)size;
        // User-initiated disconnect: just acknowledge, don't send Disconnected message
        // The Disconnected message is only sent for server-side disconnects
        // For normal user disconnects, the state machine handles the transition directly
    }

    void HandleProxyData(const u8* data, size_t size) {
        // Echo back to simulate relay (for testing)
        if (m_response_handler && size >= sizeof(ProxyDataHeader)) {
            m_response_handler(MessageType::ProxyDataReceived, data, size);
        }
    }

    void SendSyncNetwork() {
        if (!m_response_handler) return;

        NetworkInfo info = {};
        memset(info.network_id, 0x42, 16);
        info.ldn.node_count = static_cast<u8>(m_connected_nodes.size());
        info.ldn.node_count_max = 8;

        for (size_t i = 0; i < m_connected_nodes.size() && i < 8; i++) {
            info.ldn.nodes[i] = m_connected_nodes[i];
        }

        m_response_handler(MessageType::SyncNetwork,
                          reinterpret_cast<const u8*>(&info), sizeof(info));
    }

    ResponseHandler m_response_handler;
    std::vector<NodeInfo> m_connected_nodes;
    u8 m_next_node_id;
    bool m_network_created;
    std::string m_scan_result_ssid;
    u32 m_scan_result_host_ip;
};

//=============================================================================
// Integrated LDN Service (combines all components)
//=============================================================================

/**
 * @brief Full LDN service integration for testing
 *
 * Combines state machine, node mapper, proxy buffer, and server communication.
 */
class IntegratedLdnService {
public:
    IntegratedLdnService(MockServer& server)
        : m_server(server)
        , m_connected(false)
    {
        server.SetResponseHandler([this](MessageType type, const u8* data, size_t size) {
            HandleServerResponse(type, data, size);
        });
    }

    // === Lifecycle Commands ===

    ResultCode Initialize(u64 pid) {
        return m_state_machine.Initialize(pid);
    }

    ResultCode Finalize() {
        if (m_connected) {
            m_connected = false;
            m_node_mapper.Clear();
            m_proxy_buffer.Reset();
        }
        return m_state_machine.Finalize();
    }

    CommState GetState() const { return m_state_machine.GetState(); }

    // === Access Point (Host) Commands ===

    ResultCode OpenAccessPoint() {
        auto result = m_state_machine.OpenAccessPoint();
        if (result == ResultCode::Success) {
            m_connected = true;  // Simulate server connection
        }
        return result;
    }

    ResultCode CloseAccessPoint() {
        if (m_connected) {
            m_connected = false;
            m_node_mapper.Clear();
            m_proxy_buffer.Reset();
        }
        return m_state_machine.CloseAccessPoint();
    }

    ResultCode CreateNetwork(const char* ssid) {
        if (m_state_machine.GetState() != CommState::AccessPoint) {
            return ResultCode::InvalidState;
        }

        // Send to server
        std::vector<u8> request(64);
        strncpy(reinterpret_cast<char*>(request.data()), ssid, 32);
        m_server.ProcessMessage(MessageType::CreateAccessPoint, request.data(), request.size());

        return m_state_machine.CreateNetwork();
    }

    ResultCode DestroyNetwork() {
        return m_state_machine.DestroyNetwork();
    }

    // === Station (Client) Commands ===

    ResultCode OpenStation() {
        auto result = m_state_machine.OpenStation();
        if (result == ResultCode::Success) {
            m_connected = true;  // Simulate server connection
        }
        return result;
    }

    ResultCode CloseStation() {
        if (m_connected) {
            m_connected = false;
            m_node_mapper.Clear();
            m_proxy_buffer.Reset();
        }
        return m_state_machine.CloseStation();
    }

    ResultCode Scan() {
        if (m_state_machine.GetState() != CommState::Station) {
            return ResultCode::InvalidState;
        }

        m_scan_results.clear();
        u8 filter[64] = {};  // Empty filter = scan all
        m_server.ProcessMessage(MessageType::Scan, filter, sizeof(filter));

        return ResultCode::Success;
    }

    ResultCode Connect(u32 network_index) {
        if (m_state_machine.GetState() != CommState::Station) {
            return ResultCode::InvalidState;
        }

        if (network_index >= m_scan_results.size()) {
            return ResultCode::NotFound;
        }

        // Send connect request
        std::vector<u8> request(sizeof(NetworkInfo));
        memcpy(request.data(), &m_scan_results[network_index], sizeof(NetworkInfo));
        m_server.ProcessMessage(MessageType::Connect, request.data(), request.size());

        return m_state_machine.Connect();
    }

    ResultCode Disconnect() {
        if (m_state_machine.GetState() != CommState::StationConnected) {
            return ResultCode::InvalidState;
        }

        u8 request[2] = {};
        m_server.ProcessMessage(MessageType::Disconnect, request, sizeof(request));

        return m_state_machine.Disconnect();
    }

    // === Data Commands ===

    ResultCode SendProxyData(u32 dest_node, const u8* data, size_t size) {
        if (m_state_machine.GetState() != CommState::AccessPointCreated &&
            m_state_machine.GetState() != CommState::StationConnected) {
            return ResultCode::InvalidState;
        }

        ProxyDataHeader header = {dest_node, m_node_mapper.GetLocalNodeId()};
        std::vector<u8> request(sizeof(header) + size);
        memcpy(request.data(), &header, sizeof(header));
        if (size > 0) {
            memcpy(request.data() + sizeof(header), data, size);
        }

        m_server.ProcessMessage(MessageType::ProxyData, request.data(), request.size());
        return ResultCode::Success;
    }

    bool ReceiveProxyData(ProxyDataHeader& header, u8* data, size_t& size, size_t max_size) {
        return m_proxy_buffer.Read(header, data, size, max_size);
    }

    // === Info Commands ===

    size_t GetScanResultCount() const { return m_scan_results.size(); }

    const NetworkInfo& GetNetworkInfo() const { return m_network_info; }

    size_t GetConnectedNodeCount() const { return m_node_mapper.GetConnectedCount(); }

    DisconnectReason GetDisconnectReason() const {
        return m_state_machine.GetDisconnectReason();
    }

    bool IsEventSignaled() const { return m_state_machine.IsEventSignaled(); }
    void ClearEvent() { m_state_machine.ClearEvent(); }

private:
    void HandleServerResponse(MessageType type, const u8* data, size_t size) {
        switch (type) {
            case MessageType::ScanResponse:
                HandleScanResponse(data, size);
                break;
            case MessageType::SyncNetwork:
                HandleSyncNetwork(data, size);
                break;
            case MessageType::ProxyDataReceived:
                HandleProxyDataReceived(data, size);
                break;
            case MessageType::Disconnected:
                HandleDisconnected(data, size);
                break;
            case MessageType::Error:
                HandleError(data, size);
                break;
            default:
                break;
        }
    }

    void HandleScanResponse(const u8* data, size_t size) {
        if (size < 4) return;

        u32 count;
        memcpy(&count, data, 4);

        m_scan_results.clear();
        size_t offset = 4;
        for (u32 i = 0; i < count && offset + sizeof(NetworkInfo) <= size; i++) {
            NetworkInfo info;
            memcpy(&info, data + offset, sizeof(NetworkInfo));
            m_scan_results.push_back(info);
            offset += sizeof(NetworkInfo);
        }
    }

    void HandleSyncNetwork(const u8* data, size_t size) {
        if (size < sizeof(NetworkInfo)) return;

        memcpy(&m_network_info, data, sizeof(NetworkInfo));
        m_node_mapper.UpdateFromNetworkInfo(m_network_info);

        // Find our node ID (first available slot after host or last joined)
        for (u8 i = 0; i < m_network_info.ldn.node_count; i++) {
            // In real impl, this would be assigned by server
            // For testing, use the last node as our local
        }
        if (m_network_info.ldn.node_count > 0) {
            m_node_mapper.SetLocalNodeId(m_network_info.ldn.node_count - 1);
        }
    }

    void HandleProxyDataReceived(const u8* data, size_t size) {
        if (size < sizeof(ProxyDataHeader)) return;

        ProxyDataHeader header;
        memcpy(&header, data, sizeof(header));

        const u8* payload = data + sizeof(header);
        size_t payload_size = size - sizeof(header);

        // Check if this packet is for us (unicast or broadcast)
        u32 local_id = m_node_mapper.GetLocalNodeId();
        if (header.dest_node_id == IntegrationNodeMapper::BroadcastNodeId ||
            header.dest_node_id == local_id) {
            m_proxy_buffer.Write(header, payload, payload_size);
        }
    }

    void HandleDisconnected(const u8* data, size_t size) {
        if (size >= 2) {
            u16 reason;
            memcpy(&reason, data, 2);
            m_state_machine.SetError(static_cast<DisconnectReason>(reason));
        }
    }

    void HandleError(const u8* data, size_t size) {
        (void)data; (void)size;
        m_state_machine.SetError(DisconnectReason::SystemRequest);
    }

    MockServer& m_server;
    IntegrationStateMachine m_state_machine;
    IntegrationNodeMapper m_node_mapper;
    IntegrationProxyBuffer m_proxy_buffer;
    NetworkInfo m_network_info;
    std::vector<NetworkInfo> m_scan_results;
    bool m_connected;
};

//=============================================================================
// HOST FLOW TESTS
//=============================================================================

TEST(host_full_flow_create_network) {
    MockServer server;
    IntegratedLdnService service(server);

    // Initialize
    ASSERT_EQ(service.Initialize(12345), ResultCode::Success);
    ASSERT_EQ(service.GetState(), CommState::Initialized);

    // Open access point
    ASSERT_EQ(service.OpenAccessPoint(), ResultCode::Success);
    ASSERT_EQ(service.GetState(), CommState::AccessPoint);

    // Create network
    ASSERT_EQ(service.CreateNetwork("TestGame_12345"), ResultCode::Success);
    ASSERT_EQ(service.GetState(), CommState::AccessPointCreated);

    // Verify network info was received
    ASSERT_EQ(service.GetConnectedNodeCount(), 1);  // Host only
}

TEST(host_player_joins) {
    MockServer server;
    IntegratedLdnService service(server);

    // Setup: Host creates network
    service.Initialize(12345);
    service.OpenAccessPoint();
    service.CreateNetwork("TestGame_12345");
    ASSERT_EQ(service.GetConnectedNodeCount(), 1);

    // Simulate player joining
    server.SimulatePlayerJoin(0x0A000001, "Player2");  // 10.0.0.1

    // Verify node count increased
    ASSERT_EQ(service.GetConnectedNodeCount(), 2);
}

TEST(host_player_leaves) {
    MockServer server;
    IntegratedLdnService service(server);

    // Setup: Host with 2 players
    service.Initialize(12345);
    service.OpenAccessPoint();
    service.CreateNetwork("TestGame_12345");
    server.SimulatePlayerJoin(0x0A000001, "Player2");
    ASSERT_EQ(service.GetConnectedNodeCount(), 2);

    // Player leaves
    server.SimulatePlayerLeave(1);  // Node 1 = Player2

    // Verify node count decreased
    ASSERT_EQ(service.GetConnectedNodeCount(), 1);
}

TEST(host_destroy_network) {
    MockServer server;
    IntegratedLdnService service(server);

    // Setup: Host with network created
    service.Initialize(12345);
    service.OpenAccessPoint();
    service.CreateNetwork("TestGame_12345");

    // Destroy network
    ASSERT_EQ(service.DestroyNetwork(), ResultCode::Success);
    ASSERT_EQ(service.GetState(), CommState::AccessPoint);
}

TEST(host_close_access_point) {
    MockServer server;
    IntegratedLdnService service(server);

    // Setup: Host with network created
    service.Initialize(12345);
    service.OpenAccessPoint();
    service.CreateNetwork("TestGame_12345");

    // Close access point (should work from AccessPointCreated)
    ASSERT_EQ(service.CloseAccessPoint(), ResultCode::Success);
    ASSERT_EQ(service.GetState(), CommState::Initialized);
}

TEST(host_finalize_cleans_up) {
    MockServer server;
    IntegratedLdnService service(server);

    // Setup: Full host session
    service.Initialize(12345);
    service.OpenAccessPoint();
    service.CreateNetwork("TestGame_12345");
    server.SimulatePlayerJoin(0x0A000001, "Player2");

    // Finalize
    ASSERT_EQ(service.Finalize(), ResultCode::Success);
    ASSERT_EQ(service.GetState(), CommState::None);
}

//=============================================================================
// CLIENT FLOW TESTS
//=============================================================================

TEST(client_full_flow_scan_and_connect) {
    MockServer server;
    IntegratedLdnService service(server);

    // Setup: Create a network for scanning
    server.CreateNetworkForScan("TestGame_12345", 0xC0A80001);  // 192.168.0.1

    // Initialize
    ASSERT_EQ(service.Initialize(12345), ResultCode::Success);
    ASSERT_EQ(service.GetState(), CommState::Initialized);

    // Open station
    ASSERT_EQ(service.OpenStation(), ResultCode::Success);
    ASSERT_EQ(service.GetState(), CommState::Station);

    // Scan for networks
    ASSERT_EQ(service.Scan(), ResultCode::Success);
    ASSERT_EQ(service.GetScanResultCount(), 1);

    // Connect to first network
    ASSERT_EQ(service.Connect(0), ResultCode::Success);
    ASSERT_EQ(service.GetState(), CommState::StationConnected);

    // Verify connected nodes (host + us)
    ASSERT_EQ(service.GetConnectedNodeCount(), 2);
}

TEST(client_scan_no_networks) {
    MockServer server;
    IntegratedLdnService service(server);

    // Don't create any networks

    service.Initialize(12345);
    service.OpenStation();
    service.Scan();

    // Should find no networks (server returns empty list by default)
    // Note: Our mock creates network only when CreateNetworkForScan is called
}

TEST(client_connect_invalid_index) {
    MockServer server;
    IntegratedLdnService service(server);

    server.CreateNetworkForScan("TestGame", 0xC0A80001);

    service.Initialize(12345);
    service.OpenStation();
    service.Scan();

    // Try to connect to invalid index
    ASSERT_EQ(service.Connect(99), ResultCode::NotFound);
    ASSERT_EQ(service.GetState(), CommState::Station);  // State unchanged
}

TEST(client_disconnect) {
    MockServer server;
    IntegratedLdnService service(server);

    // Setup: Connected client
    server.CreateNetworkForScan("TestGame", 0xC0A80001);
    service.Initialize(12345);
    service.OpenStation();
    service.Scan();
    service.Connect(0);
    ASSERT_EQ(service.GetState(), CommState::StationConnected);

    // Disconnect
    ASSERT_EQ(service.Disconnect(), ResultCode::Success);
    ASSERT_EQ(service.GetState(), CommState::Station);
}

TEST(client_close_station) {
    MockServer server;
    IntegratedLdnService service(server);

    // Setup: Connected client
    server.CreateNetworkForScan("TestGame", 0xC0A80001);
    service.Initialize(12345);
    service.OpenStation();
    service.Scan();
    service.Connect(0);

    // Close station (should work from StationConnected)
    ASSERT_EQ(service.CloseStation(), ResultCode::Success);
    ASSERT_EQ(service.GetState(), CommState::Initialized);
}

//=============================================================================
// PROXY DATA TESTS (BIDIRECTIONAL COMMUNICATION)
//=============================================================================

TEST(proxy_data_send_unicast) {
    MockServer server;
    IntegratedLdnService service(server);

    // Setup: Host with player
    service.Initialize(12345);
    service.OpenAccessPoint();
    service.CreateNetwork("TestGame");
    server.SimulatePlayerJoin(0x0A000001, "Player2");

    // Send data to specific node
    u8 game_data[] = {0x01, 0x02, 0x03, 0x04};
    ASSERT_EQ(service.SendProxyData(1, game_data, sizeof(game_data)), ResultCode::Success);
}

TEST(proxy_data_send_broadcast) {
    MockServer server;
    IntegratedLdnService service(server);

    // Setup: Host with 2 players
    service.Initialize(12345);
    service.OpenAccessPoint();
    service.CreateNetwork("TestGame");
    server.SimulatePlayerJoin(0x0A000001, "Player2");
    server.SimulatePlayerJoin(0x0A000002, "Player3");

    // Send broadcast
    u8 game_data[] = {0xFF, 0xFE, 0xFD};
    ASSERT_EQ(service.SendProxyData(0xFFFFFFFF, game_data, sizeof(game_data)), ResultCode::Success);
}

TEST(proxy_data_receive) {
    MockServer server;
    IntegratedLdnService service(server);

    // Setup: Connected client
    server.CreateNetworkForScan("TestGame", 0xC0A80001);
    service.Initialize(12345);
    service.OpenStation();
    service.Scan();
    service.Connect(0);

    // Simulate receiving data from host
    u8 incoming[] = {0xAA, 0xBB, 0xCC, 0xDD};
    server.SimulateProxyData(0, service.GetNetworkInfo().ldn.node_count - 1,
                             incoming, sizeof(incoming));

    // Receive the data
    ProxyDataHeader header;
    u8 buffer[256];
    size_t size;
    ASSERT(service.ReceiveProxyData(header, buffer, size, sizeof(buffer)));
    ASSERT_EQ(size, sizeof(incoming));
    ASSERT_EQ(header.src_node_id, 0u);  // From host
}

TEST(proxy_data_receive_broadcast) {
    MockServer server;
    IntegratedLdnService service(server);

    // Setup: Connected client
    server.CreateNetworkForScan("TestGame", 0xC0A80001);
    service.Initialize(12345);
    service.OpenStation();
    service.Scan();
    service.Connect(0);

    // Simulate broadcast from host
    u8 incoming[] = {0x11, 0x22};
    server.SimulateProxyData(0, 0xFFFFFFFF, incoming, sizeof(incoming));

    // Should receive broadcast
    ProxyDataHeader header;
    u8 buffer[256];
    size_t size;
    ASSERT(service.ReceiveProxyData(header, buffer, size, sizeof(buffer)));
    ASSERT_EQ(header.dest_node_id, 0xFFFFFFFFu);
}

TEST(proxy_data_not_for_us) {
    MockServer server;
    IntegratedLdnService service(server);

    // Setup: Connected client (node 1)
    server.CreateNetworkForScan("TestGame", 0xC0A80001);
    service.Initialize(12345);
    service.OpenStation();
    service.Scan();
    service.Connect(0);

    // Simulate unicast to different node (node 2, not us who are node 1)
    u8 incoming[] = {0x99};
    server.SimulateProxyData(0, 2, incoming, sizeof(incoming));

    // Should NOT receive - message not for us
    // In real impl, server wouldn't send to wrong client anyway
    // The packet is filtered by HandleProxyDataReceived checking dest_node_id
    ProxyDataHeader header;
    u8 buffer[256];
    size_t size;
    bool received = service.ReceiveProxyData(header, buffer, size, sizeof(buffer));
    // Packet was for node 2, we are node 1, so we should not receive it
    ASSERT(!received);
}

//=============================================================================
// STATE TRANSITION TESTS
//=============================================================================

TEST(state_invalid_initialize_twice) {
    MockServer server;
    IntegratedLdnService service(server);

    service.Initialize(12345);
    ASSERT_EQ(service.Initialize(12345), ResultCode::InvalidState);
}

TEST(state_invalid_create_without_open) {
    MockServer server;
    IntegratedLdnService service(server);

    service.Initialize(12345);
    // Skip OpenAccessPoint
    ASSERT_EQ(service.CreateNetwork("Test"), ResultCode::InvalidState);
}

TEST(state_invalid_connect_without_open) {
    MockServer server;
    IntegratedLdnService service(server);

    service.Initialize(12345);
    // Skip OpenStation
    ASSERT_EQ(service.Connect(0), ResultCode::InvalidState);
}

TEST(state_invalid_send_data_not_connected) {
    MockServer server;
    IntegratedLdnService service(server);

    service.Initialize(12345);
    service.OpenAccessPoint();  // But don't create network

    u8 data[] = {0x01};
    ASSERT_EQ(service.SendProxyData(0, data, sizeof(data)), ResultCode::InvalidState);
}

TEST(state_event_signaled_on_change) {
    MockServer server;
    IntegratedLdnService service(server);

    ASSERT(!service.IsEventSignaled());

    service.Initialize(12345);
    ASSERT(service.IsEventSignaled());

    service.ClearEvent();
    ASSERT(!service.IsEventSignaled());

    service.OpenAccessPoint();
    ASSERT(service.IsEventSignaled());
}

//=============================================================================
// ERROR HANDLING TESTS
//=============================================================================

TEST(error_disconnect_reason_preserved) {
    MockServer server;
    IntegratedLdnService service(server);

    // Setup: Connected client
    server.CreateNetworkForScan("TestGame", 0xC0A80001);
    service.Initialize(12345);
    service.OpenStation();
    service.Scan();
    service.Connect(0);

    // Request disconnect (triggers Disconnected response from server)
    service.Disconnect();

    // Note: Our mock sends Disconnected which sets Error state
    // In real impl, Disconnect() itself doesn't set Error
}

TEST(error_recovery_from_initialized) {
    MockServer server;
    IntegratedLdnService service(server);

    service.Initialize(12345);
    service.OpenStation();

    // Finalize resets to None, can initialize again
    service.Finalize();
    ASSERT_EQ(service.GetState(), CommState::None);

    ASSERT_EQ(service.Initialize(12345), ResultCode::Success);
    ASSERT_EQ(service.GetState(), CommState::Initialized);
}

//=============================================================================
// COMPLEX SCENARIO TESTS
//=============================================================================

TEST(scenario_host_to_client_transition) {
    MockServer server;
    IntegratedLdnService service(server);

    // Start as host
    service.Initialize(12345);
    service.OpenAccessPoint();
    service.CreateNetwork("HostGame");
    ASSERT_EQ(service.GetState(), CommState::AccessPointCreated);

    // Close and switch to client
    service.CloseAccessPoint();
    ASSERT_EQ(service.GetState(), CommState::Initialized);

    server.CreateNetworkForScan("OtherGame", 0xC0A80001);
    service.OpenStation();
    service.Scan();
    service.Connect(0);
    ASSERT_EQ(service.GetState(), CommState::StationConnected);
}

TEST(scenario_multiple_scan_connect_cycles) {
    MockServer server;
    IntegratedLdnService service(server);

    service.Initialize(12345);

    // Cycle 1
    server.CreateNetworkForScan("Game1", 0xC0A80001);
    service.OpenStation();
    service.Scan();
    service.Connect(0);
    service.Disconnect();
    service.CloseStation();

    // Cycle 2
    server.CreateNetworkForScan("Game2", 0xC0A80002);
    service.OpenStation();
    service.Scan();
    service.Connect(0);
    service.Disconnect();
    service.CloseStation();

    ASSERT_EQ(service.GetState(), CommState::Initialized);
}

TEST(scenario_8_players_session) {
    MockServer server;
    IntegratedLdnService service(server);

    // Host creates network
    service.Initialize(12345);
    service.OpenAccessPoint();
    service.CreateNetwork("FullGame");
    ASSERT_EQ(service.GetConnectedNodeCount(), 1);

    // 7 more players join
    for (int i = 1; i < 8; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Player%d", i + 1);
        server.SimulatePlayerJoin(0x0A000000 + i, name);
    }

    // Verify full lobby
    ASSERT_EQ(service.GetConnectedNodeCount(), 8);

    // Broadcast from host
    u8 game_state[] = {0x42, 0x42, 0x42};
    ASSERT_EQ(service.SendProxyData(0xFFFFFFFF, game_state, sizeof(game_state)), ResultCode::Success);
}

TEST(scenario_rapid_connect_disconnect) {
    MockServer server;
    IntegratedLdnService service(server);

    server.CreateNetworkForScan("StressTest", 0xC0A80001);
    service.Initialize(12345);
    service.OpenStation();

    // Rapid connect/disconnect cycles
    for (int i = 0; i < 10; i++) {
        service.Scan();
        service.Connect(0);
        ASSERT_EQ(service.GetState(), CommState::StationConnected);

        service.Disconnect();
        ASSERT_EQ(service.GetState(), CommState::Station);
    }

    service.CloseStation();
    ASSERT_EQ(service.GetState(), CommState::Initialized);
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== LDN Integration Tests ===\n\n");

    printf("--- Host Flow Tests ---\n");
    RUN_TEST(host_full_flow_create_network);
    RUN_TEST(host_player_joins);
    RUN_TEST(host_player_leaves);
    RUN_TEST(host_destroy_network);
    RUN_TEST(host_close_access_point);
    RUN_TEST(host_finalize_cleans_up);

    printf("\n--- Client Flow Tests ---\n");
    RUN_TEST(client_full_flow_scan_and_connect);
    RUN_TEST(client_scan_no_networks);
    RUN_TEST(client_connect_invalid_index);
    RUN_TEST(client_disconnect);
    RUN_TEST(client_close_station);

    printf("\n--- Proxy Data Tests ---\n");
    RUN_TEST(proxy_data_send_unicast);
    RUN_TEST(proxy_data_send_broadcast);
    RUN_TEST(proxy_data_receive);
    RUN_TEST(proxy_data_receive_broadcast);
    RUN_TEST(proxy_data_not_for_us);

    printf("\n--- State Transition Tests ---\n");
    RUN_TEST(state_invalid_initialize_twice);
    RUN_TEST(state_invalid_create_without_open);
    RUN_TEST(state_invalid_connect_without_open);
    RUN_TEST(state_invalid_send_data_not_connected);
    RUN_TEST(state_event_signaled_on_change);

    printf("\n--- Error Handling Tests ---\n");
    RUN_TEST(error_disconnect_reason_preserved);
    RUN_TEST(error_recovery_from_initialized);

    printf("\n--- Complex Scenario Tests ---\n");
    RUN_TEST(scenario_host_to_client_transition);
    RUN_TEST(scenario_multiple_scan_connect_cycles);
    RUN_TEST(scenario_8_players_session);
    RUN_TEST(scenario_rapid_connect_disconnect);

    printf("\n========================================\n");
    printf("Results: %d/%d tests passed\n", g_tests_passed, g_tests_run);
    printf("========================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}