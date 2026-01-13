/**
 * @file ldn_proxy_tests.cpp
 * @brief Unit tests for LDN data proxy functionality
 *
 * Tests for Story 3.7: Proxy de données
 * - ProxyDataHeader structure
 * - Node mapping
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

constexpr uint32_t BROADCAST_NODE_ID = 0xFFFFFFFF;
constexpr size_t MAX_PROXY_DATA_SIZE = 0x1000; // 4KB max per packet
constexpr uint8_t MAX_NODES = 8;

// ============================================================================
// Test Node Mapping Helper (simulates the mapping we'll implement)
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
        for (uint8_t i = 0; i < MAX_NODES; i++) {
            m_nodes[i] = {i, 0, false};
        }
    }

    void add_node(uint32_t node_id, uint32_t ipv4) {
        if (node_id < MAX_NODES) {
            m_nodes[node_id].ipv4_address = ipv4;
            m_nodes[node_id].is_connected = true;
        }
    }

    void remove_node(uint32_t node_id) {
        if (node_id < MAX_NODES) {
            m_nodes[node_id].is_connected = false;
        }
    }

    bool is_node_connected(uint32_t node_id) const {
        if (node_id >= MAX_NODES) return false;
        return m_nodes[node_id].is_connected;
    }

    uint32_t get_node_ip(uint32_t node_id) const {
        if (node_id >= MAX_NODES) return 0;
        return m_nodes[node_id].ipv4_address;
    }

    std::vector<uint32_t> get_broadcast_targets(uint32_t source_node_id) const {
        std::vector<uint32_t> targets;
        for (uint8_t i = 0; i < MAX_NODES; i++) {
            if (m_nodes[i].is_connected && i != source_node_id) {
                targets.push_back(i);
            }
        }
        return targets;
    }

    size_t get_connected_count() const {
        size_t count = 0;
        for (uint8_t i = 0; i < MAX_NODES; i++) {
            if (m_nodes[i].is_connected) count++;
        }
        return count;
    }

private:
    NodeEntry m_nodes[MAX_NODES];
};

// ============================================================================
// Test Proxy Data Buffer (simulates the buffer we'll implement)
// ============================================================================

class TestProxyBuffer {
public:
    TestProxyBuffer() : m_write_pos(0), m_read_pos(0) {
        m_buffer.resize(MAX_PROXY_DATA_SIZE * 4); // 4 packets
    }

    bool write(const ryu_ldn::protocol::ProxyDataHeader& header,
               const uint8_t* data, size_t size) {
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

    bool read(ryu_ldn::protocol::ProxyDataHeader& header,
              uint8_t* data, size_t& size, size_t max_size) {
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
// ProxyDataHeader Tests
// ============================================================================

TEST(proxy_data_header_size) {
    ASSERT_EQ(sizeof(ryu_ldn::protocol::ProxyDataHeader), 8u);
}

TEST(proxy_data_header_fields) {
    ryu_ldn::protocol::ProxyDataHeader header{};
    header.destination_node_id = 1;
    header.source_node_id = 0;

    ASSERT_EQ(header.destination_node_id, 1u);
    ASSERT_EQ(header.source_node_id, 0u);
}

TEST(proxy_data_header_broadcast) {
    ryu_ldn::protocol::ProxyDataHeader header{};
    header.destination_node_id = BROADCAST_NODE_ID;
    header.source_node_id = 0;

    ASSERT_EQ(header.destination_node_id, BROADCAST_NODE_ID);
}

TEST(proxy_data_header_zero_init) {
    ryu_ldn::protocol::ProxyDataHeader header{};
    ASSERT_EQ(header.destination_node_id, 0u);
    ASSERT_EQ(header.source_node_id, 0u);
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
    mapper.add_node(0, 0x0A720001); // 10.114.0.1

    ASSERT_TRUE(mapper.is_node_connected(0));
    ASSERT_EQ(mapper.get_node_ip(0), 0x0A720001u);
    ASSERT_EQ(mapper.get_connected_count(), 1u);
}

TEST(node_mapper_add_multiple_nodes) {
    TestNodeMapper mapper;
    mapper.add_node(0, 0x0A720001);
    mapper.add_node(1, 0x0A720002);
    mapper.add_node(2, 0x0A720003);

    ASSERT_EQ(mapper.get_connected_count(), 3u);
    ASSERT_TRUE(mapper.is_node_connected(0));
    ASSERT_TRUE(mapper.is_node_connected(1));
    ASSERT_TRUE(mapper.is_node_connected(2));
    ASSERT_FALSE(mapper.is_node_connected(3));
}

TEST(node_mapper_remove_node) {
    TestNodeMapper mapper;
    mapper.add_node(0, 0x0A720001);
    mapper.add_node(1, 0x0A720002);

    mapper.remove_node(0);

    ASSERT_FALSE(mapper.is_node_connected(0));
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
    mapper.add_node(0, 0x0A720001);
    mapper.add_node(1, 0x0A720002);
    mapper.add_node(2, 0x0A720003);

    // Broadcast from node 0 should target nodes 1 and 2
    auto targets = mapper.get_broadcast_targets(0);
    ASSERT_EQ(targets.size(), 2u);
}

TEST(node_mapper_broadcast_excludes_source) {
    TestNodeMapper mapper;
    mapper.add_node(0, 0x0A720001);
    mapper.add_node(1, 0x0A720002);

    auto targets = mapper.get_broadcast_targets(0);

    // Should not include node 0 (the source)
    for (auto t : targets) {
        ASSERT_NE(t, 0u);
    }
}

TEST(node_mapper_max_nodes) {
    TestNodeMapper mapper;

    // Add all 8 nodes
    for (uint8_t i = 0; i < MAX_NODES; i++) {
        mapper.add_node(i, 0x0A720000 + i);
    }

    ASSERT_EQ(mapper.get_connected_count(), MAX_NODES);

    // Broadcast from node 0 should target 7 nodes
    auto targets = mapper.get_broadcast_targets(0);
    ASSERT_EQ(targets.size(), MAX_NODES - 1);
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
    ryu_ldn::protocol::ProxyDataHeader header{};
    header.destination_node_id = 1;
    header.source_node_id = 0;

    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    ASSERT_TRUE(buffer.write(header, data, sizeof(data)));
    ASSERT_EQ(buffer.pending_packets(), 1u);

    ryu_ldn::protocol::ProxyDataHeader read_header{};
    uint8_t read_data[64];
    size_t read_size;
    ASSERT_TRUE(buffer.read(read_header, read_data, read_size, sizeof(read_data)));

    ASSERT_EQ(read_header.destination_node_id, 1u);
    ASSERT_EQ(read_header.source_node_id, 0u);
    ASSERT_EQ(read_size, sizeof(data));
    ASSERT_EQ(read_data[0], 0x01u);
    ASSERT_EQ(read_data[3], 0x04u);
}

TEST(proxy_buffer_write_multiple) {
    TestProxyBuffer buffer;

    for (uint32_t i = 0; i < 4; i++) {
        ryu_ldn::protocol::ProxyDataHeader header{};
        header.destination_node_id = i;
        header.source_node_id = 0;

        uint8_t data = static_cast<uint8_t>(i);
        ASSERT_TRUE(buffer.write(header, &data, 1));
    }

    ASSERT_EQ(buffer.pending_packets(), 4u);
}

TEST(proxy_buffer_read_order_fifo) {
    TestProxyBuffer buffer;

    // Write packets with different destination IDs
    for (uint32_t i = 0; i < 3; i++) {
        ryu_ldn::protocol::ProxyDataHeader header{};
        header.destination_node_id = i;
        header.source_node_id = 0;

        uint8_t data = static_cast<uint8_t>(i * 10);
        buffer.write(header, &data, 1);
    }

    // Read should be in FIFO order
    for (uint32_t i = 0; i < 3; i++) {
        ryu_ldn::protocol::ProxyDataHeader header{};
        uint8_t data;
        size_t size;
        ASSERT_TRUE(buffer.read(header, &data, size, 1));
        ASSERT_EQ(header.destination_node_id, i);
        ASSERT_EQ(data, static_cast<uint8_t>(i * 10));
    }
}

TEST(proxy_buffer_read_empty) {
    TestProxyBuffer buffer;
    ryu_ldn::protocol::ProxyDataHeader header{};
    uint8_t data[64];
    size_t size;

    ASSERT_FALSE(buffer.read(header, data, size, sizeof(data)));
}

TEST(proxy_buffer_reset) {
    TestProxyBuffer buffer;
    ryu_ldn::protocol::ProxyDataHeader header{};
    uint8_t data = 0x42;
    buffer.write(header, &data, 1);

    ASSERT_EQ(buffer.pending_packets(), 1u);

    buffer.reset();
    ASSERT_EQ(buffer.pending_packets(), 0u);
}

TEST(proxy_buffer_header_only) {
    TestProxyBuffer buffer;
    ryu_ldn::protocol::ProxyDataHeader header{};
    header.destination_node_id = 5;
    header.source_node_id = 2;

    // Write header only (no data)
    ASSERT_TRUE(buffer.write(header, nullptr, 0));

    ryu_ldn::protocol::ProxyDataHeader read_header{};
    uint8_t read_data[64];
    size_t read_size;
    ASSERT_TRUE(buffer.read(read_header, read_data, read_size, sizeof(read_data)));

    ASSERT_EQ(read_header.destination_node_id, 5u);
    ASSERT_EQ(read_header.source_node_id, 2u);
    ASSERT_EQ(read_size, 0u);
}

// ============================================================================
// Proxy Routing Logic Tests
// ============================================================================

// Simulates the routing decision
static bool should_route_to_node(const ryu_ldn::protocol::ProxyDataHeader& header,
                                 uint32_t target_node_id,
                                 const TestNodeMapper& mapper) {
    // Broadcast: route to all connected nodes except source
    if (header.destination_node_id == BROADCAST_NODE_ID) {
        return mapper.is_node_connected(target_node_id) &&
               target_node_id != header.source_node_id;
    }

    // Unicast: only route to destination node
    return header.destination_node_id == target_node_id &&
           mapper.is_node_connected(target_node_id);
}

TEST(routing_unicast_to_connected) {
    TestNodeMapper mapper;
    mapper.add_node(0, 0x0A720001);
    mapper.add_node(1, 0x0A720002);

    ryu_ldn::protocol::ProxyDataHeader header{};
    header.destination_node_id = 1;
    header.source_node_id = 0;

    ASSERT_TRUE(should_route_to_node(header, 1, mapper));
    ASSERT_FALSE(should_route_to_node(header, 0, mapper)); // Not destination
    ASSERT_FALSE(should_route_to_node(header, 2, mapper)); // Not connected
}

TEST(routing_unicast_to_disconnected) {
    TestNodeMapper mapper;
    mapper.add_node(0, 0x0A720001);
    // Node 1 not connected

    ryu_ldn::protocol::ProxyDataHeader header{};
    header.destination_node_id = 1;
    header.source_node_id = 0;

    ASSERT_FALSE(should_route_to_node(header, 1, mapper)); // Not connected
}

TEST(routing_broadcast_all_nodes) {
    TestNodeMapper mapper;
    mapper.add_node(0, 0x0A720001);
    mapper.add_node(1, 0x0A720002);
    mapper.add_node(2, 0x0A720003);

    ryu_ldn::protocol::ProxyDataHeader header{};
    header.destination_node_id = BROADCAST_NODE_ID;
    header.source_node_id = 0;

    ASSERT_FALSE(should_route_to_node(header, 0, mapper)); // Source excluded
    ASSERT_TRUE(should_route_to_node(header, 1, mapper));
    ASSERT_TRUE(should_route_to_node(header, 2, mapper));
    ASSERT_FALSE(should_route_to_node(header, 3, mapper)); // Not connected
}

TEST(routing_broadcast_excludes_source) {
    TestNodeMapper mapper;
    mapper.add_node(0, 0x0A720001);
    mapper.add_node(1, 0x0A720002);

    ryu_ldn::protocol::ProxyDataHeader header{};
    header.destination_node_id = BROADCAST_NODE_ID;
    header.source_node_id = 0;

    // Broadcast should not go back to source
    ASSERT_FALSE(should_route_to_node(header, 0, mapper));
}

// ============================================================================
// Data Size Tests
// ============================================================================

TEST(proxy_max_data_size) {
    TestProxyBuffer buffer;
    ryu_ldn::protocol::ProxyDataHeader header{};

    // Write maximum size data
    std::vector<uint8_t> large_data(MAX_PROXY_DATA_SIZE, 0xAA);
    ASSERT_TRUE(buffer.write(header, large_data.data(), large_data.size()));

    ryu_ldn::protocol::ProxyDataHeader read_header{};
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
        ryu_ldn::protocol::ProxyDataHeader header{};
        header.destination_node_id = 1;
        std::vector<uint8_t> data(s, 0x55);
        ASSERT_TRUE(buffer.write(header, data.data(), data.size()));
    }

    // Read and verify sizes
    for (size_t expected_size : sizes) {
        ryu_ldn::protocol::ProxyDataHeader header{};
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
    mapper.add_node(0, 0x0A720001);

    ryu_ldn::protocol::ProxyDataHeader header{};
    header.destination_node_id = 0;
    header.source_node_id = 0;

    // Sending to self - router should allow but game might filter
    ASSERT_TRUE(should_route_to_node(header, 0, mapper));
}

TEST(proxy_header_byte_layout) {
    ryu_ldn::protocol::ProxyDataHeader header{};
    header.destination_node_id = 0x12345678;
    header.source_node_id = 0xABCDEF01;

    uint8_t* bytes = reinterpret_cast<uint8_t*>(&header);

    // Little-endian: destination should be first 4 bytes
    ASSERT_EQ(bytes[0], 0x78u);
    ASSERT_EQ(bytes[1], 0x56u);
    ASSERT_EQ(bytes[2], 0x34u);
    ASSERT_EQ(bytes[3], 0x12u);

    // Source should be next 4 bytes
    ASSERT_EQ(bytes[4], 0x01u);
    ASSERT_EQ(bytes[5], 0xEFu);
    ASSERT_EQ(bytes[6], 0xCDu);
    ASSERT_EQ(bytes[7], 0xABu);
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