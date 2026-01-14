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
// Additional Encode/Decode Tests (Full Coverage)
// ============================================================================

TEST(encode_passphrase_packet) {
    uint8_t buffer[256];
    size_t out_size = 0;
    const char* passphrase = "TestPassphrase123";

    EncodeResult result = encode_passphrase(buffer, sizeof(buffer),
                                            reinterpret_cast<const uint8_t*>(passphrase),
                                            std::strlen(passphrase), out_size);

    ASSERT_EQ(result, EncodeResult::Success);
    ASSERT_EQ(out_size, sizeof(LdnHeader) + sizeof(PassphraseMessage));

    // Verify header
    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    ASSERT_EQ(header->type, static_cast<uint8_t>(PacketId::Passphrase));
    ASSERT_EQ(header->data_size, static_cast<int32_t>(sizeof(PassphraseMessage)));

    // Verify payload
    PassphraseMessage* msg = reinterpret_cast<PassphraseMessage*>(buffer + sizeof(LdnHeader));
    ASSERT_TRUE(std::memcmp(msg->passphrase, passphrase, std::strlen(passphrase)) == 0);
}

TEST(roundtrip_passphrase) {
    uint8_t buffer[256];
    size_t encoded_size = 0;
    const char* original = "MySecretPass";

    // Encode
    EncodeResult enc_result = encode_passphrase(buffer, sizeof(buffer),
                                                reinterpret_cast<const uint8_t*>(original),
                                                std::strlen(original), encoded_size);
    ASSERT_EQ(enc_result, EncodeResult::Success);

    // Decode
    LdnHeader header;
    PassphraseMessage msg;
    DecodeResult dec_result = decode_passphrase(buffer, encoded_size, header, msg);

    ASSERT_EQ(dec_result, DecodeResult::Success);
    ASSERT_TRUE(std::memcmp(msg.passphrase, original, std::strlen(original)) == 0);
}

TEST(encode_scan_packet) {
    uint8_t buffer[256];
    size_t out_size = 0;

    ScanFilterFull filter{};
    filter.flag = 0x25;  // Some filter flags
    filter.network_id.intent_id.local_communication_id = 0xDEADBEEF;

    EncodeResult result = encode_scan(buffer, sizeof(buffer), filter, out_size);

    ASSERT_EQ(result, EncodeResult::Success);
    ASSERT_EQ(out_size, sizeof(LdnHeader) + sizeof(ScanFilterFull));

    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    ASSERT_EQ(header->type, static_cast<uint8_t>(PacketId::Scan));
}

TEST(roundtrip_scan) {
    uint8_t buffer[256];
    size_t encoded_size = 0;

    ScanFilterFull original{};
    original.flag = 0x37;
    original.network_type = 2;
    original.network_id.intent_id.local_communication_id = 0xCAFEBABE;

    // Encode
    EncodeResult enc_result = encode_scan(buffer, sizeof(buffer), original, encoded_size);
    ASSERT_EQ(enc_result, EncodeResult::Success);

    // Decode
    LdnHeader header;
    ScanFilterFull decoded;
    DecodeResult dec_result = decode_scan(buffer, encoded_size, header, decoded);

    ASSERT_EQ(dec_result, DecodeResult::Success);
    ASSERT_EQ(decoded.flag, original.flag);
    ASSERT_EQ(decoded.network_type, original.network_type);
    ASSERT_EQ(decoded.network_id.intent_id.local_communication_id,
              original.network_id.intent_id.local_communication_id);
}

TEST(encode_connect_packet) {
    uint8_t buffer[2048];
    size_t out_size = 0;

    ConnectRequest request{};
    request.security_config.security_mode = static_cast<uint16_t>(SecurityMode::Product);
    request.security_config.passphrase_size = 32;
    request.user_config.user_name[0] = 'T';
    request.user_config.user_name[1] = 'e';
    request.user_config.user_name[2] = 's';
    request.user_config.user_name[3] = 't';
    request.network_info.ldn.security_mode = static_cast<uint16_t>(SecurityMode::Product);

    EncodeResult result = encode_connect(buffer, sizeof(buffer), request, out_size);

    ASSERT_EQ(result, EncodeResult::Success);
    ASSERT_EQ(out_size, sizeof(LdnHeader) + sizeof(ConnectRequest));
}

TEST(roundtrip_connect) {
    uint8_t buffer[2048];
    size_t encoded_size = 0;

    ConnectRequest original{};
    original.security_config.security_mode = static_cast<uint16_t>(SecurityMode::Debug);
    original.local_communication_version = 5;
    original.option_unknown = 0xAB;

    // Encode
    EncodeResult enc_result = encode_connect(buffer, sizeof(buffer), original, encoded_size);
    ASSERT_EQ(enc_result, EncodeResult::Success);

    // Decode
    LdnHeader header;
    ConnectRequest decoded;
    DecodeResult dec_result = decode_connect(buffer, encoded_size, header, decoded);

    ASSERT_EQ(dec_result, DecodeResult::Success);
    ASSERT_EQ(decoded.security_config.security_mode, original.security_config.security_mode);
    ASSERT_EQ(decoded.local_communication_version, original.local_communication_version);
    ASSERT_EQ(decoded.option_unknown, original.option_unknown);
}

TEST(encode_create_access_point_packet) {
    uint8_t buffer[512];
    size_t out_size = 0;

    CreateAccessPointRequest request{};
    request.security_config.security_mode = static_cast<uint16_t>(SecurityMode::Product);
    request.network_config.intent_id.local_communication_id = 0xAABBCCDD;
    request.ryu_network_config.internal_proxy_port = 8080;

    uint8_t advertise_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};

    EncodeResult result = encode_create_access_point(buffer, sizeof(buffer), request,
                                                     advertise_data, sizeof(advertise_data), out_size);

    ASSERT_EQ(result, EncodeResult::Success);
    ASSERT_EQ(out_size, sizeof(LdnHeader) + sizeof(CreateAccessPointRequest) + sizeof(advertise_data));

    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    ASSERT_EQ(header->type, static_cast<uint8_t>(PacketId::CreateAccessPoint));
}

TEST(roundtrip_create_access_point) {
    uint8_t buffer[512];
    size_t encoded_size = 0;

    CreateAccessPointRequest original{};
    original.security_config.security_mode = static_cast<uint16_t>(SecurityMode::Debug);
    original.ryu_network_config.internal_proxy_port = 12345;
    original.ryu_network_config.external_proxy_port = 54321;

    uint8_t original_data[] = {0xDE, 0xAD, 0xBE, 0xEF};

    // Encode
    EncodeResult enc_result = encode_create_access_point(buffer, sizeof(buffer), original,
                                                         original_data, sizeof(original_data), encoded_size);
    ASSERT_EQ(enc_result, EncodeResult::Success);

    // Decode
    LdnHeader header;
    CreateAccessPointRequest decoded;
    const uint8_t* decoded_data;
    size_t decoded_size;
    DecodeResult dec_result = decode_create_access_point(buffer, encoded_size, header, decoded,
                                                          decoded_data, decoded_size);

    ASSERT_EQ(dec_result, DecodeResult::Success);
    ASSERT_EQ(decoded.ryu_network_config.internal_proxy_port, original.ryu_network_config.internal_proxy_port);
    ASSERT_EQ(decoded.ryu_network_config.external_proxy_port, original.ryu_network_config.external_proxy_port);
    ASSERT_EQ(decoded_size, sizeof(original_data));
    ASSERT_TRUE(std::memcmp(decoded_data, original_data, decoded_size) == 0);
}

TEST(encode_set_accept_policy_packet) {
    uint8_t buffer[64];
    size_t out_size = 0;

    EncodeResult result = encode_set_accept_policy(buffer, sizeof(buffer),
                                                   AcceptPolicy::AcceptAll, out_size);

    ASSERT_EQ(result, EncodeResult::Success);
    ASSERT_EQ(out_size, sizeof(LdnHeader) + sizeof(SetAcceptPolicyRequest));

    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    ASSERT_EQ(header->type, static_cast<uint8_t>(PacketId::SetAcceptPolicy));
}

TEST(roundtrip_set_accept_policy) {
    uint8_t buffer[64];
    size_t encoded_size = 0;

    // Encode
    EncodeResult enc_result = encode_set_accept_policy(buffer, sizeof(buffer),
                                                       AcceptPolicy::BlackList, encoded_size);
    ASSERT_EQ(enc_result, EncodeResult::Success);

    // Decode
    LdnHeader header;
    SetAcceptPolicyRequest decoded;
    DecodeResult dec_result = decode_set_accept_policy(buffer, encoded_size, header, decoded);

    ASSERT_EQ(dec_result, DecodeResult::Success);
    ASSERT_EQ(decoded.accept_policy, static_cast<uint8_t>(AcceptPolicy::BlackList));
}

TEST(encode_set_advertise_data_packet) {
    uint8_t buffer[512];
    size_t out_size = 0;

    uint8_t data[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

    EncodeResult result = encode_set_advertise_data(buffer, sizeof(buffer),
                                                    data, sizeof(data), out_size);

    ASSERT_EQ(result, EncodeResult::Success);
    ASSERT_EQ(out_size, sizeof(LdnHeader) + sizeof(data));

    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    ASSERT_EQ(header->type, static_cast<uint8_t>(PacketId::SetAdvertiseData));
    ASSERT_EQ(header->data_size, static_cast<int32_t>(sizeof(data)));
}

TEST(roundtrip_set_advertise_data) {
    uint8_t buffer[512];
    size_t encoded_size = 0;

    uint8_t original_data[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    // Encode
    EncodeResult enc_result = encode_set_advertise_data(buffer, sizeof(buffer),
                                                        original_data, sizeof(original_data), encoded_size);
    ASSERT_EQ(enc_result, EncodeResult::Success);

    // Decode
    LdnHeader header;
    const uint8_t* decoded_data;
    size_t decoded_size;
    DecodeResult dec_result = decode_set_advertise_data(buffer, encoded_size, header,
                                                         decoded_data, decoded_size);

    ASSERT_EQ(dec_result, DecodeResult::Success);
    ASSERT_EQ(decoded_size, sizeof(original_data));
    ASSERT_TRUE(std::memcmp(decoded_data, original_data, decoded_size) == 0);
}

TEST(encode_proxy_data_packet) {
    uint8_t buffer[512];
    size_t out_size = 0;

    ProxyInfo info{};
    info.source_ipv4 = 0x0A720001;  // 10.114.0.1
    info.source_port = 12345;
    info.dest_ipv4 = 0x0A720002;    // 10.114.0.2
    info.dest_port = 54321;
    info.protocol = ProtocolType::Udp;

    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};

    EncodeResult result = encode_proxy_data(buffer, sizeof(buffer), info,
                                            payload, sizeof(payload), out_size);

    ASSERT_EQ(result, EncodeResult::Success);
    ASSERT_EQ(out_size, sizeof(LdnHeader) + sizeof(ProxyDataHeader) + sizeof(payload));

    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    ASSERT_EQ(header->type, static_cast<uint8_t>(PacketId::ProxyData));
}

TEST(roundtrip_proxy_data) {
    uint8_t buffer[512];
    size_t encoded_size = 0;

    ProxyInfo original_info{};
    original_info.source_ipv4 = 0xC0A80101;  // 192.168.1.1
    original_info.source_port = 8888;
    original_info.dest_ipv4 = 0xC0A80102;    // 192.168.1.2
    original_info.dest_port = 9999;
    original_info.protocol = ProtocolType::Tcp;

    uint8_t original_payload[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};

    // Encode
    EncodeResult enc_result = encode_proxy_data(buffer, sizeof(buffer), original_info,
                                                original_payload, sizeof(original_payload), encoded_size);
    ASSERT_EQ(enc_result, EncodeResult::Success);

    // Decode
    LdnHeader header;
    ProxyDataHeader proxy_header;
    const uint8_t* decoded_data;
    size_t decoded_size;
    DecodeResult dec_result = decode_proxy_data(buffer, encoded_size, header, proxy_header,
                                                 decoded_data, decoded_size);

    ASSERT_EQ(dec_result, DecodeResult::Success);
    ASSERT_EQ(proxy_header.info.source_ipv4, original_info.source_ipv4);
    ASSERT_EQ(proxy_header.info.source_port, original_info.source_port);
    ASSERT_EQ(proxy_header.info.dest_ipv4, original_info.dest_ipv4);
    ASSERT_EQ(proxy_header.info.dest_port, original_info.dest_port);
    ASSERT_EQ(static_cast<int>(proxy_header.info.protocol), static_cast<int>(original_info.protocol));
    ASSERT_EQ(proxy_header.data_length, sizeof(original_payload));
    ASSERT_EQ(decoded_size, sizeof(original_payload));
    ASSERT_TRUE(std::memcmp(decoded_data, original_payload, decoded_size) == 0);
}

TEST(decode_proxy_connect_packet) {
    uint8_t buffer[64];

    // Build packet manually
    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    header->magic = PROTOCOL_MAGIC;
    header->type = static_cast<uint8_t>(PacketId::ProxyConnect);
    header->version = PROTOCOL_VERSION;
    header->data_size = sizeof(ProxyConnectRequest);

    ProxyConnectRequest* req = reinterpret_cast<ProxyConnectRequest*>(buffer + sizeof(LdnHeader));
    req->info.source_ipv4 = 0x0A000001;
    req->info.dest_ipv4 = 0x0A000002;
    req->info.source_port = 1234;
    req->info.dest_port = 5678;

    // Decode
    LdnHeader decoded_header;
    ProxyConnectRequest decoded_req;
    DecodeResult result = decode_proxy_connect(buffer, sizeof(LdnHeader) + sizeof(ProxyConnectRequest),
                                                decoded_header, decoded_req);

    ASSERT_EQ(result, DecodeResult::Success);
    ASSERT_EQ(decoded_req.info.source_ipv4, 0x0A000001u);
    ASSERT_EQ(decoded_req.info.dest_ipv4, 0x0A000002u);
    ASSERT_EQ(decoded_req.info.source_port, 1234u);
    ASSERT_EQ(decoded_req.info.dest_port, 5678u);
}

TEST(decode_proxy_connect_reply_packet) {
    uint8_t buffer[64];

    // Build packet manually
    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    header->magic = PROTOCOL_MAGIC;
    header->type = static_cast<uint8_t>(PacketId::ProxyConnectReply);
    header->version = PROTOCOL_VERSION;
    header->data_size = sizeof(ProxyConnectResponse);

    ProxyConnectResponse* resp = reinterpret_cast<ProxyConnectResponse*>(buffer + sizeof(LdnHeader));
    resp->info.source_ipv4 = 0x0A000002;
    resp->info.dest_ipv4 = 0x0A000001;
    resp->info.source_port = 5678;
    resp->info.dest_port = 1234;

    // Decode
    LdnHeader decoded_header;
    ProxyConnectResponse decoded_resp;
    DecodeResult result = decode_proxy_connect_reply(buffer, sizeof(LdnHeader) + sizeof(ProxyConnectResponse),
                                                      decoded_header, decoded_resp);

    ASSERT_EQ(result, DecodeResult::Success);
    ASSERT_EQ(decoded_resp.info.source_ipv4, 0x0A000002u);
    ASSERT_EQ(decoded_resp.info.dest_ipv4, 0x0A000001u);
}

TEST(decode_proxy_disconnect_packet) {
    uint8_t buffer[64];

    // Build packet manually
    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    header->magic = PROTOCOL_MAGIC;
    header->type = static_cast<uint8_t>(PacketId::ProxyDisconnect);
    header->version = PROTOCOL_VERSION;
    header->data_size = sizeof(ProxyDisconnectMessage);

    ProxyDisconnectMessage* msg = reinterpret_cast<ProxyDisconnectMessage*>(buffer + sizeof(LdnHeader));
    msg->info.source_ipv4 = 0x0A000001;
    msg->info.dest_ipv4 = 0x0A000002;
    msg->disconnect_reason = static_cast<int32_t>(DisconnectReason::User);

    // Decode
    LdnHeader decoded_header;
    ProxyDisconnectMessage decoded_msg;
    DecodeResult result = decode_proxy_disconnect(buffer, sizeof(LdnHeader) + sizeof(ProxyDisconnectMessage),
                                                   decoded_header, decoded_msg);

    ASSERT_EQ(result, DecodeResult::Success);
    ASSERT_EQ(decoded_msg.info.source_ipv4, 0x0A000001u);
    ASSERT_EQ(decoded_msg.info.dest_ipv4, 0x0A000002u);
    ASSERT_EQ(static_cast<int>(decoded_msg.disconnect_reason),
              static_cast<int>(DisconnectReason::User));
}

TEST(decode_reject_packet) {
    uint8_t buffer[64];

    // Build packet manually
    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    header->magic = PROTOCOL_MAGIC;
    header->type = static_cast<uint8_t>(PacketId::Reject);
    header->version = PROTOCOL_VERSION;
    header->data_size = sizeof(RejectRequest);

    RejectRequest* req = reinterpret_cast<RejectRequest*>(buffer + sizeof(LdnHeader));
    req->node_id = 3;
    req->disconnect_reason = static_cast<uint32_t>(DisconnectReason::Rejected);

    // Decode
    LdnHeader decoded_header;
    RejectRequest decoded_req;
    DecodeResult result = decode_reject(buffer, sizeof(LdnHeader) + sizeof(RejectRequest),
                                         decoded_header, decoded_req);

    ASSERT_EQ(result, DecodeResult::Success);
    ASSERT_EQ(decoded_req.node_id, 3u);
    ASSERT_EQ(static_cast<int>(decoded_req.disconnect_reason),
              static_cast<int>(DisconnectReason::Rejected));
}

TEST(encode_network_info_packet) {
    uint8_t buffer[2048];
    size_t out_size = 0;

    NetworkInfo info{};
    info.ldn.node_count = 4;
    info.ldn.node_count_max = 8;
    info.ldn.advertise_data_size = 100;
    info.common.mac_address.data[0] = 0xAA;
    info.common.channel = 6;

    EncodeResult result = encode_network_info(buffer, sizeof(buffer),
                                              PacketId::Connected, info, out_size);

    ASSERT_EQ(result, EncodeResult::Success);
    ASSERT_EQ(out_size, sizeof(LdnHeader) + sizeof(NetworkInfo));

    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    ASSERT_EQ(header->type, static_cast<uint8_t>(PacketId::Connected));
}

TEST(roundtrip_network_info) {
    uint8_t buffer[2048];
    size_t encoded_size = 0;

    NetworkInfo original{};
    original.ldn.node_count = 3;
    original.ldn.node_count_max = 8;
    original.ldn.advertise_data_size = 50;
    original.common.mac_address.data[0] = 0xBB;
    original.common.mac_address.data[5] = 0xCC;
    original.common.channel = 11;

    // Encode
    EncodeResult enc_result = encode_network_info(buffer, sizeof(buffer),
                                                  PacketId::ScanReply, original, encoded_size);
    ASSERT_EQ(enc_result, EncodeResult::Success);

    // Decode
    LdnHeader header;
    NetworkInfo decoded;
    DecodeResult dec_result = decode_network_info(buffer, encoded_size, header, decoded);

    ASSERT_EQ(dec_result, DecodeResult::Success);
    ASSERT_EQ(decoded.ldn.node_count, original.ldn.node_count);
    ASSERT_EQ(decoded.ldn.node_count_max, original.ldn.node_count_max);
    ASSERT_EQ(decoded.ldn.advertise_data_size, original.ldn.advertise_data_size);
    ASSERT_EQ(decoded.common.mac_address.data[0], original.common.mac_address.data[0]);
    ASSERT_EQ(decoded.common.mac_address.data[5], original.common.mac_address.data[5]);
    ASSERT_EQ(decoded.common.channel, original.common.channel);
}

TEST(encode_scan_reply_end_packet) {
    uint8_t buffer[64];
    size_t out_size = 0;

    EncodeResult result = encode_scan_reply_end(buffer, sizeof(buffer), out_size);

    ASSERT_EQ(result, EncodeResult::Success);
    ASSERT_EQ(out_size, sizeof(LdnHeader));

    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    ASSERT_EQ(header->type, static_cast<uint8_t>(PacketId::ScanReplyEnd));
    ASSERT_EQ(header->data_size, 0);
}

TEST(encode_reject_reply_packet) {
    uint8_t buffer[64];
    size_t out_size = 0;

    EncodeResult result = encode_reject_reply(buffer, sizeof(buffer), out_size);

    ASSERT_EQ(result, EncodeResult::Success);
    ASSERT_EQ(out_size, sizeof(LdnHeader));

    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    ASSERT_EQ(header->type, static_cast<uint8_t>(PacketId::RejectReply));
    ASSERT_EQ(header->data_size, 0);
}

TEST(encode_raw_packet) {
    uint8_t buffer[128];
    size_t out_size = 0;

    uint8_t raw_data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    EncodeResult result = encode_raw(buffer, sizeof(buffer), PacketId::SetAdvertiseData,
                                     raw_data, sizeof(raw_data), out_size);

    ASSERT_EQ(result, EncodeResult::Success);
    ASSERT_EQ(out_size, sizeof(LdnHeader) + sizeof(raw_data));

    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    ASSERT_EQ(header->data_size, static_cast<int32_t>(sizeof(raw_data)));

    // Verify raw data was copied
    ASSERT_TRUE(std::memcmp(buffer + sizeof(LdnHeader), raw_data, sizeof(raw_data)) == 0);
}

TEST(roundtrip_raw) {
    uint8_t buffer[128];
    size_t encoded_size = 0;

    uint8_t original_data[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};

    // Encode
    EncodeResult enc_result = encode_raw(buffer, sizeof(buffer), PacketId::SetAdvertiseData,
                                         original_data, sizeof(original_data), encoded_size);
    ASSERT_EQ(enc_result, EncodeResult::Success);

    // Decode
    LdnHeader header;
    const uint8_t* decoded_data;
    size_t decoded_size;
    DecodeResult dec_result = decode_raw(buffer, encoded_size, header, decoded_data, decoded_size);

    ASSERT_EQ(dec_result, DecodeResult::Success);
    ASSERT_EQ(decoded_size, sizeof(original_data));
    ASSERT_TRUE(std::memcmp(decoded_data, original_data, decoded_size) == 0);
}

TEST(encode_header_function) {
    uint8_t buffer[32];

    size_t written = encode_header(buffer, PacketId::Ping, 100);

    ASSERT_EQ(written, sizeof(LdnHeader));

    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    ASSERT_EQ(header->magic, PROTOCOL_MAGIC);
    ASSERT_EQ(header->type, static_cast<uint8_t>(PacketId::Ping));
    ASSERT_EQ(header->version, PROTOCOL_VERSION);
    ASSERT_EQ(header->data_size, 100);
}

TEST(get_packet_size_functions) {
    ASSERT_EQ(get_packet_size(0), sizeof(LdnHeader));
    ASSERT_EQ(get_packet_size(100), sizeof(LdnHeader) + 100);
    ASSERT_EQ(get_packet_size<PingMessage>(), sizeof(LdnHeader) + sizeof(PingMessage));
    ASSERT_EQ(get_packet_size<NetworkInfo>(), sizeof(LdnHeader) + sizeof(NetworkInfo));
}

TEST(has_header_function) {
    ASSERT_FALSE(has_header(0));
    ASSERT_FALSE(has_header(5));
    ASSERT_FALSE(has_header(sizeof(LdnHeader) - 1));
    ASSERT_TRUE(has_header(sizeof(LdnHeader)));
    ASSERT_TRUE(has_header(sizeof(LdnHeader) + 100));
}

TEST(get_packet_type_function) {
    uint8_t buffer[32];
    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    header->magic = PROTOCOL_MAGIC;
    header->type = static_cast<uint8_t>(PacketId::ProxyData);
    header->version = PROTOCOL_VERSION;
    header->data_size = 0;

    PacketId type = get_packet_type(buffer);
    ASSERT_EQ(type, PacketId::ProxyData);
}

TEST(get_payload_size_function) {
    uint8_t buffer[32];
    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    header->magic = PROTOCOL_MAGIC;
    header->type = static_cast<uint8_t>(PacketId::Ping);
    header->version = PROTOCOL_VERSION;
    header->data_size = 42;

    int32_t size = get_payload_size(buffer);
    ASSERT_EQ(size, 42);
}

TEST(get_payload_ptr_function) {
    uint8_t buffer[64];
    std::memset(buffer, 0, sizeof(buffer));
    buffer[sizeof(LdnHeader)] = 0xDE;
    buffer[sizeof(LdnHeader) + 1] = 0xAD;

    const uint8_t* payload = get_payload_ptr(buffer);

    ASSERT_TRUE(payload == buffer + sizeof(LdnHeader));
    ASSERT_EQ(payload[0], 0xDE);
    ASSERT_EQ(payload[1], 0xAD);
}

TEST(decode_raw_empty_payload) {
    uint8_t buffer[32];

    // Build packet with no payload
    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    header->magic = PROTOCOL_MAGIC;
    header->type = static_cast<uint8_t>(PacketId::ScanReplyEnd);
    header->version = PROTOCOL_VERSION;
    header->data_size = 0;

    LdnHeader decoded_header;
    const uint8_t* data;
    size_t data_size;
    DecodeResult result = decode_raw(buffer, sizeof(LdnHeader), decoded_header, data, data_size);

    ASSERT_EQ(result, DecodeResult::Success);
    ASSERT_EQ(data_size, 0u);
    ASSERT_TRUE(data == nullptr);
}

TEST(encode_with_data_template) {
    uint8_t buffer[256];
    size_t out_size = 0;

    ProxyInfo info{};
    info.source_ipv4 = 0x0A000001;
    info.dest_ipv4 = 0x0A000002;

    uint8_t extra[] = {0x11, 0x22, 0x33};

    EncodeResult result = encode_with_data(buffer, sizeof(buffer), PacketId::ProxyConnect,
                                           info, extra, sizeof(extra), out_size);

    ASSERT_EQ(result, EncodeResult::Success);
    ASSERT_EQ(out_size, sizeof(LdnHeader) + sizeof(ProxyInfo) + sizeof(extra));

    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    ASSERT_EQ(header->data_size, static_cast<int32_t>(sizeof(ProxyInfo) + sizeof(extra)));
}

TEST(decode_with_data_template) {
    uint8_t buffer[256];
    size_t encoded_size = 0;

    ProxyInfo original_info{};
    original_info.source_ipv4 = 0xC0A80001;
    original_info.dest_ipv4 = 0xC0A80002;

    uint8_t original_extra[] = {0xAA, 0xBB, 0xCC, 0xDD};

    // Encode
    EncodeResult enc_result = encode_with_data(buffer, sizeof(buffer), PacketId::ProxyConnect,
                                               original_info, original_extra, sizeof(original_extra), encoded_size);
    ASSERT_EQ(enc_result, EncodeResult::Success);

    // Decode
    LdnHeader header;
    ProxyInfo decoded_info;
    const uint8_t* decoded_extra;
    size_t decoded_extra_size;
    DecodeResult dec_result = decode_with_data(buffer, encoded_size, header, decoded_info,
                                                decoded_extra, decoded_extra_size);

    ASSERT_EQ(dec_result, DecodeResult::Success);
    ASSERT_EQ(decoded_info.source_ipv4, original_info.source_ipv4);
    ASSERT_EQ(decoded_info.dest_ipv4, original_info.dest_ipv4);
    ASSERT_EQ(decoded_extra_size, sizeof(original_extra));
    ASSERT_TRUE(std::memcmp(decoded_extra, original_extra, decoded_extra_size) == 0);
}

TEST(all_packet_id_to_string) {
    // Test all PacketId values have valid strings
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::Initialize), "Initialize") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::Passphrase), "Passphrase") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::CreateAccessPoint), "CreateAccessPoint") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::CreateAccessPointPrivate), "CreateAccessPointPrivate") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::ExternalProxy), "ExternalProxy") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::ExternalProxyToken), "ExternalProxyToken") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::ExternalProxyState), "ExternalProxyState") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::SyncNetwork), "SyncNetwork") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::Reject), "Reject") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::RejectReply), "RejectReply") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::Scan), "Scan") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::ScanReply), "ScanReply") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::ScanReplyEnd), "ScanReplyEnd") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::Connect), "Connect") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::ConnectPrivate), "ConnectPrivate") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::Connected), "Connected") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::Disconnect), "Disconnect") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::ProxyConfig), "ProxyConfig") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::ProxyConnect), "ProxyConnect") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::ProxyConnectReply), "ProxyConnectReply") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::ProxyData), "ProxyData") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::ProxyDisconnect), "ProxyDisconnect") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::SetAcceptPolicy), "SetAcceptPolicy") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::SetAdvertiseData), "SetAdvertiseData") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::Ping), "Ping") == 0);
    ASSERT_TRUE(std::strcmp(packet_id_to_string(PacketId::NetworkError), "NetworkError") == 0);
}

TEST(all_decode_result_to_string) {
    ASSERT_TRUE(std::strcmp(decode_result_to_string(DecodeResult::Success), "Success") == 0);
    ASSERT_TRUE(std::strcmp(decode_result_to_string(DecodeResult::BufferTooSmall), "BufferTooSmall") == 0);
    ASSERT_TRUE(std::strcmp(decode_result_to_string(DecodeResult::InvalidMagic), "InvalidMagic") == 0);
    ASSERT_TRUE(std::strcmp(decode_result_to_string(DecodeResult::InvalidVersion), "InvalidVersion") == 0);
    ASSERT_TRUE(std::strcmp(decode_result_to_string(DecodeResult::PacketTooLarge), "PacketTooLarge") == 0);
    ASSERT_TRUE(std::strcmp(decode_result_to_string(DecodeResult::IncompletePacket), "IncompletePacket") == 0);
}

TEST(all_encode_result_to_string) {
    ASSERT_TRUE(std::strcmp(encode_result_to_string(EncodeResult::Success), "Success") == 0);
    ASSERT_TRUE(std::strcmp(encode_result_to_string(EncodeResult::BufferTooSmall), "BufferTooSmall") == 0);
    ASSERT_TRUE(std::strcmp(encode_result_to_string(EncodeResult::InvalidPacketId), "InvalidPacketId") == 0);
}

// ============================================================================
// Additional Structure Size Tests
// ============================================================================

TEST(additional_structure_sizes) {
    // Verify all structure sizes match RyuLDN protocol
    ASSERT_EQ(sizeof(SetAcceptPolicyRequest), 0x1);  // 1 byte per Ryujinx protocol
    ASSERT_EQ(sizeof(ExternalProxyConfig), 0x26);
    ASSERT_EQ(sizeof(ExternalProxyToken), 0x28);
    ASSERT_EQ(sizeof(ExternalProxyConnectionState), 0x08);
    ASSERT_EQ(sizeof(ProxyConnectRequest), 0x10);
    ASSERT_EQ(sizeof(ProxyConnectResponse), 0x10);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST(encode_buffer_too_small_for_payload) {
    uint8_t buffer[16];  // Too small for NetworkInfo
    size_t out_size = 0;

    NetworkInfo info{};
    EncodeResult result = encode_network_info(buffer, sizeof(buffer), PacketId::Connected, info, out_size);

    ASSERT_EQ(result, EncodeResult::BufferTooSmall);
    ASSERT_EQ(out_size, 0u);
}

TEST(decode_incomplete_packet_data) {
    uint8_t buffer[32];

    // Build header saying we have 100 bytes of payload
    LdnHeader* header = reinterpret_cast<LdnHeader*>(buffer);
    header->magic = PROTOCOL_MAGIC;
    header->type = static_cast<uint8_t>(PacketId::ProxyData);
    header->version = PROTOCOL_VERSION;
    header->data_size = 100;  // Says 100 bytes

    // But buffer only has 32 bytes total
    LdnHeader decoded_header;
    const uint8_t* data;
    size_t data_size;
    DecodeResult result = decode_raw(buffer, sizeof(buffer), decoded_header, data, data_size);

    ASSERT_EQ(result, DecodeResult::IncompletePacket);
}

TEST(decode_with_data_no_extra) {
    uint8_t buffer[128];
    size_t encoded_size = 0;

    ProxyInfo info{};
    info.source_ipv4 = 0x01020304;

    // Encode with NO extra data
    EncodeResult enc_result = encode_with_data(buffer, sizeof(buffer), PacketId::ProxyConnect,
                                               info, nullptr, 0, encoded_size);
    ASSERT_EQ(enc_result, EncodeResult::Success);

    // Decode
    LdnHeader header;
    ProxyInfo decoded_info;
    const uint8_t* extra_data;
    size_t extra_size;
    DecodeResult dec_result = decode_with_data(buffer, encoded_size, header, decoded_info,
                                                extra_data, extra_size);

    ASSERT_EQ(dec_result, DecodeResult::Success);
    ASSERT_TRUE(extra_data == nullptr);
    ASSERT_EQ(extra_size, 0u);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== ryu_ldn_nx Protocol Unit Tests ===\n\n");
    run_all_tests();
    return g_tests_failed > 0 ? 1 : 0;
}
