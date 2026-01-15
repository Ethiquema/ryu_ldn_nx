/**
 * @file tcp_client_tests.cpp
 * @brief Unit tests for ryu_ldn::network::TcpClient
 *
 * These tests verify the TcpClient class functionality including:
 * - Construction and state management
 * - Connection handling (without actual server)
 * - Send operations (encoding verification)
 * - Result code mapping
 * - Move semantics
 *
 * ## Test Strategy
 *
 * Since TcpClient depends on actual network connectivity for full testing,
 * these unit tests focus on:
 * 1. State management (connected/disconnected)
 * 2. Error handling (proper error codes returned)
 * 3. API contracts (correct behavior when disconnected)
 * 4. Result code string conversion
 *
 * Integration tests with a mock server would be needed for complete coverage.
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "network/tcp_client.hpp"

using namespace ryu_ldn::network;
using namespace ryu_ldn::protocol;

// =============================================================================
// Test Framework
// =============================================================================

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct TestRegister_##name { \
        TestRegister_##name() { \
            printf("  [TEST] " #name "... "); \
            fflush(stdout); \
            g_tests_run++; \
            try { \
                test_##name(); \
                printf("PASS\n"); \
                g_tests_passed++; \
            } catch (const char* msg) { \
                printf("FAIL: %s\n", msg); \
                g_tests_failed++; \
            } \
        } \
    } g_test_register_##name; \
    static void test_##name()

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            throw "Assertion failed: " #cond; \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            throw "Assertion failed: " #a " != " #b; \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            throw "Assertion failed: " #a " == " #b; \
        } \
    } while (0)

#define ASSERT_TRUE(cond) ASSERT(cond)
#define ASSERT_FALSE(cond) ASSERT(!(cond))

// =============================================================================
// Tests: Construction and State
// =============================================================================

/**
 * @test Default constructed client is disconnected
 */
TEST(default_disconnected) {
    socket_init();

    TcpClient client;
    ASSERT_FALSE(client.is_connected());
}

/**
 * @test Multiple clients can be created
 */
TEST(multiple_clients) {
    socket_init();

    TcpClient client1;
    TcpClient client2;
    TcpClient client3;

    ASSERT_FALSE(client1.is_connected());
    ASSERT_FALSE(client2.is_connected());
    ASSERT_FALSE(client3.is_connected());
}

/**
 * @test Client destructor doesn't crash when disconnected
 */
TEST(destructor_safe_disconnected) {
    socket_init();

    {
        TcpClient client;
        // Destructor called here
    }
    // If we get here, test passed
    ASSERT_TRUE(true);
}

/**
 * @test Move constructor transfers state
 */
TEST(move_constructor) {
    socket_init();

    TcpClient client1;
    TcpClient client2(std::move(client1));

    ASSERT_FALSE(client1.is_connected());
    ASSERT_FALSE(client2.is_connected());
}

/**
 * @test Move assignment transfers state
 */
TEST(move_assignment) {
    socket_init();

    TcpClient client1;
    TcpClient client2;

    client2 = std::move(client1);

    ASSERT_FALSE(client1.is_connected());
    ASSERT_FALSE(client2.is_connected());
}

// =============================================================================
// Tests: Connection (Without Server)
// =============================================================================

/**
 * @test Connect to non-existent server fails
 */
TEST(connect_fails_no_server) {
    socket_init();

    TcpClient client;

    // Try to connect to a port that's almost certainly not listening
    ClientResult result = client.connect("127.0.0.1", 59999, 500);

    ASSERT_NE(result, ClientResult::Success);
    ASSERT_FALSE(client.is_connected());
}

/**
 * @test Connect with invalid host returns error
 */
TEST(connect_invalid_host) {
    socket_init();

    TcpClient client;
    ClientResult result = client.connect("invalid.host.that.does.not.exist.local", 30456, 500);

    ASSERT_NE(result, ClientResult::Success);
    ASSERT_FALSE(client.is_connected());
}

/**
 * @test Connect with null host returns error
 */
TEST(connect_null_host) {
    socket_init();

    TcpClient client;
    ClientResult result = client.connect(nullptr, 30456, 500);

    ASSERT_NE(result, ClientResult::Success);
    ASSERT_FALSE(client.is_connected());
}

/**
 * @test Connect with empty host returns error
 */
TEST(connect_empty_host) {
    socket_init();

    TcpClient client;
    ClientResult result = client.connect("", 30456, 500);

    ASSERT_NE(result, ClientResult::Success);
    ASSERT_FALSE(client.is_connected());
}

/**
 * @test Disconnect on non-connected client is safe
 */
TEST(disconnect_not_connected) {
    socket_init();

    TcpClient client;
    ASSERT_FALSE(client.is_connected());

    client.disconnect();  // Should not crash
    client.disconnect();  // Multiple disconnects should be safe

    ASSERT_FALSE(client.is_connected());
}

// =============================================================================
// Tests: Send Operations (Disconnected)
// =============================================================================

/**
 * @test send_initialize fails when disconnected
 */
TEST(send_initialize_not_connected) {
    socket_init();

    TcpClient client;

    InitializeMessage msg{};
    ClientResult result = client.send_initialize(msg);

    ASSERT_EQ(result, ClientResult::NotConnected);
}

/**
 * @test send_passphrase fails when disconnected
 */
TEST(send_passphrase_not_connected) {
    socket_init();

    TcpClient client;

    PassphraseMessage msg{};
    ClientResult result = client.send_passphrase(msg);

    ASSERT_EQ(result, ClientResult::NotConnected);
}

/**
 * @test send_ping fails when disconnected
 */
TEST(send_ping_not_connected) {
    socket_init();

    TcpClient client;

    PingMessage msg{};
    msg.requester = 1;
    msg.id = 42;
    ClientResult result = client.send_ping(msg);

    ASSERT_EQ(result, ClientResult::NotConnected);
}

/**
 * @test send_disconnect fails when disconnected
 */
TEST(send_disconnect_not_connected) {
    socket_init();

    TcpClient client;

    DisconnectMessage msg{};
    msg.disconnect_ip = 0xC0A80101;  // 192.168.1.1
    ClientResult result = client.send_disconnect(msg);

    ASSERT_EQ(result, ClientResult::NotConnected);
}

/**
 * @test send_packet fails when disconnected
 */
TEST(send_packet_not_connected) {
    socket_init();

    TcpClient client;

    uint8_t data[] = {0x01, 0x02, 0x03};
    ClientResult result = client.send_packet(PacketId::Ping, data, sizeof(data));

    ASSERT_EQ(result, ClientResult::NotConnected);
}

/**
 * @test send_create_access_point fails when disconnected
 */
TEST(send_create_access_point_not_connected) {
    socket_init();

    TcpClient client;

    CreateAccessPointRequest request{};
    ClientResult result = client.send_create_access_point(request);

    ASSERT_EQ(result, ClientResult::NotConnected);
}

/**
 * @test send_connect fails when disconnected
 */
TEST(send_connect_not_connected) {
    socket_init();

    TcpClient client;

    ConnectRequest request{};
    ClientResult result = client.send_connect(request);

    ASSERT_EQ(result, ClientResult::NotConnected);
}

/**
 * @test send_scan fails when disconnected
 */
TEST(send_scan_not_connected) {
    socket_init();

    TcpClient client;

    ScanFilterFull filter{};
    ClientResult result = client.send_scan(filter);

    ASSERT_EQ(result, ClientResult::NotConnected);
}

/**
 * @test send_proxy_data fails when disconnected
 */
TEST(send_proxy_data_not_connected) {
    socket_init();

    TcpClient client;

    ProxyDataHeader header{};
    header.info.source_ipv4 = 0xC0A80101;   // 192.168.1.1
    header.info.source_port = 12345;
    header.info.dest_ipv4 = 0xC0A80102;     // 192.168.1.2
    header.info.dest_port = 54321;
    header.info.protocol = ProtocolType::Udp;
    header.data_length = 3;
    uint8_t data[] = {0xAA, 0xBB, 0xCC};

    ClientResult result = client.send_proxy_data(header, data, sizeof(data));

    ASSERT_EQ(result, ClientResult::NotConnected);
}

// =============================================================================
// Tests: Receive Operations (Disconnected)
// =============================================================================

/**
 * @test receive_packet fails when disconnected
 */
TEST(receive_packet_not_connected) {
    socket_init();

    TcpClient client;

    PacketId type;
    uint8_t payload[256];
    size_t payload_size;

    ClientResult result = client.receive_packet(type, payload, sizeof(payload), payload_size, 0);

    ASSERT_EQ(result, ClientResult::NotConnected);
}

/**
 * @test has_packet_available returns false when disconnected
 */
TEST(has_packet_not_connected) {
    socket_init();

    TcpClient client;

    ASSERT_FALSE(client.has_packet_available());
}

// =============================================================================
// Tests: Configuration (Disconnected)
// =============================================================================

/**
 * @test set_nodelay fails when disconnected
 */
TEST(set_nodelay_not_connected) {
    socket_init();

    TcpClient client;

    ClientResult result = client.set_nodelay(true);

    ASSERT_EQ(result, ClientResult::NotConnected);
}

// =============================================================================
// Tests: Result String Conversion
// =============================================================================

/**
 * @test client_result_to_string covers all values
 */
TEST(result_to_string_success) {
    ASSERT(strcmp(client_result_to_string(ClientResult::Success), "Success") == 0);
}

TEST(result_to_string_not_connected) {
    ASSERT(strcmp(client_result_to_string(ClientResult::NotConnected), "NotConnected") == 0);
}

TEST(result_to_string_already_connected) {
    ASSERT(strcmp(client_result_to_string(ClientResult::AlreadyConnected), "AlreadyConnected") == 0);
}

TEST(result_to_string_connection_failed) {
    ASSERT(strcmp(client_result_to_string(ClientResult::ConnectionFailed), "ConnectionFailed") == 0);
}

TEST(result_to_string_connection_lost) {
    ASSERT(strcmp(client_result_to_string(ClientResult::ConnectionLost), "ConnectionLost") == 0);
}

TEST(result_to_string_timeout) {
    ASSERT(strcmp(client_result_to_string(ClientResult::Timeout), "Timeout") == 0);
}

TEST(result_to_string_invalid_packet) {
    ASSERT(strcmp(client_result_to_string(ClientResult::InvalidPacket), "InvalidPacket") == 0);
}

TEST(result_to_string_protocol_error) {
    ASSERT(strcmp(client_result_to_string(ClientResult::ProtocolError), "ProtocolError") == 0);
}

TEST(result_to_string_buffer_too_small) {
    ASSERT(strcmp(client_result_to_string(ClientResult::BufferTooSmall), "BufferTooSmall") == 0);
}

TEST(result_to_string_encoding_error) {
    ASSERT(strcmp(client_result_to_string(ClientResult::EncodingError), "EncodingError") == 0);
}

TEST(result_to_string_not_initialized) {
    ASSERT(strcmp(client_result_to_string(ClientResult::NotInitialized), "NotInitialized") == 0);
}

TEST(result_to_string_internal_error) {
    ASSERT(strcmp(client_result_to_string(ClientResult::InternalError), "InternalError") == 0);
}

TEST(result_to_string_unknown) {
    ClientResult unknown = static_cast<ClientResult>(9999);
    ASSERT(strcmp(client_result_to_string(unknown), "Unknown") == 0);
}

// =============================================================================
// Tests: Operations Before Socket Init
// =============================================================================

/**
 * @test Connect fails if socket subsystem not initialized
 */
TEST(connect_before_socket_init) {
    socket_exit();  // Ensure not initialized
    ASSERT_FALSE(socket_is_initialized());

    TcpClient client;
    ClientResult result = client.connect("127.0.0.1", 30456, 500);

    ASSERT_EQ(result, ClientResult::NotInitialized);

    // Restore for other tests
    socket_init();
}

// =============================================================================
// Tests: State After Failed Connect
// =============================================================================

/**
 * @test Client state is consistent after failed connect
 */
TEST(state_after_failed_connect) {
    socket_init();

    TcpClient client;

    // First failed connect
    ClientResult result1 = client.connect("127.0.0.1", 59998, 100);
    ASSERT_NE(result1, ClientResult::Success);
    ASSERT_FALSE(client.is_connected());

    // Second failed connect should also work
    ClientResult result2 = client.connect("127.0.0.1", 59997, 100);
    ASSERT_NE(result2, ClientResult::Success);
    ASSERT_FALSE(client.is_connected());

    // Disconnect should be safe
    client.disconnect();
    ASSERT_FALSE(client.is_connected());
}

/**
 * @test Can retry send after disconnect
 */
TEST(retry_after_failed_connect) {
    socket_init();

    TcpClient client;

    // Failed connect
    client.connect("127.0.0.1", 59996, 100);

    // Send should fail
    PingMessage msg{};
    ClientResult result = client.send_ping(msg);
    ASSERT_EQ(result, ClientResult::NotConnected);

    // Another failed connect
    client.connect("127.0.0.1", 59995, 100);

    // Send should still fail
    result = client.send_ping(msg);
    ASSERT_EQ(result, ClientResult::NotConnected);
}

// =============================================================================
// Tests: Edge Cases
// =============================================================================

/**
 * @test Multiple destructor calls (via multiple moves)
 */
TEST(multiple_moves_safe) {
    socket_init();

    TcpClient client1;
    TcpClient client2(std::move(client1));
    TcpClient client3(std::move(client2));
    TcpClient client4;
    client4 = std::move(client3);

    ASSERT_FALSE(client1.is_connected());
    ASSERT_FALSE(client2.is_connected());
    ASSERT_FALSE(client3.is_connected());
    ASSERT_FALSE(client4.is_connected());
}

/**
 * @test Sending empty proxy data
 */
TEST(send_empty_proxy_data) {
    socket_init();

    TcpClient client;

    ProxyDataHeader header{};
    ClientResult result = client.send_proxy_data(header, nullptr, 0);

    ASSERT_EQ(result, ClientResult::NotConnected);
}

// =============================================================================
// Tests: Private Room Operations (Story 7.7)
// =============================================================================

/**
 * @test send_create_access_point_private fails when disconnected
 */
TEST(send_create_access_point_private_not_connected) {
    socket_init();

    TcpClient client;

    CreateAccessPointPrivateRequest request{};
    request.security_config.security_mode = 2;
    request.security_parameter.data[0] = 0xAA;
    request.network_config.node_count_max = 8;

    ClientResult result = client.send_create_access_point_private(request);

    ASSERT_EQ(result, ClientResult::NotConnected);
}

/**
 * @test send_create_access_point_private with advertise data fails when disconnected
 */
TEST(send_create_access_point_private_with_advertise_not_connected) {
    socket_init();

    TcpClient client;

    CreateAccessPointPrivateRequest request{};
    request.security_config.security_mode = 2;

    uint8_t advertise_data[] = {0x01, 0x02, 0x03, 0x04};
    ClientResult result = client.send_create_access_point_private(request, advertise_data, sizeof(advertise_data));

    ASSERT_EQ(result, ClientResult::NotConnected);
}

/**
 * @test send_connect_private fails when disconnected
 */
TEST(send_connect_private_not_connected) {
    socket_init();

    TcpClient client;

    ConnectPrivateRequest request{};
    request.security_config.security_mode = 2;
    request.security_parameter.data[0] = 0xBB;
    request.local_communication_version = 1;
    request.network_config.node_count_max = 4;

    ClientResult result = client.send_connect_private(request);

    ASSERT_EQ(result, ClientResult::NotConnected);
}

/**
 * @test CreateAccessPointPrivateRequest size is correct
 */
TEST(create_access_point_private_request_size) {
    ASSERT_EQ(sizeof(CreateAccessPointPrivateRequest), 0x13Cu);  // 316 bytes
}

/**
 * @test ConnectPrivateRequest size is correct
 */
TEST(connect_private_request_size) {
    ASSERT_EQ(sizeof(ConnectPrivateRequest), 0xBCu);  // 188 bytes
}

/**
 * @test SecurityParameter size is correct
 */
TEST(security_parameter_size) {
    ASSERT_EQ(sizeof(SecurityParameter), 0x20u);  // 32 bytes
}

/**
 * @test AddressList size is correct
 */
TEST(address_list_size) {
    ASSERT_EQ(sizeof(AddressList), 0x60u);  // 96 bytes
}

/**
 * @test AddressEntry size is correct
 */
TEST(address_entry_size) {
    ASSERT_EQ(sizeof(AddressEntry), 0x0Cu);  // 12 bytes
}

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("\n");
    printf("========================================\n");
    printf("  TcpClient Tests - ryu_ldn_nx\n");
    printf("========================================\n\n");

    // Tests run automatically via static initialization

    printf("\n========================================\n");
    printf("  Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0) {
        printf(" (%d FAILED)", g_tests_failed);
    }
    printf("\n========================================\n\n");

    // Cleanup
    socket_exit();

    return g_tests_failed > 0 ? 1 : 0;
}
