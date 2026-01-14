/**
 * @file ldn_proxy_tests.cpp
 * @brief Unit tests for LDN data proxy functionality
 *
 * Tests for Story 3.7: Proxy de données
 * - ProxyDataHeader structure (IP-based addressing per RyuLDN protocol)
 * - ProxyInfo structure
 * - Node mapping (IP to node ID translation)
 * - Data routing
 * - Broadcast handling
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <functional>
#include <map>

#include "protocol/types.hpp"

using namespace ryu_ldn::protocol;

// ============================================================================
// Test Framework (minimal standalone)
// ============================================================================

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct TestRegistrar_##name { \
        TestRegistrar_##name() { \
            printf("  [TEST] " #name "... "); \
            fflush(stdout); \
            test_##name(); \
            g_tests_passed++; \
            printf("PASS\n"); \
        } \
    } g_registrar_##name; \
    static void test_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL\n    Expected: " #cond " to be true\n    at %s:%d\n", __FILE__, __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_FALSE(cond) \
    do { \
        if (cond) { \
            printf("FAIL\n    Expected: " #cond " to be false\n    at %s:%d\n", __FILE__, __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            printf("FAIL\n    Expected: " #a " == " #b "\n    at %s:%d\n", __FILE__, __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            printf("FAIL\n    Expected: " #a " != " #b "\n    at %s:%d\n", __FILE__, __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

// ============================================================================
// Constants for Proxy
// ============================================================================

constexpr uint32_t BROADCAST_IP = 0xFFFFFFFF;  ///< Broadcast destination IP
constexpr size_t MAX_PROXY_DATA_SIZE = 0x1000; // 4KB max per packet
constexpr uint8_t TEST_MAX_NODES = 8;

// Test IP addresses (10.114.0.x network)
constexpr uint32_t TEST_IP_NODE0 = 0x0A720001;  // 10.114.0.1
constexpr uint32_t TEST_IP_NODE1 = 0x0A720002;  // 10.114.0.2
constexpr uint32_t TEST_IP_NODE2 = 0x0A720003;  // 10.114.0.3
constexpr uint16_t TEST_PORT = 30456;

// ============================================================================
// Test Node Mapping Helper (simulates IP-to-node mapping)
// ============================================================================

class TestNodeMapper {
public:
    struct NodeEntry {
        uint32_t node_id;
        uint32_t ipv4_address;
        bool is_connected;
    };

    TestNodeMapper() {
        // Initialize with empty nodes
        for (uint8_t i = 0; i < TEST_MAX_NODES; i++) {
            m_nodes[i] = {i, 0, false};
        }
    }

    void add_node(uint32_t node_id, uint32_t ipv4) {
        if (node_id < TEST_MAX_NODES) {
            m_nodes[node_id].ipv4_address = ipv4;
            m_nodes[node_id].is_connected = true;
            m_ip_to_node[ipv4] = node_id;
        }
    }

    void remove_node(uint32_t node_id) {
        if (node_id < TEST_MAX_NODES) {
            uint32_t ip = m_nodes[node_id].ipv4_address;
            m_nodes[node_id].is_connected = false;
            m_ip_to_node.erase(ip);
        }
    }

    bool is_node_connected(uint32_t node_id) const {
        if (node_id >= TEST_MAX_NODES) return false;
        return m_nodes[node_id].is_connected;
    }

    bool is_ip_connected(uint32_t ip) const {
        auto it = m_ip_to_node.find(ip);
        if (it == m_ip_to_node.end()) return false;
        return m_nodes[it->second].is_connected;
    }

    uint32_t get_node_ip(uint32_t node_id) const {
        if (node_id >= TEST_MAX_NODES) return 0;
        return m_nodes[node_id].ipv4_address;
    }

    uint32_t get_ip_node_id(uint32_t ip) const {
        auto it = m_ip_to_node.find(ip);
        if (it == m_ip_to_node.end()) return 0xFFFFFFFF;
        return it->second;
    }

    std::vector<uint32_t> get_broadcast_target_ips(uint32_t source_ip) const {
        std::vector<uint32_t> targets;
        for (uint8_t i = 0; i < TEST_MAX_NODES; i++) {
            if (m_nodes[i].is_connected && m_nodes[i].ipv4_address != source_ip) {
                targets.push_back(m_nodes[i].ipv4_address);
            }
        }
        return targets;
    }

    size_t get_connected_count() const {
        size_t count = 0;
        for (uint8_t i = 0; i < TEST_MAX_NODES; i++) {
            if (m_nodes[i].is_connected) count++;
        }
        return count;
    }

private:
    NodeEntry m_nodes[TEST_MAX_NODES];
    std::map<uint32_t, uint32_t> m_ip_to_node;  // IP -> node_id mapping
};

// ============================================================================
// Test Proxy Data Buffer (simulates the buffer we'll implement)
// ============================================================================

class TestProxyBuffer {
public:
    TestProxyBuffer() : m_write_pos(0), m_read_pos(0) {
        m_buffer.resize(MAX_PROXY_DATA_SIZE * 4); // 4 packets
    }

    bool write(const ProxyDataHeader& header, const uint8_t* data, size_t size) {
        size_t total_size = sizeof(header) + size;
        if (m_write_pos + total_size > m_buffer.size()) {
            return false; // Buffer full
        }

        std::memcpy(m_buffer.data() + m_write_pos, &header, sizeof(header));
        m_write_pos += sizeof(header);

        if (size > 0 && data != nullptr) {
            std::memcpy(m_buffer.data() + m_write_pos, data, size);
            m_write_pos += size;
        }

        m_packet_sizes.push_back(total_size);
        return true;
    }

    bool read(ProxyDataHeader& header, uint8_t* data, size_t& size, size_t max_size) {
        if (m_packet_sizes.empty()) {
            return false; // No data
        }

        size_t packet_size = m_packet_sizes.front();
        size_t data_size = packet_size - sizeof(header);

        if (data_size > max_size) {
            return false; // Buffer too small
        }

        std::memcpy(&header, m_buffer.data() + m_read_pos, sizeof(header));
        m_read_pos += sizeof(header);

        if (data_size > 0) {
            std::memcpy(data, m_buffer.data() + m_read_pos, data_size);
            m_read_pos += data_size;
        }

        size = data_size;
        m_packet_sizes.erase(m_packet_sizes.begin());
        return true;
    }

    size_t pending_packets() const {
        return m_packet_sizes.size();
    }

    void reset() {
        m_write_pos = 0;
        m_read_pos = 0;
        m_packet_sizes.clear();
    }

private:
    std::vector<uint8_t> m_buffer;
    size_t m_write_pos;
    size_t m_read_pos;
    std::vector<size_t> m_packet_sizes;
};

// ============================================================================
// ProxyInfo Tests
// ============================================================================

TEST(proxy_info_size) {
    ASSERT_EQ(sizeof(ProxyInfo), 0x10u);  // 16 bytes per RyuLDN protocol
}

TEST(proxy_info_fields) {
    ProxyInfo info{};
    info.source_ipv4 = TEST_IP_NODE0;
    info.source_port = 12345;
    info.dest_ipv4 = TEST_IP_NODE1;
    info.dest_port = 54321;
    info.protocol = ProtocolType::Udp;

    ASSERT_EQ(info.source_ipv4, TEST_IP_NODE0);
    ASSERT_EQ(info.source_port, 12345u);
    ASSERT_EQ(info.dest_ipv4, TEST_IP_NODE1);
    ASSERT_EQ(info.dest_port, 54321u);
    ASSERT_EQ(info.protocol, ProtocolType::Udp);
}

TEST(proxy_info_zero_init) {
    ProxyInfo info{};
    ASSERT_EQ(info.source_ipv4, 0u);
    ASSERT_EQ(info.source_port, 0u);
    ASSERT_EQ(info.dest_ipv4, 0u);
    ASSERT_EQ(info.dest_port, 0u);
}

// ============================================================================
// ProxyDataHeader Tests
// ============================================================================

TEST(proxy_data_header_size) {
    ASSERT_EQ(sizeof(ProxyDataHeader), 0x14u);  // 20 bytes per RyuLDN protocol
}

TEST(proxy_data_header_fields) {
    ProxyDataHeader header{};
    header.info.source_ipv4 = TEST_IP_NODE0;
    header.info.source_port = TEST_PORT;
    header.info.dest_ipv4 = TEST_IP_NODE1;
    header.info.dest_port = TEST_PORT;
    header.info.protocol = ProtocolType::Udp;
    header.data_length = 100;

    ASSERT_EQ(header.info.source_ipv4, TEST_IP_NODE0);
    ASSERT_EQ(header.info.dest_ipv4, TEST_IP_NODE1);
    ASSERT_EQ(header.data_length, 100u);
}

TEST(proxy_data_header_broadcast) {
    ProxyDataHeader header{};
    header.info.dest_ipv4 = BROADCAST_IP;
    header.info.source_ipv4 = TEST_IP_NODE0;

    ASSERT_EQ(header.info.dest_ipv4, BROADCAST_IP);
}

TEST(proxy_data_header_zero_init) {
    ProxyDataHeader header{};
    ASSERT_EQ(header.info.dest_ipv4, 0u);
    ASSERT_EQ(header.info.source_ipv4, 0u);
    ASSERT_EQ(header.data_length, 0u);
}

// ============================================================================
// Node Mapping Tests
// ============================================================================

TEST(node_mapper_initial_state) {
    TestNodeMapper mapper;
    ASSERT_EQ(mapper.get_connected_count(), 0u);
}

TEST(node_mapper_add_node) {
    TestNodeMapper mapper;
    mapper.add_node(0, TEST_IP_NODE0);

    ASSERT_TRUE(mapper.is_node_connected(0));
    ASSERT_TRUE(mapper.is_ip_connected(TEST_IP_NODE0));
    ASSERT_EQ(mapper.get_node_ip(0), TEST_IP_NODE0);
    ASSERT_EQ(mapper.get_ip_node_id(TEST_IP_NODE0), 0u);
    ASSERT_EQ(mapper.get_connected_count(), 1u);
}

TEST(node_mapper_add_multiple_nodes) {
    TestNodeMapper mapper;
    mapper.add_node(0, TEST_IP_NODE0);
    mapper.add_node(1, TEST_IP_NODE1);
    mapper.add_node(2, TEST_IP_NODE2);

    ASSERT_EQ(mapper.get_connected_count(), 3u);
    ASSERT_TRUE(mapper.is_node_connected(0));
    ASSERT_TRUE(mapper.is_node_connected(1));
    ASSERT_TRUE(mapper.is_node_connected(2));
    ASSERT_FALSE(mapper.is_node_connected(3));
}

TEST(node_mapper_remove_node) {
    TestNodeMapper mapper;
    mapper.add_node(0, TEST_IP_NODE0);
    mapper.add_node(1, TEST_IP_NODE1);

    mapper.remove_node(0);

    ASSERT_FALSE(mapper.is_node_connected(0));
    ASSERT_FALSE(mapper.is_ip_connected(TEST_IP_NODE0));
    ASSERT_TRUE(mapper.is_node_connected(1));
    ASSERT_EQ(mapper.get_connected_count(), 1u);
}

TEST(node_mapper_invalid_node_id) {
    TestNodeMapper mapper;
    ASSERT_FALSE(mapper.is_node_connected(100));
    ASSERT_EQ(mapper.get_node_ip(100), 0u);
}

TEST(node_mapper_broadcast_targets) {
    TestNodeMapper mapper;
    mapper.add_node(0, TEST_IP_NODE0);
    mapper.add_node(1, TEST_IP_NODE1);
    mapper.add_node(2, TEST_IP_NODE2);

    // Broadcast from node 0 should target nodes 1 and 2
    auto targets = mapper.get_broadcast_target_ips(TEST_IP_NODE0);
    ASSERT_EQ(targets.size(), 2u);
}

TEST(node_mapper_broadcast_excludes_source) {
    TestNodeMapper mapper;
    mapper.add_node(0, TEST_IP_NODE0);
    mapper.add_node(1, TEST_IP_NODE1);

    auto targets = mapper.get_broadcast_target_ips(TEST_IP_NODE0);

    // Should not include source IP
    for (auto t : targets) {
        ASSERT_NE(t, TEST_IP_NODE0);
    }
}

TEST(node_mapper_max_nodes) {
    TestNodeMapper mapper;

    // Add all 8 nodes
    for (uint8_t i = 0; i < TEST_MAX_NODES; i++) {
        mapper.add_node(i, 0x0A720000 + i);
    }

    ASSERT_EQ(mapper.get_connected_count(), static_cast<size_t>(TEST_MAX_NODES));

    // Broadcast from node 0 should target 7 nodes
    auto targets = mapper.get_broadcast_target_ips(0x0A720000);
    ASSERT_EQ(targets.size(), static_cast<size_t>(TEST_MAX_NODES - 1));
}

// ============================================================================
// Proxy Buffer Tests
// ============================================================================

TEST(proxy_buffer_initial_empty) {
    TestProxyBuffer buffer;
    ASSERT_EQ(buffer.pending_packets(), 0u);
}

TEST(proxy_buffer_write_read_small) {
    TestProxyBuffer buffer;
    ProxyDataHeader header{};
    header.info.source_ipv4 = TEST_IP_NODE0;
    header.info.dest_ipv4 = TEST_IP_NODE1;
    header.info.protocol = ProtocolType::Udp;
    header.data_length = 4;

    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    ASSERT_TRUE(buffer.write(header, data, sizeof(data)));
    ASSERT_EQ(buffer.pending_packets(), 1u);

    ProxyDataHeader read_header{};
    uint8_t read_data[64];
    size_t read_size;
    ASSERT_TRUE(buffer.read(read_header, read_data, read_size, sizeof(read_data)));

    ASSERT_EQ(read_header.info.dest_ipv4, TEST_IP_NODE1);
    ASSERT_EQ(read_header.info.source_ipv4, TEST_IP_NODE0);
    ASSERT_EQ(read_size, sizeof(data));
    ASSERT_EQ(read_data[0], 0x01u);
    ASSERT_EQ(read_data[3], 0x04u);
}

TEST(proxy_buffer_write_multiple) {
    TestProxyBuffer buffer;

    for (uint32_t i = 0; i < 4; i++) {
        ProxyDataHeader header{};
        header.info.dest_ipv4 = 0x0A720000 + i;
        header.info.source_ipv4 = TEST_IP_NODE0;

        uint8_t data = static_cast<uint8_t>(i);
        ASSERT_TRUE(buffer.write(header, &data, 1));
    }

    ASSERT_EQ(buffer.pending_packets(), 4u);
}

TEST(proxy_buffer_read_order_fifo) {
    TestProxyBuffer buffer;

    // Write packets with different destination IPs
    for (uint32_t i = 0; i < 3; i++) {
        ProxyDataHeader header{};
        header.info.dest_ipv4 = 0x0A720000 + i;
        header.info.source_ipv4 = TEST_IP_NODE0;

        uint8_t data = static_cast<uint8_t>(i * 10);
        buffer.write(header, &data, 1);
    }

    // Read should be in FIFO order
    for (uint32_t i = 0; i < 3; i++) {
        ProxyDataHeader header{};
        uint8_t data;
        size_t size;
        ASSERT_TRUE(buffer.read(header, &data, size, 1));
        ASSERT_EQ(header.info.dest_ipv4, 0x0A720000 + i);
        ASSERT_EQ(data, static_cast<uint8_t>(i * 10));
    }
}

TEST(proxy_buffer_read_empty) {
    TestProxyBuffer buffer;
    ProxyDataHeader header{};
    uint8_t data[64];
    size_t size;

    ASSERT_FALSE(buffer.read(header, data, size, sizeof(data)));
}

TEST(proxy_buffer_reset) {
    TestProxyBuffer buffer;
    ProxyDataHeader header{};
    uint8_t data = 0x42;
    buffer.write(header, &data, 1);

    ASSERT_EQ(buffer.pending_packets(), 1u);

    buffer.reset();
    ASSERT_EQ(buffer.pending_packets(), 0u);
}

TEST(proxy_buffer_header_only) {
    TestProxyBuffer buffer;
    ProxyDataHeader header{};
    header.info.dest_ipv4 = TEST_IP_NODE1;
    header.info.source_ipv4 = TEST_IP_NODE2;
    header.data_length = 0;

    // Write header only (no data)
    ASSERT_TRUE(buffer.write(header, nullptr, 0));

    ProxyDataHeader read_header{};
    uint8_t read_data[64];
    size_t read_size;
    ASSERT_TRUE(buffer.read(read_header, read_data, read_size, sizeof(read_data)));

    ASSERT_EQ(read_header.info.dest_ipv4, TEST_IP_NODE1);
    ASSERT_EQ(read_header.info.source_ipv4, TEST_IP_NODE2);
    ASSERT_EQ(read_size, 0u);
}

// ============================================================================
// Proxy Routing Logic Tests (IP-based)
// ============================================================================

// Simulates the routing decision based on IP addresses
static bool should_route_to_ip(const ProxyDataHeader& header,
                               uint32_t target_ip,
                               const TestNodeMapper& mapper) {
    // Broadcast: route to all connected IPs except source
    if (header.info.dest_ipv4 == BROADCAST_IP) {
        return mapper.is_ip_connected(target_ip) &&
               target_ip != header.info.source_ipv4;
    }

    // Unicast: only route to destination IP
    return header.info.dest_ipv4 == target_ip &&
           mapper.is_ip_connected(target_ip);
}

TEST(routing_unicast_to_connected) {
    TestNodeMapper mapper;
    mapper.add_node(0, TEST_IP_NODE0);
    mapper.add_node(1, TEST_IP_NODE1);

    ProxyDataHeader header{};
    header.info.dest_ipv4 = TEST_IP_NODE1;
    header.info.source_ipv4 = TEST_IP_NODE0;

    ASSERT_TRUE(should_route_to_ip(header, TEST_IP_NODE1, mapper));
    ASSERT_FALSE(should_route_to_ip(header, TEST_IP_NODE0, mapper)); // Not destination
    ASSERT_FALSE(should_route_to_ip(header, TEST_IP_NODE2, mapper)); // Not connected
}

TEST(routing_unicast_to_disconnected) {
    TestNodeMapper mapper;
    mapper.add_node(0, TEST_IP_NODE0);
    // Node 1 not connected

    ProxyDataHeader header{};
    header.info.dest_ipv4 = TEST_IP_NODE1;
    header.info.source_ipv4 = TEST_IP_NODE0;

    ASSERT_FALSE(should_route_to_ip(header, TEST_IP_NODE1, mapper)); // Not connected
}

TEST(routing_broadcast_all_nodes) {
    TestNodeMapper mapper;
    mapper.add_node(0, TEST_IP_NODE0);
    mapper.add_node(1, TEST_IP_NODE1);
    mapper.add_node(2, TEST_IP_NODE2);

    ProxyDataHeader header{};
    header.info.dest_ipv4 = BROADCAST_IP;
    header.info.source_ipv4 = TEST_IP_NODE0;

    ASSERT_FALSE(should_route_to_ip(header, TEST_IP_NODE0, mapper)); // Source excluded
    ASSERT_TRUE(should_route_to_ip(header, TEST_IP_NODE1, mapper));
    ASSERT_TRUE(should_route_to_ip(header, TEST_IP_NODE2, mapper));
    ASSERT_FALSE(should_route_to_ip(header, 0x0A720004, mapper)); // Not connected
}

TEST(routing_broadcast_excludes_source) {
    TestNodeMapper mapper;
    mapper.add_node(0, TEST_IP_NODE0);
    mapper.add_node(1, TEST_IP_NODE1);

    ProxyDataHeader header{};
    header.info.dest_ipv4 = BROADCAST_IP;
    header.info.source_ipv4 = TEST_IP_NODE0;

    // Broadcast should not go back to source
    ASSERT_FALSE(should_route_to_ip(header, TEST_IP_NODE0, mapper));
}

// ============================================================================
// Data Size Tests
// ============================================================================

TEST(proxy_max_data_size) {
    TestProxyBuffer buffer;
    ProxyDataHeader header{};

    // Write maximum size data
    std::vector<uint8_t> large_data(MAX_PROXY_DATA_SIZE, 0xAA);
    ASSERT_TRUE(buffer.write(header, large_data.data(), large_data.size()));

    ProxyDataHeader read_header{};
    std::vector<uint8_t> read_data(MAX_PROXY_DATA_SIZE);
    size_t read_size;
    ASSERT_TRUE(buffer.read(read_header, read_data.data(), read_size, read_data.size()));
    ASSERT_EQ(read_size, MAX_PROXY_DATA_SIZE);
}

TEST(proxy_varying_data_sizes) {
    TestProxyBuffer buffer;

    // Write packets of varying sizes
    std::vector<size_t> sizes = {1, 10, 100, 500, 1000};

    for (size_t s : sizes) {
        ProxyDataHeader header{};
        header.info.dest_ipv4 = TEST_IP_NODE1;
        std::vector<uint8_t> data(s, 0x55);
        ASSERT_TRUE(buffer.write(header, data.data(), data.size()));
    }

    // Read and verify sizes
    for (size_t expected_size : sizes) {
        ProxyDataHeader header{};
        std::vector<uint8_t> data(2048);
        size_t read_size;
        ASSERT_TRUE(buffer.read(header, data.data(), read_size, data.size()));
        ASSERT_EQ(read_size, expected_size);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(proxy_self_destination) {
    TestNodeMapper mapper;
    mapper.add_node(0, TEST_IP_NODE0);

    ProxyDataHeader header{};
    header.info.dest_ipv4 = TEST_IP_NODE0;
    header.info.source_ipv4 = TEST_IP_NODE0;

    // Sending to self - router should allow but game might filter
    ASSERT_TRUE(should_route_to_ip(header, TEST_IP_NODE0, mapper));
}

TEST(proxy_header_byte_layout) {
    ProxyDataHeader header{};
    header.info.source_ipv4 = 0x01020304;    // 1.2.3.4
    header.info.source_port = 0x0506;        // 1286
    header.info.dest_ipv4 = 0x0708090A;      // 7.8.9.10
    header.info.dest_port = 0x0B0C;          // 2828
    header.info.protocol = ProtocolType::Udp;  // 17
    header.data_length = 0x12345678;

    uint8_t* bytes = reinterpret_cast<uint8_t*>(&header);

    // ProxyInfo layout (16 bytes):
    // - source_ipv4: 4 bytes (offset 0)
    // - source_port: 2 bytes (offset 4)
    // - dest_ipv4: 4 bytes (offset 6)
    // - dest_port: 2 bytes (offset 10)
    // - protocol: 4 bytes (offset 12)
    // data_length: 4 bytes (offset 16)

    // Little-endian: source_ipv4 at offset 0
    ASSERT_EQ(bytes[0], 0x04u);
    ASSERT_EQ(bytes[1], 0x03u);
    ASSERT_EQ(bytes[2], 0x02u);
    ASSERT_EQ(bytes[3], 0x01u);

    // source_port at offset 4
    ASSERT_EQ(bytes[4], 0x06u);
    ASSERT_EQ(bytes[5], 0x05u);

    // dest_ipv4 at offset 6
    ASSERT_EQ(bytes[6], 0x0Au);
    ASSERT_EQ(bytes[7], 0x09u);
    ASSERT_EQ(bytes[8], 0x08u);
    ASSERT_EQ(bytes[9], 0x07u);

    // dest_port at offset 10
    ASSERT_EQ(bytes[10], 0x0Cu);
    ASSERT_EQ(bytes[11], 0x0Bu);

    // protocol at offset 12 (ProtocolType::Udp = 17)
    ASSERT_EQ(bytes[12], 17u);
    ASSERT_EQ(bytes[13], 0u);
    ASSERT_EQ(bytes[14], 0u);
    ASSERT_EQ(bytes[15], 0u);

    // data_length at offset 16
    ASSERT_EQ(bytes[16], 0x78u);
    ASSERT_EQ(bytes[17], 0x56u);
    ASSERT_EQ(bytes[18], 0x34u);
    ASSERT_EQ(bytes[19], 0x12u);
}

TEST(proxy_info_protocol_types) {
    ProxyInfo info{};

    info.protocol = ProtocolType::Tcp;
    ASSERT_EQ(static_cast<int32_t>(info.protocol), 6);

    info.protocol = ProtocolType::Udp;
    ASSERT_EQ(static_cast<int32_t>(info.protocol), 17);

    info.protocol = ProtocolType::Unknown;
    ASSERT_EQ(static_cast<int32_t>(info.protocol), -1);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("\n========================================\n");
    printf("  LDN Proxy Tests - ryu_ldn_nx\n");
    printf("========================================\n\n");

    // Tests run automatically via static initializers

    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n", g_tests_passed, g_tests_passed + g_tests_failed);
    printf("========================================\n\n");

    return g_tests_failed > 0 ? 1 : 0;
}
