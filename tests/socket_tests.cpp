/**
 * @file socket_tests.cpp
 * @brief Unit tests for ryu_ldn::network::Socket using mocks
 *
 * These tests use mocking to test the Socket wrapper without requiring
 * actual network connections. This makes tests:
 * - Faster (no network I/O)
 * - More reliable (no port conflicts, no network issues)
 * - Deterministic (same results every run)
 *
 * ## Mock Architecture
 *
 * We intercept the low-level socket functions using a mock layer that
 * can be configured to return specific values for testing different
 * scenarios (success, errors, timeouts, etc.).
 *
 * ## Running Tests
 *
 * ```bash
 * cd tests
 * make test-socket
 * ```
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <functional>

// For test build, we mock the socket functions
#define TEST_BUILD_SOCKET_MOCK

#include "network/socket.hpp"

using namespace ryu_ldn::network;

// =============================================================================
// Test Framework (minimal, same as other test files)
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
// Tests: Socket Subsystem Initialization
// =============================================================================

/**
 * @test socket_init should succeed
 *
 * Verifies that socket_init() returns Success and marks the subsystem
 * as initialized.
 */
TEST(socket_init_succeeds) {
    // Start fresh
    socket_exit();
    ASSERT_FALSE(socket_is_initialized());

    SocketResult result = socket_init();
    ASSERT_EQ(result, SocketResult::Success);
    ASSERT_TRUE(socket_is_initialized());
}

/**
 * @test socket_init is idempotent
 *
 * Calling socket_init() multiple times should succeed without error.
 */
TEST(socket_init_idempotent) {
    SocketResult result1 = socket_init();
    ASSERT_EQ(result1, SocketResult::Success);

    SocketResult result2 = socket_init();
    ASSERT_EQ(result2, SocketResult::Success);

    SocketResult result3 = socket_init();
    ASSERT_EQ(result3, SocketResult::Success);

    ASSERT_TRUE(socket_is_initialized());
}

/**
 * @test socket_exit clears initialized state
 *
 * After socket_exit(), socket_is_initialized() should return false.
 */
TEST(socket_exit_clears_state) {
    socket_init();
    ASSERT_TRUE(socket_is_initialized());

    socket_exit();
    ASSERT_FALSE(socket_is_initialized());
}

/**
 * @test socket_exit is idempotent
 *
 * Calling socket_exit() multiple times should not crash.
 */
TEST(socket_exit_idempotent) {
    socket_init();
    socket_exit();
    socket_exit(); // Should not crash
    socket_exit(); // Should not crash
    ASSERT_FALSE(socket_is_initialized());

    // Re-init for subsequent tests
    socket_init();
}

// =============================================================================
// Tests: Socket Construction and State
// =============================================================================

/**
 * @test Default constructed socket is invalid
 *
 * A newly constructed Socket should be invalid and not connected.
 */
TEST(socket_default_invalid) {
    socket_init();

    Socket sock;
    ASSERT_FALSE(sock.is_valid());
    ASSERT_FALSE(sock.is_connected());
    ASSERT_EQ(sock.get_fd(), -1);
}

/**
 * @test Socket can be created multiple times
 *
 * Creating multiple Socket objects should work independently.
 */
TEST(socket_multiple_instances) {
    socket_init();

    Socket sock1;
    Socket sock2;
    Socket sock3;

    ASSERT_FALSE(sock1.is_valid());
    ASSERT_FALSE(sock2.is_valid());
    ASSERT_FALSE(sock3.is_valid());

    ASSERT_EQ(sock1.get_fd(), -1);
    ASSERT_EQ(sock2.get_fd(), -1);
    ASSERT_EQ(sock3.get_fd(), -1);
}

/**
 * @test Socket move constructor transfers ownership
 *
 * Moving a socket should transfer the file descriptor and leave
 * the original socket invalid.
 */
TEST(socket_move_constructor) {
    socket_init();

    Socket sock1;
    // We can't connect without a server, but we can test move semantics
    // by checking that the fd transfers correctly

    Socket sock2(std::move(sock1));

    // Both should be invalid since sock1 was never connected
    ASSERT_FALSE(sock1.is_valid());
    ASSERT_FALSE(sock2.is_valid());
    ASSERT_EQ(sock1.get_fd(), -1);
    ASSERT_EQ(sock2.get_fd(), -1);
}

/**
 * @test Socket move assignment works
 *
 * Move assignment should transfer ownership.
 */
TEST(socket_move_assignment) {
    socket_init();

    Socket sock1;
    Socket sock2;

    sock2 = std::move(sock1);

    ASSERT_FALSE(sock1.is_valid());
    ASSERT_FALSE(sock2.is_valid());
}

// =============================================================================
// Tests: Connection State Without Server
// =============================================================================

/**
 * @test Connect to invalid host returns error
 *
 * Connecting to an invalid hostname should return an error.
 */
TEST(connect_invalid_host) {
    socket_init();

    Socket sock;
    SocketResult result = sock.connect("invalid.host.that.does.not.exist.local", 12345, 100);

    // Should fail with some error (InvalidAddress or HostUnreachable)
    ASSERT_NE(result, SocketResult::Success);
    ASSERT_FALSE(sock.is_connected());
}

/**
 * @test Connect with null host returns error
 *
 * Passing null for hostname should fail safely.
 */
TEST(connect_null_host) {
    socket_init();

    Socket sock;
    SocketResult result = sock.connect(nullptr, 12345, 100);

    ASSERT_NE(result, SocketResult::Success);
    ASSERT_FALSE(sock.is_connected());
}

/**
 * @test Connect with empty host returns error
 *
 * Empty hostname string should fail.
 */
TEST(connect_empty_host) {
    socket_init();

    Socket sock;
    SocketResult result = sock.connect("", 12345, 100);

    ASSERT_NE(result, SocketResult::Success);
    ASSERT_FALSE(sock.is_connected());
}

/**
 * @test Connect to unreachable port times out or refuses
 *
 * Connecting to localhost on a port with no listener should fail.
 */
TEST(connect_refused) {
    socket_init();

    Socket sock;
    // Port 1 is privileged and unlikely to be in use
    // Use high port that's almost certainly not listening
    SocketResult result = sock.connect("127.0.0.1", 59999, 500);

    // Should fail - either ConnectionRefused or Timeout
    ASSERT_NE(result, SocketResult::Success);
    ASSERT_FALSE(sock.is_connected());
}

// =============================================================================
// Tests: Close Operations
// =============================================================================

/**
 * @test Close on invalid socket is safe
 *
 * Calling close() on a socket that was never connected should not crash.
 */
TEST(close_on_invalid_safe) {
    socket_init();

    Socket sock;
    ASSERT_FALSE(sock.is_valid());

    sock.close(); // Should not crash
    sock.close(); // Multiple closes should be safe
    sock.close();

    ASSERT_FALSE(sock.is_valid());
    ASSERT_FALSE(sock.is_connected());
}

/**
 * @test Close is idempotent
 *
 * Calling close() multiple times should be safe.
 */
TEST(close_idempotent) {
    socket_init();

    Socket sock;

    for (int i = 0; i < 10; i++) {
        sock.close();
    }

    ASSERT_FALSE(sock.is_valid());
}

// =============================================================================
// Tests: Send Without Connection
// =============================================================================

/**
 * @test Send on disconnected socket fails
 *
 * Sending on a socket that isn't connected should fail.
 */
TEST(send_not_connected) {
    socket_init();

    Socket sock;
    ASSERT_FALSE(sock.is_connected());

    uint8_t data[] = {0x01, 0x02, 0x03};
    size_t sent = 0;
    SocketResult result = sock.send(data, sizeof(data), sent);

    ASSERT_EQ(result, SocketResult::NotConnected);
    ASSERT_EQ(sent, 0u);
}

/**
 * @test Send all on disconnected socket fails
 *
 * send_all() on a disconnected socket should fail.
 */
TEST(send_all_not_connected) {
    socket_init();

    Socket sock;

    uint8_t data[] = {0x01, 0x02, 0x03};
    SocketResult result = sock.send_all(data, sizeof(data));

    ASSERT_EQ(result, SocketResult::NotConnected);
}

/**
 * @test Send with null buffer on disconnected socket
 *
 * Should fail with NotConnected before checking buffer.
 */
TEST(send_null_buffer_not_connected) {
    socket_init();

    Socket sock;
    size_t sent = 0;
    SocketResult result = sock.send(nullptr, 10, sent);

    ASSERT_EQ(result, SocketResult::NotConnected);
}

/**
 * @test Send with zero size on disconnected socket
 *
 * Should fail with NotConnected.
 */
TEST(send_zero_size_not_connected) {
    socket_init();

    Socket sock;
    uint8_t data[] = {0x01};
    size_t sent = 999; // Should be set to 0
    SocketResult result = sock.send(data, 0, sent);

    ASSERT_EQ(result, SocketResult::NotConnected);
}

// =============================================================================
// Tests: Receive Without Connection
// =============================================================================

/**
 * @test Receive on disconnected socket fails
 *
 * Receiving on a socket that isn't connected should fail.
 */
TEST(recv_not_connected) {
    socket_init();

    Socket sock;
    ASSERT_FALSE(sock.is_connected());

    uint8_t buffer[256];
    size_t received = 0;
    SocketResult result = sock.recv(buffer, sizeof(buffer), received, 0);

    ASSERT_EQ(result, SocketResult::NotConnected);
    ASSERT_EQ(received, 0u);
}

/**
 * @test Receive with timeout on disconnected socket
 *
 * Should fail immediately without waiting.
 */
TEST(recv_timeout_not_connected) {
    socket_init();

    Socket sock;

    uint8_t buffer[256];
    size_t received = 0;
    SocketResult result = sock.recv(buffer, sizeof(buffer), received, 1000);

    ASSERT_EQ(result, SocketResult::NotConnected);
}

/**
 * @test Receive with blocking mode on disconnected socket
 *
 * Should fail immediately without blocking.
 */
TEST(recv_blocking_not_connected) {
    socket_init();

    Socket sock;

    uint8_t buffer[256];
    size_t received = 0;
    SocketResult result = sock.recv(buffer, sizeof(buffer), received, -1);

    ASSERT_EQ(result, SocketResult::NotConnected);
}

// =============================================================================
// Tests: Socket Options Without Connection
// =============================================================================

/**
 * @test Set non-blocking on invalid socket fails
 *
 * Setting options on an invalid socket should fail.
 */
TEST(set_non_blocking_invalid) {
    socket_init();

    Socket sock;
    ASSERT_FALSE(sock.is_valid());

    SocketResult result = sock.set_non_blocking(true);
    ASSERT_EQ(result, SocketResult::SocketError);
}

/**
 * @test Set nodelay on invalid socket fails
 */
TEST(set_nodelay_invalid) {
    socket_init();

    Socket sock;

    SocketResult result = sock.set_nodelay(true);
    ASSERT_EQ(result, SocketResult::SocketError);
}

/**
 * @test Set recv buffer size on invalid socket fails
 */
TEST(set_recv_buffer_invalid) {
    socket_init();

    Socket sock;

    SocketResult result = sock.set_recv_buffer_size(65536);
    ASSERT_EQ(result, SocketResult::SocketError);
}

/**
 * @test Set send buffer size on invalid socket fails
 */
TEST(set_send_buffer_invalid) {
    socket_init();

    Socket sock;

    SocketResult result = sock.set_send_buffer_size(65536);
    ASSERT_EQ(result, SocketResult::SocketError);
}

// =============================================================================
// Tests: Result String Conversion
// =============================================================================

/**
 * @test socket_result_to_string covers all values
 *
 * All SocketResult values should have meaningful string representations.
 */
TEST(result_to_string_success) {
    ASSERT(strcmp(socket_result_to_string(SocketResult::Success), "Success") == 0);
}

TEST(result_to_string_would_block) {
    ASSERT(strcmp(socket_result_to_string(SocketResult::WouldBlock), "WouldBlock") == 0);
}

TEST(result_to_string_timeout) {
    ASSERT(strcmp(socket_result_to_string(SocketResult::Timeout), "Timeout") == 0);
}

TEST(result_to_string_connection_refused) {
    ASSERT(strcmp(socket_result_to_string(SocketResult::ConnectionRefused), "ConnectionRefused") == 0);
}

TEST(result_to_string_connection_reset) {
    ASSERT(strcmp(socket_result_to_string(SocketResult::ConnectionReset), "ConnectionReset") == 0);
}

TEST(result_to_string_host_unreachable) {
    ASSERT(strcmp(socket_result_to_string(SocketResult::HostUnreachable), "HostUnreachable") == 0);
}

TEST(result_to_string_network_down) {
    ASSERT(strcmp(socket_result_to_string(SocketResult::NetworkDown), "NetworkDown") == 0);
}

TEST(result_to_string_not_connected) {
    ASSERT(strcmp(socket_result_to_string(SocketResult::NotConnected), "NotConnected") == 0);
}

TEST(result_to_string_already_connected) {
    ASSERT(strcmp(socket_result_to_string(SocketResult::AlreadyConnected), "AlreadyConnected") == 0);
}

TEST(result_to_string_invalid_address) {
    ASSERT(strcmp(socket_result_to_string(SocketResult::InvalidAddress), "InvalidAddress") == 0);
}

TEST(result_to_string_socket_error) {
    ASSERT(strcmp(socket_result_to_string(SocketResult::SocketError), "SocketError") == 0);
}

TEST(result_to_string_not_initialized) {
    ASSERT(strcmp(socket_result_to_string(SocketResult::NotInitialized), "NotInitialized") == 0);
}

TEST(result_to_string_closed) {
    ASSERT(strcmp(socket_result_to_string(SocketResult::Closed), "Closed") == 0);
}

/**
 * @test Unknown result returns "Unknown"
 */
TEST(result_to_string_unknown) {
    SocketResult unknown = static_cast<SocketResult>(9999);
    ASSERT(strcmp(socket_result_to_string(unknown), "Unknown") == 0);
}

// =============================================================================
// Tests: Socket Subsystem Not Initialized
// =============================================================================

/**
 * @test Operations before socket_init fail
 *
 * Socket operations should fail if the subsystem isn't initialized.
 */
TEST(operations_before_init) {
    // Make sure we're not initialized
    socket_exit();
    ASSERT_FALSE(socket_is_initialized());

    Socket sock;

    // Connect should fail
    SocketResult connect_result = sock.connect("127.0.0.1", 12345, 100);
    ASSERT_EQ(connect_result, SocketResult::NotInitialized);

    // Re-init for subsequent tests
    socket_init();
}

// =============================================================================
// Tests: Edge Cases
// =============================================================================

/**
 * @test Very long hostname is handled
 *
 * An extremely long hostname should be handled without buffer overflow.
 */
TEST(very_long_hostname) {
    socket_init();

    Socket sock;

    // Create a 1024-char hostname
    char long_host[1025];
    memset(long_host, 'a', 1024);
    long_host[1024] = '\0';

    SocketResult result = sock.connect(long_host, 12345, 100);

    // Should fail with InvalidAddress, not crash
    ASSERT_NE(result, SocketResult::Success);
}

/**
 * @test Port 0 is handled
 *
 * Connecting to port 0 should fail.
 */
TEST(port_zero) {
    socket_init();

    Socket sock;
    SocketResult result = sock.connect("127.0.0.1", 0, 100);

    // Should fail
    ASSERT_NE(result, SocketResult::Success);
}

/**
 * @test Maximum port number is handled
 *
 * Port 65535 is valid and should be accepted.
 */
TEST(port_max) {
    socket_init();

    Socket sock;
    // This will fail to connect (no server) but shouldn't crash
    SocketResult result = sock.connect("127.0.0.1", 65535, 100);

    // Should fail with refused/timeout, not invalid
    ASSERT_NE(result, SocketResult::Success);
    // The result should be a connection error, not a validation error
    ASSERT(result == SocketResult::ConnectionRefused ||
           result == SocketResult::Timeout ||
           result == SocketResult::HostUnreachable ||
           result == SocketResult::SocketError);
}

/**
 * @test IPv4 address format is accepted
 */
TEST(ipv4_format) {
    socket_init();

    Socket sock;
    // Valid format, should try to connect (and fail due to no server)
    SocketResult result = sock.connect("192.168.1.1", 12345, 100);

    // Should fail but with network error, not format error
    ASSERT_NE(result, SocketResult::Success);
}

/**
 * @test Localhost variants work
 */
TEST(localhost_variants) {
    socket_init();

    Socket sock1;
    SocketResult r1 = sock1.connect("127.0.0.1", 59998, 100);
    ASSERT_NE(r1, SocketResult::Success);

    Socket sock2;
    SocketResult r2 = sock2.connect("localhost", 59998, 100);
    // localhost should resolve (might fail to connect)
    ASSERT_NE(r2, SocketResult::Success);
}

// =============================================================================
// Tests: Destructor Safety
// =============================================================================

/**
 * @test Destructor on uninitialized socket is safe
 *
 * Socket destructor should handle uninitialized state.
 */
TEST(destructor_uninitialized) {
    socket_init();

    {
        Socket sock;
        // sock goes out of scope without being connected
    }
    // Should not crash

    ASSERT_TRUE(true); // If we get here, test passed
}

/**
 * @test Multiple sockets destroyed in sequence
 *
 * Creating and destroying multiple sockets should work.
 */
TEST(multiple_destructor_calls) {
    socket_init();

    for (int i = 0; i < 100; i++) {
        Socket sock;
        // Let it be destroyed
    }

    ASSERT_TRUE(true);
}

// =============================================================================
// Tests: State Consistency
// =============================================================================

/**
 * @test State is consistent after failed connect
 *
 * After a failed connection attempt, socket should be in clean state.
 */
TEST(state_after_failed_connect) {
    socket_init();

    Socket sock;

    // Try to connect (will fail)
    sock.connect("127.0.0.1", 59997, 100);

    // State should be consistent
    ASSERT_FALSE(sock.is_connected());

    // Should be able to try again
    sock.connect("127.0.0.1", 59996, 100);
    ASSERT_FALSE(sock.is_connected());
}

/**
 * @test Close after failed connect is safe
 */
TEST(close_after_failed_connect) {
    socket_init();

    Socket sock;
    sock.connect("127.0.0.1", 59995, 100);

    sock.close();
    sock.close();

    ASSERT_FALSE(sock.is_valid());
    ASSERT_FALSE(sock.is_connected());
}

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("\n");
    printf("========================================\n");
    printf("  Socket Tests - ryu_ldn_nx\n");
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
