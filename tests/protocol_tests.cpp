/**
 * @file protocol_tests.cpp
 * @brief Unit tests for RyuLdn Protocol module
 *
 * Tests cover:
 * - Structure sizes and alignment (static_assert compile-time)
 * - Packet encoding
 * - Packet decoding
 * - Encode/decode round-trip
 * - Error handling (invalid packets)
 * - PacketBuffer TCP fragmentation handling
 */

#include "protocol/types.hpp"
#include "protocol/ryu_protocol.hpp"
#include "protocol/packet_buffer.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdexcept>

using namespace ryu_ldn::protocol;

// ============================================================================
// Test Framework (minimal, no external dependencies)
// ============================================================================

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct TestRegister_##name { \
        TestRegister_##name() { register_test(#name, test_##name); } \
    } g_register_##name; \
    static void test_##name()

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            printf("  FAIL: %s (line %d)\n", #expr, __LINE__); \
            throw std::runtime_error("assertion failed"); \
        } \
    } while(0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b) \
    do { \
        auto _a = static_cast<long long>(a); \
        auto _b = static_cast<long long>(b); \
        if (_a != _b) { \
            printf("  FAIL: %s == %s (line %d)\n", #a, #b, __LINE__); \
            printf("    got: %lld vs %lld\n", _a, _b); \
            throw std::runtime_error("assertion failed"); \
        } \
    } while(0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            printf("  FAIL: %s != %s (line %d)\n", #a, #b, __LINE__); \
            throw std::runtime_error("assertion failed"); \
        } \
    } while(0)

struct TestEntry {
    const char* name;
    void (*func)();
};

static TestEntry g_tests[256];
static int g_test_count = 0;

void register_test(const char* name, void (*func)()) {
    if (g_test_count < 256) {
        g_tests[g_test_count++] = {name, func};
    }
}

void run_all_tests() {
    printf("Running %d tests...\n\n", g_test_count);

    for (int i = 0; i < g_test_count; i++) {
        printf("[%d/%d] %s... ", i + 1, g_test_count, g_tests[i].name);
        fflush(stdout);
        g_tests_run++;

        try {
            g_tests[i].func();
            printf("OK\n");
            g_tests_passed++;
        } catch (...) {
            g_tests_failed++;
        }
    }

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed, %d total\n",
           g_tests_passed, g_tests_failed, g_tests_run);

    if (g_tests_failed > 0) {
        printf("FAILED\n");
    } else {
        printf("ALL TESTS PASSED\n");
    }
}

// ============================================================================
// Structure Size Tests (compile-time via static_assert in types.hpp)
// These tests verify at runtime that sizes match expected values
// ============================================================================

TEST(structure_sizes) {
    // Core structures
    ASSERT_EQ(sizeof(LdnHeader), 0xA);  // 10 bytes
    ASSERT_EQ(sizeof(MacAddress), 6);
    ASSERT_EQ(sizeof(Ssid), 0x22);      // 34 bytes
    ASSERT_EQ(sizeof(NetworkId), 0x20); // 32 bytes
    ASSERT_EQ(sizeof(SessionId), 0x10); // 16 bytes
    ASSERT_EQ(sizeof(IntentId), 0x10);  // 16 bytes

    // Node and Network Info
    ASSERT_EQ(sizeof(NodeInfo), 0x40);  // 64 bytes
    ASSERT_EQ(sizeof(CommonNetworkInfo), 0x30);  // 48 bytes
    ASSERT_EQ(sizeof(LdnNetworkInfo), 0x430);    // 1072 bytes
    ASSERT_EQ(sizeof(NetworkInfo), 0x480);       // 1152 bytes

    // Messages
    ASSERT_EQ(sizeof(InitializeMessage), 0x16);  // 22 bytes
    ASSERT_EQ(sizeof(PassphraseMessage), 0x80);  // 128 bytes
    ASSERT_EQ(sizeof(PingMessage), 2);
    ASSERT_EQ(sizeof(DisconnectMessage), 4);     // 4 bytes (DisconnectIP only)

    // Request structures
    ASSERT_EQ(sizeof(SecurityConfig), 0x44);
    ASSERT_EQ(sizeof(UserConfig), 0x30);
    ASSERT_EQ(sizeof(NetworkConfig), 0x20);
    ASSERT_EQ(sizeof(RyuNetworkConfig), 0x28);
    ASSERT_EQ(sizeof(CreateAccessPointRequest), 0xBC);
    ASSERT_EQ(sizeof(ScanFilterFull), 0x60);     // 96 bytes (Pack=8 alignment)
    ASSERT_EQ(sizeof(ConnectRequest), 0x4FC);
    ASSERT_EQ(sizeof(RejectRequest), 8);         // 8 bytes (NodeId + DisconnectReason)

    // Proxy structures
    ASSERT_EQ(sizeof(ProxyInfo), 0x10);          // 16 bytes
    ASSERT_EQ(sizeof(ProxyConfig), 8);           // 8 bytes (ip + subnetmask)
    ASSERT_EQ(sizeof(ProxyDataHeader), 0x14);    // 20 bytes (ProxyInfo + DataLength)
    ASSERT_EQ(sizeof(ProxyConnectRequest), 0x10);  // 16 bytes (ProxyInfo)
    ASSERT_EQ(sizeof(ProxyConnectResponse), 0x10); // 16 bytes (ProxyInfo)
    ASSERT_EQ(sizeof(ProxyDisconnectMessage), 0x14); // 20 bytes (ProxyInfo + reason)
}

TEST(protocol_constants) {
    // PROTOCOL_MAGIC = 'R' | ('L' << 8) | ('D' << 16) | ('N' << 24) = 0x4E444C52
    ASSERT_EQ(PROTOCOL_MAGIC, 0x4E444C52u);
    ASSERT_EQ(PROTOCOL_VERSION, 1);
}

// ============================================================================
// Encode Tests
// ============================================================================

TEST(encode_header_only_packet) {
    uint8_t buffer[64];
    size_t out_size = 0;

    EncodeResult result = encode(buffer, sizeof(buffer), PacketId::ScanReplyEnd, out_size);

    ASSERT_EQ(result, EncodeResult::Success);
    ASSERT_EQ(out_size, sizeof(LdnHeader));

    // Verify header contents
    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    ASSERT_EQ(header->magic, PROTOCOL_MAGIC);
    ASSERT_EQ(header->type, static_cast<uint8_t>(PacketId::ScanReplyEnd));
    ASSERT_EQ(header->version, PROTOCOL_VERSION);
    ASSERT_EQ(header->data_size, 0);
}

TEST(encode_ping_packet) {
    uint8_t buffer[64];
    size_t out_size = 0;
    uint8_t requester = 1;
    uint8_t id = 42;

    EncodeResult result = encode_ping(buffer, sizeof(buffer), requester, id, out_size);

    ASSERT_EQ(result, EncodeResult::Success);
    ASSERT_EQ(out_size, sizeof(LdnHeader) + sizeof(PingMessage));

    // Verify header
    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    ASSERT_EQ(header->magic, PROTOCOL_MAGIC);
    ASSERT_EQ(header->type, static_cast<uint8_t>(PacketId::Ping));
    ASSERT_EQ(header->data_size, sizeof(PingMessage));

    // Verify payload
    PingMessage* msg = reinterpret_cast<PingMessage*>(buffer + sizeof(LdnHeader));
    ASSERT_EQ(msg->requester, requester);
    ASSERT_EQ(msg->id, id);
}

TEST(encode_initialize_packet) {
    uint8_t buffer[64];
    size_t out_size = 0;

    SessionId session_id = {};
    // Set first 8 bytes to a recognizable pattern
    session_id.data[0] = 0xDE;
    session_id.data[1] = 0xAD;
    session_id.data[2] = 0xBE;
    session_id.data[3] = 0xEF;

    MacAddress mac = {};
    mac.data[0] = 0x11;
    mac.data[1] = 0x22;
    mac.data[2] = 0x33;
    mac.data[3] = 0x44;
    mac.data[4] = 0x55;
    mac.data[5] = 0x66;

    EncodeResult result = encode_initialize(buffer, sizeof(buffer), session_id, mac, out_size);

    ASSERT_EQ(result, EncodeResult::Success);
    ASSERT_EQ(out_size, sizeof(LdnHeader) + sizeof(InitializeMessage));

    // Verify payload
    InitializeMessage* msg = reinterpret_cast<InitializeMessage*>(buffer + sizeof(LdnHeader));
    ASSERT_EQ(msg->id.data[0], 0xDE);
    ASSERT_EQ(msg->id.data[3], 0xEF);
    ASSERT_EQ(msg->mac_address.data[0], 0x11);
    ASSERT_EQ(msg->mac_address.data[5], 0x66);
}

TEST(encode_buffer_too_small) {
    uint8_t buffer[4];  // Too small for header
    size_t out_size = 0;

    EncodeResult result = encode(buffer, sizeof(buffer), PacketId::Ping, out_size);

    ASSERT_EQ(result, EncodeResult::BufferTooSmall);
    ASSERT_EQ(out_size, 0);
}

TEST(encode_disconnect_packet) {
    uint8_t buffer[64];
    size_t out_size = 0;

    // Disconnect message now contains IP address of disconnecting client
    uint32_t disconnect_ip = 0xC0A80101;  // 192.168.1.1
    EncodeResult result = encode_disconnect(buffer, sizeof(buffer),
                                            disconnect_ip, out_size);

    ASSERT_EQ(result, EncodeResult::Success);

    DisconnectMessage* msg = reinterpret_cast<DisconnectMessage*>(buffer + sizeof(LdnHeader));
    ASSERT_EQ(msg->disconnect_ip, disconnect_ip);
}

// ============================================================================
// Decode Tests
// ============================================================================

TEST(decode_valid_header) {
    uint8_t buffer[sizeof(LdnHeader)];
    LdnHeader* header_in = reinterpret_cast<LdnHeader*>(buffer);
    header_in->magic = PROTOCOL_MAGIC;
    header_in->type = static_cast<uint8_t>(PacketId::Ping);
    header_in->version = PROTOCOL_VERSION;
    header_in->data_size = 8;

    LdnHeader header_out;
    DecodeResult result = decode_header(buffer, sizeof(buffer), header_out);

    ASSERT_EQ(result, DecodeResult::Success);
    ASSERT_EQ(header_out.magic, PROTOCOL_MAGIC);
    ASSERT_EQ(header_out.type, static_cast<uint8_t>(PacketId::Ping));
    ASSERT_EQ(header_out.data_size, 8);
}

TEST(decode_invalid_magic) {
    uint8_t buffer[sizeof(LdnHeader)];
    LdnHeader* header_in = reinterpret_cast<LdnHeader*>(buffer);
    header_in->magic = 0xDEADBEEF;  // Wrong magic
    header_in->type = 0;
    header_in->version = PROTOCOL_VERSION;
    header_in->data_size = 0;

    LdnHeader header_out;
    DecodeResult result = decode_header(buffer, sizeof(buffer), header_out);

    ASSERT_EQ(result, DecodeResult::InvalidMagic);
}

TEST(decode_invalid_version) {
    uint8_t buffer[sizeof(LdnHeader)];
    LdnHeader* header_in = reinterpret_cast<LdnHeader*>(buffer);
    header_in->magic = PROTOCOL_MAGIC;
    header_in->type = 0;
    header_in->version = 99;  // Wrong version
    header_in->data_size = 0;

    LdnHeader header_out;
    DecodeResult result = decode_header(buffer, sizeof(buffer), header_out);

    ASSERT_EQ(result, DecodeResult::InvalidVersion);
}

TEST(decode_packet_too_large) {
    uint8_t buffer[sizeof(LdnHeader)];
    LdnHeader* header_in = reinterpret_cast<LdnHeader*>(buffer);
    header_in->magic = PROTOCOL_MAGIC;
    header_in->type = 0;
    header_in->version = PROTOCOL_VERSION;
    // MAX_PACKET_SIZE is 131072, so 131073 should be rejected
    header_in->data_size = 131073;

    LdnHeader header_out;
    DecodeResult result = decode_header(buffer, sizeof(buffer), header_out);

    ASSERT_EQ(result, DecodeResult::PacketTooLarge);
}

TEST(decode_buffer_too_small) {
    uint8_t buffer[4] = {0};  // Less than header size, initialized

    LdnHeader header_out;
    DecodeResult result = decode_header(buffer, sizeof(buffer), header_out);

    ASSERT_EQ(result, DecodeResult::BufferTooSmall);
}

TEST(decode_ping_packet) {
    uint8_t buffer[sizeof(LdnHeader) + sizeof(PingMessage)];

    // Build packet
    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    header->magic = PROTOCOL_MAGIC;
    header->type = static_cast<uint8_t>(PacketId::Ping);
    header->version = PROTOCOL_VERSION;
    header->data_size = sizeof(PingMessage);

    PingMessage* msg_in = reinterpret_cast<PingMessage*>(buffer + sizeof(LdnHeader));
    msg_in->requester = 0;
    msg_in->id = 99;

    // Decode
    LdnHeader header_out;
    PingMessage msg_out;
    DecodeResult result = decode_ping(buffer, sizeof(buffer), header_out, msg_out);

    ASSERT_EQ(result, DecodeResult::Success);
    ASSERT_EQ(msg_out.requester, 0);
    ASSERT_EQ(msg_out.id, 99);
}

TEST(check_complete_packet_success) {
    uint8_t buffer[sizeof(LdnHeader) + 8];

    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    header->magic = PROTOCOL_MAGIC;
    header->type = static_cast<uint8_t>(PacketId::Ping);
    header->version = PROTOCOL_VERSION;
    header->data_size = 8;

    size_t packet_size;
    DecodeResult result = check_complete_packet(buffer, sizeof(buffer), packet_size);

    ASSERT_EQ(result, DecodeResult::Success);
    ASSERT_EQ(packet_size, sizeof(LdnHeader) + 8);
}

TEST(check_complete_packet_incomplete) {
    uint8_t buffer[sizeof(LdnHeader) + 4];  // Only 4 bytes of payload

    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    header->magic = PROTOCOL_MAGIC;
    header->type = static_cast<uint8_t>(PacketId::Ping);
    header->version = PROTOCOL_VERSION;
    header->data_size = 8;  // Says 8 bytes, but buffer only has 4

    size_t packet_size;
    DecodeResult result = check_complete_packet(buffer, sizeof(buffer), packet_size);

    ASSERT_EQ(result, DecodeResult::IncompletePacket);
}

// ============================================================================
// Round-Trip Tests (Encode then Decode)
// ============================================================================

TEST(roundtrip_ping) {
    uint8_t buffer[64];
    size_t encoded_size = 0;
    uint8_t original_requester = 0;
    uint8_t original_id = 55;

    // Encode
    EncodeResult enc_result = encode_ping(buffer, sizeof(buffer), original_requester, original_id, encoded_size);
    ASSERT_EQ(enc_result, EncodeResult::Success);

    // Decode
    LdnHeader header;
    PingMessage msg;
    DecodeResult dec_result = decode_ping(buffer, encoded_size, header, msg);

    ASSERT_EQ(dec_result, DecodeResult::Success);
    ASSERT_EQ(msg.requester, original_requester);
    ASSERT_EQ(msg.id, original_id);
}

TEST(roundtrip_disconnect) {
    uint8_t buffer[64];
    size_t encoded_size = 0;

    // Encode with IP address (new format)
    uint32_t disconnect_ip = 0x0A000001;  // 10.0.0.1
    EncodeResult enc_result = encode_disconnect(buffer, sizeof(buffer),
                                                disconnect_ip, encoded_size);
    ASSERT_EQ(enc_result, EncodeResult::Success);

    // Decode
    LdnHeader header;
    DisconnectMessage msg;
    DecodeResult dec_result = decode_disconnect(buffer, encoded_size, header, msg);

    ASSERT_EQ(dec_result, DecodeResult::Success);
    ASSERT_EQ(msg.disconnect_ip, disconnect_ip);
}

TEST(roundtrip_initialize) {
    uint8_t buffer[64];
    size_t encoded_size = 0;

    SessionId session = {};
    session.data[0] = 0xCA;
    session.data[1] = 0xFE;
    session.data[2] = 0xBA;
    session.data[3] = 0xBE;

    MacAddress mac = {};
    mac.data[0] = 0xAA;
    mac.data[5] = 0xFF;

    // Encode
    EncodeResult enc_result = encode_initialize(buffer, sizeof(buffer), session, mac, encoded_size);
    ASSERT_EQ(enc_result, EncodeResult::Success);

    // Decode
    LdnHeader header;
    InitializeMessage msg;
    DecodeResult dec_result = decode_initialize(buffer, encoded_size, header, msg);

    ASSERT_EQ(dec_result, DecodeResult::Success);
    ASSERT_EQ(msg.id.data[0], session.data[0]);
    ASSERT_EQ(msg.id.data[3], session.data[3]);
    ASSERT_EQ(msg.mac_address.data[0], 0xAA);
    ASSERT_EQ(msg.mac_address.data[5], 0xFF);
}

// ============================================================================
// PacketBuffer Tests
// ============================================================================

TEST(buffer_empty_initially) {
    PacketBuffer<1024> buffer;

    ASSERT_TRUE(buffer.empty());
    ASSERT_EQ(buffer.size(), 0);
    ASSERT_FALSE(buffer.has_complete_packet());
}

TEST(buffer_append_data) {
    PacketBuffer<1024> buffer;
    uint8_t data[] = {1, 2, 3, 4, 5};

    BufferResult result = buffer.append(data, sizeof(data));

    ASSERT_EQ(result, BufferResult::Success);
    ASSERT_EQ(buffer.size(), 5);
    ASSERT_FALSE(buffer.empty());
}

TEST(buffer_complete_packet_single_append) {
    PacketBuffer<1024> buffer;

    // Create a complete packet (requester=1, id=23)
    uint8_t packet[sizeof(LdnHeader) + sizeof(PingMessage)];
    size_t packet_size;
    encode_ping(packet, sizeof(packet), 1, 23, packet_size);

    // Append entire packet
    buffer.append(packet, packet_size);

    ASSERT_TRUE(buffer.has_complete_packet());

    size_t peek_size;
    const uint8_t* peek_data = buffer.peek_packet(peek_size);
    ASSERT_NE(peek_data, nullptr);
    ASSERT_EQ(peek_size, packet_size);
}

TEST(buffer_fragmented_packet_2_parts) {
    PacketBuffer<1024> buffer;

    // Create a complete packet (requester=1, id=23)
    uint8_t packet[sizeof(LdnHeader) + sizeof(PingMessage)];
    size_t packet_size;
    encode_ping(packet, sizeof(packet), 1, 23, packet_size);

    // Append in 2 parts
    size_t part1 = sizeof(LdnHeader) / 2;  // Half of header
    size_t part2 = packet_size - part1;

    buffer.append(packet, part1);
    ASSERT_FALSE(buffer.has_complete_packet());

    buffer.append(packet + part1, part2);
    ASSERT_TRUE(buffer.has_complete_packet());
}

TEST(buffer_fragmented_packet_n_parts) {
    PacketBuffer<1024> buffer;

    // Create a complete packet (requester=1, id=42)
    uint8_t packet[sizeof(LdnHeader) + sizeof(PingMessage)];
    size_t packet_size;
    encode_ping(packet, sizeof(packet), 1, 42, packet_size);

    // Append byte by byte
    for (size_t i = 0; i < packet_size - 1; i++) {
        buffer.append(&packet[i], 1);
        ASSERT_FALSE(buffer.has_complete_packet());
    }

    // Last byte completes the packet
    buffer.append(&packet[packet_size - 1], 1);
    ASSERT_TRUE(buffer.has_complete_packet());
}

TEST(buffer_multiple_packets) {
    PacketBuffer<1024> buffer;

    // Create 3 packets
    uint8_t packet1[32], packet2[32], packet3[32];
    size_t size1, size2, size3;

    encode_ping(packet1, sizeof(packet1), 1, 11, size1);
    encode_ping(packet2, sizeof(packet2), 0, 22, size2);
    encode_ping(packet3, sizeof(packet3), 1, 33, size3);

    // Append all at once
    buffer.append(packet1, size1);
    buffer.append(packet2, size2);
    buffer.append(packet3, size3);

    // Extract and verify each
    ASSERT_TRUE(buffer.has_complete_packet());

    size_t pkt_size;
    buffer.peek_packet(pkt_size);
    buffer.consume(pkt_size);

    ASSERT_TRUE(buffer.has_complete_packet());
    buffer.peek_packet(pkt_size);
    buffer.consume(pkt_size);

    ASSERT_TRUE(buffer.has_complete_packet());
    buffer.peek_packet(pkt_size);
    buffer.consume(pkt_size);

    ASSERT_FALSE(buffer.has_complete_packet());
    ASSERT_TRUE(buffer.empty());
}

TEST(buffer_extract_packet) {
    PacketBuffer<1024> buffer;

    uint8_t packet[32];
    size_t packet_size;
    encode_ping(packet, sizeof(packet), 0, 77, packet_size);

    buffer.append(packet, packet_size);

    uint8_t out[32];
    size_t out_size;
    BufferResult result = buffer.extract_packet(out, sizeof(out), out_size);

    ASSERT_EQ(result, BufferResult::Success);
    ASSERT_EQ(out_size, packet_size);
    ASSERT_TRUE(buffer.empty());

    // Verify extracted data
    LdnHeader header;
    PingMessage msg;
    decode_ping(out, out_size, header, msg);
    ASSERT_EQ(msg.requester, 0);
    ASSERT_EQ(msg.id, 77);
}

TEST(buffer_overflow_protection) {
    PacketBuffer<64> buffer;  // Small buffer

    uint8_t data[128];
    std::memset(data, 0, sizeof(data));

    BufferResult result = buffer.append(data, sizeof(data));

    ASSERT_EQ(result, BufferResult::BufferFull);
}

TEST(buffer_reset) {
    PacketBuffer<1024> buffer;

    uint8_t data[] = {1, 2, 3, 4, 5};
    buffer.append(data, sizeof(data));
    ASSERT_FALSE(buffer.empty());

    buffer.reset();

    ASSERT_TRUE(buffer.empty());
    ASSERT_EQ(buffer.size(), 0);
}

// ============================================================================
// Utility Function Tests
// ============================================================================

TEST(result_to_string) {
    ASSERT_TRUE(std::strcmp(decode_result_to_string(DecodeResult::Success), "Success") == 0);
    ASSERT_TRUE(std::strcmp(decode_result_to_string(DecodeResult::InvalidMagic), "InvalidMagic") == 0);

    ASSERT_TRUE(std::strcmp(encode_result_to_string(EncodeResult::Success), "Success") == 0);
    ASSERT_TRUE(std::strcmp(encode_result_to_string(EncodeResult::BufferTooSmall), "BufferTooSmall") == 0);

    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::Initialize), "Initialize") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::Ping), "Ping") == 0);

    ASSERT_TRUE(std::strcmp(buffer_result_to_string(BufferResult::Success), "Success") == 0);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== ryu_ldn_nx Protocol Unit Tests ===\n\n");
    run_all_tests();
    return g_tests_failed > 0 ? 1 : 0;
}
