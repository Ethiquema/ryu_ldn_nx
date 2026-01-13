/**
 * @file overlay_tests.cpp
 * @brief Unit tests for Tesla overlay logic
 *
 * Tests the overlay helper functions and IPC structures without
 * requiring the actual Switch environment. Uses mocks for libnx types.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

//=============================================================================
// Mock Switch/libnx types for testing
//=============================================================================

// Basic types
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t Result;

#define R_SUCCEEDED(res) ((res) == 0)
#define R_FAILED(res)    ((res) != 0)

// Mock Service structure
typedef struct {
    u32 handle;
} Service;

//=============================================================================
// Include overlay types (copy from ryu_ldn_ipc.h for testing)
//=============================================================================

typedef enum {
    RyuLdnStatus_Disconnected = 0,
    RyuLdnStatus_Connecting = 1,
    RyuLdnStatus_Connected = 2,
    RyuLdnStatus_Ready = 3,
    RyuLdnStatus_Error = 4,
} RyuLdnConnectionStatus;

typedef enum {
    RyuLdnState_None = 0,
    RyuLdnState_Initialized = 1,
    RyuLdnState_AccessPoint = 2,
    RyuLdnState_AccessPointCreated = 3,
    RyuLdnState_Station = 4,
    RyuLdnState_StationConnected = 5,
    RyuLdnState_Error = 6,
} RyuLdnState;

typedef struct {
    u8 node_count;
    u8 node_count_max;
    u8 local_node_id;
    u8 is_host;
    u32 session_duration_ms;
    char game_name[64];
} RyuLdnSessionInfo;

typedef struct {
    Service s;
} RyuLdnConfigService;

//=============================================================================
// Helper functions from main.cpp (extracted for testing)
//=============================================================================

const char* ConnectionStatusToString(RyuLdnConnectionStatus status) {
    switch (status) {
        case RyuLdnStatus_Disconnected: return "Disconnected";
        case RyuLdnStatus_Connecting:   return "Connecting...";
        case RyuLdnStatus_Connected:    return "Connected";
        case RyuLdnStatus_Ready:        return "Ready";
        case RyuLdnStatus_Error:        return "Error";
        default:                        return "Unknown";
    }
}

const char* LdnStateToString(RyuLdnState state) {
    switch (state) {
        case RyuLdnState_None:              return "None";
        case RyuLdnState_Initialized:       return "Initialized";
        case RyuLdnState_AccessPoint:       return "Access Point";
        case RyuLdnState_AccessPointCreated:return "Hosting";
        case RyuLdnState_Station:           return "Station";
        case RyuLdnState_StationConnected:  return "Connected";
        case RyuLdnState_Error:             return "Error";
        default:                            return "Unknown";
    }
}

// Format session info string
void FormatSessionInfo(const RyuLdnSessionInfo* info, char* buf, size_t bufSize) {
    if (info->node_count == 0) {
        snprintf(buf, bufSize, "Not in session");
    } else {
        snprintf(buf, bufSize, "%d/%d players (%s)",
                 info->node_count, info->node_count_max,
                 info->is_host ? "Host" : "Client");
    }
}

// Format server address string
void FormatServerAddress(const char* host, u16 port, char* buf, size_t bufSize) {
    snprintf(buf, bufSize, "%s:%u", host, port);
}

// Format latency string
void FormatLatency(u32 rtt_ms, char* buf, size_t bufSize) {
    if (rtt_ms == 0) {
        snprintf(buf, bufSize, "N/A");
    } else {
        snprintf(buf, bufSize, "%u ms", rtt_ms);
    }
}

//=============================================================================
// Test Framework
//=============================================================================

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    g_tests_run++; \
    printf("  [TEST] %s...", #name); \
    name(); \
    g_tests_passed++; \
    printf(" PASS\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAIL\n    Assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_STREQ(a, b) ASSERT(strcmp((a), (b)) == 0)

//=============================================================================
// Connection Status Tests
//=============================================================================

TEST(status_disconnected_to_string) {
    ASSERT_STREQ(ConnectionStatusToString(RyuLdnStatus_Disconnected), "Disconnected");
}

TEST(status_connecting_to_string) {
    ASSERT_STREQ(ConnectionStatusToString(RyuLdnStatus_Connecting), "Connecting...");
}

TEST(status_connected_to_string) {
    ASSERT_STREQ(ConnectionStatusToString(RyuLdnStatus_Connected), "Connected");
}

TEST(status_ready_to_string) {
    ASSERT_STREQ(ConnectionStatusToString(RyuLdnStatus_Ready), "Ready");
}

TEST(status_error_to_string) {
    ASSERT_STREQ(ConnectionStatusToString(RyuLdnStatus_Error), "Error");
}

TEST(status_unknown_to_string) {
    ASSERT_STREQ(ConnectionStatusToString((RyuLdnConnectionStatus)99), "Unknown");
}

//=============================================================================
// LDN State Tests
//=============================================================================

TEST(ldn_state_none_to_string) {
    ASSERT_STREQ(LdnStateToString(RyuLdnState_None), "None");
}

TEST(ldn_state_initialized_to_string) {
    ASSERT_STREQ(LdnStateToString(RyuLdnState_Initialized), "Initialized");
}

TEST(ldn_state_access_point_to_string) {
    ASSERT_STREQ(LdnStateToString(RyuLdnState_AccessPoint), "Access Point");
}

TEST(ldn_state_access_point_created_to_string) {
    ASSERT_STREQ(LdnStateToString(RyuLdnState_AccessPointCreated), "Hosting");
}

TEST(ldn_state_station_to_string) {
    ASSERT_STREQ(LdnStateToString(RyuLdnState_Station), "Station");
}

TEST(ldn_state_station_connected_to_string) {
    ASSERT_STREQ(LdnStateToString(RyuLdnState_StationConnected), "Connected");
}

TEST(ldn_state_error_to_string) {
    ASSERT_STREQ(LdnStateToString(RyuLdnState_Error), "Error");
}

TEST(ldn_state_unknown_to_string) {
    ASSERT_STREQ(LdnStateToString((RyuLdnState)99), "Unknown");
}

//=============================================================================
// Session Info Formatting Tests
//=============================================================================

TEST(session_info_not_in_session) {
    RyuLdnSessionInfo info = {};
    info.node_count = 0;
    char buf[64];
    FormatSessionInfo(&info, buf, sizeof(buf));
    ASSERT_STREQ(buf, "Not in session");
}

TEST(session_info_host_single_player) {
    RyuLdnSessionInfo info = {};
    info.node_count = 1;
    info.node_count_max = 8;
    info.is_host = 1;
    char buf[64];
    FormatSessionInfo(&info, buf, sizeof(buf));
    ASSERT_STREQ(buf, "1/8 players (Host)");
}

TEST(session_info_client_multi_player) {
    RyuLdnSessionInfo info = {};
    info.node_count = 4;
    info.node_count_max = 8;
    info.is_host = 0;
    char buf[64];
    FormatSessionInfo(&info, buf, sizeof(buf));
    ASSERT_STREQ(buf, "4/8 players (Client)");
}

TEST(session_info_full_session) {
    RyuLdnSessionInfo info = {};
    info.node_count = 8;
    info.node_count_max = 8;
    info.is_host = 1;
    char buf[64];
    FormatSessionInfo(&info, buf, sizeof(buf));
    ASSERT_STREQ(buf, "8/8 players (Host)");
}

TEST(session_info_two_players) {
    RyuLdnSessionInfo info = {};
    info.node_count = 2;
    info.node_count_max = 2;
    info.is_host = 0;
    char buf[64];
    FormatSessionInfo(&info, buf, sizeof(buf));
    ASSERT_STREQ(buf, "2/2 players (Client)");
}

//=============================================================================
// Server Address Formatting Tests
//=============================================================================

TEST(server_address_localhost) {
    char buf[96];
    FormatServerAddress("localhost", 39990, buf, sizeof(buf));
    ASSERT_STREQ(buf, "localhost:39990");
}

TEST(server_address_ip) {
    char buf[96];
    FormatServerAddress("192.168.1.100", 39990, buf, sizeof(buf));
    ASSERT_STREQ(buf, "192.168.1.100:39990");
}

TEST(server_address_hostname) {
    char buf[96];
    FormatServerAddress("ryu.example.com", 39990, buf, sizeof(buf));
    ASSERT_STREQ(buf, "ryu.example.com:39990");
}

TEST(server_address_custom_port) {
    char buf[96];
    FormatServerAddress("server.net", 12345, buf, sizeof(buf));
    ASSERT_STREQ(buf, "server.net:12345");
}

//=============================================================================
// Latency Formatting Tests
//=============================================================================

TEST(latency_zero_shows_na) {
    char buf[32];
    FormatLatency(0, buf, sizeof(buf));
    ASSERT_STREQ(buf, "N/A");
}

TEST(latency_small_value) {
    char buf[32];
    FormatLatency(5, buf, sizeof(buf));
    ASSERT_STREQ(buf, "5 ms");
}

TEST(latency_medium_value) {
    char buf[32];
    FormatLatency(42, buf, sizeof(buf));
    ASSERT_STREQ(buf, "42 ms");
}

TEST(latency_high_value) {
    char buf[32];
    FormatLatency(250, buf, sizeof(buf));
    ASSERT_STREQ(buf, "250 ms");
}

TEST(latency_very_high_value) {
    char buf[32];
    FormatLatency(1500, buf, sizeof(buf));
    ASSERT_STREQ(buf, "1500 ms");
}

//=============================================================================
// IPC Structure Tests
//=============================================================================

TEST(session_info_structure_size) {
    // Verify structure packing
    ASSERT(sizeof(RyuLdnSessionInfo) >= 72); // 4 bytes + 4 bytes padding + 64 game_name
}

TEST(session_info_zero_initialized) {
    RyuLdnSessionInfo info = {};
    ASSERT_EQ(info.node_count, 0);
    ASSERT_EQ(info.node_count_max, 0);
    ASSERT_EQ(info.local_node_id, 0);
    ASSERT_EQ(info.is_host, 0);
    ASSERT_EQ(info.session_duration_ms, 0u);
    ASSERT_EQ(info.game_name[0], '\0');
}

TEST(connection_status_enum_values) {
    ASSERT_EQ(RyuLdnStatus_Disconnected, 0);
    ASSERT_EQ(RyuLdnStatus_Connecting, 1);
    ASSERT_EQ(RyuLdnStatus_Connected, 2);
    ASSERT_EQ(RyuLdnStatus_Ready, 3);
    ASSERT_EQ(RyuLdnStatus_Error, 4);
}

TEST(ldn_state_enum_values) {
    ASSERT_EQ(RyuLdnState_None, 0);
    ASSERT_EQ(RyuLdnState_Initialized, 1);
    ASSERT_EQ(RyuLdnState_AccessPoint, 2);
    ASSERT_EQ(RyuLdnState_AccessPointCreated, 3);
    ASSERT_EQ(RyuLdnState_Station, 4);
    ASSERT_EQ(RyuLdnState_StationConnected, 5);
    ASSERT_EQ(RyuLdnState_Error, 6);
}

//=============================================================================
// Edge Cases
//=============================================================================

TEST(format_session_buffer_small) {
    RyuLdnSessionInfo info = {};
    info.node_count = 8;
    info.node_count_max = 8;
    info.is_host = 1;
    char buf[10]; // Too small, but should not crash
    FormatSessionInfo(&info, buf, sizeof(buf));
    // Just verify it doesn't crash and produces something
    ASSERT(strlen(buf) < 10);
}

TEST(format_latency_max_u32) {
    char buf[32];
    FormatLatency(4294967295u, buf, sizeof(buf));
    ASSERT(strlen(buf) > 0);
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("\n========================================\n");
    printf("  Overlay Tests - ryu_ldn_nx\n");
    printf("========================================\n\n");

    printf("--- Connection Status Tests ---\n");
    RUN_TEST(status_disconnected_to_string);
    RUN_TEST(status_connecting_to_string);
    RUN_TEST(status_connected_to_string);
    RUN_TEST(status_ready_to_string);
    RUN_TEST(status_error_to_string);
    RUN_TEST(status_unknown_to_string);

    printf("\n--- LDN State Tests ---\n");
    RUN_TEST(ldn_state_none_to_string);
    RUN_TEST(ldn_state_initialized_to_string);
    RUN_TEST(ldn_state_access_point_to_string);
    RUN_TEST(ldn_state_access_point_created_to_string);
    RUN_TEST(ldn_state_station_to_string);
    RUN_TEST(ldn_state_station_connected_to_string);
    RUN_TEST(ldn_state_error_to_string);
    RUN_TEST(ldn_state_unknown_to_string);

    printf("\n--- Session Info Formatting Tests ---\n");
    RUN_TEST(session_info_not_in_session);
    RUN_TEST(session_info_host_single_player);
    RUN_TEST(session_info_client_multi_player);
    RUN_TEST(session_info_full_session);
    RUN_TEST(session_info_two_players);

    printf("\n--- Server Address Formatting Tests ---\n");
    RUN_TEST(server_address_localhost);
    RUN_TEST(server_address_ip);
    RUN_TEST(server_address_hostname);
    RUN_TEST(server_address_custom_port);

    printf("\n--- Latency Formatting Tests ---\n");
    RUN_TEST(latency_zero_shows_na);
    RUN_TEST(latency_small_value);
    RUN_TEST(latency_medium_value);
    RUN_TEST(latency_high_value);
    RUN_TEST(latency_very_high_value);

    printf("\n--- IPC Structure Tests ---\n");
    RUN_TEST(session_info_structure_size);
    RUN_TEST(session_info_zero_initialized);
    RUN_TEST(connection_status_enum_values);
    RUN_TEST(ldn_state_enum_values);

    printf("\n--- Edge Cases ---\n");
    RUN_TEST(format_session_buffer_small);
    RUN_TEST(format_latency_max_u32);

    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n", g_tests_passed, g_tests_run);
    printf("========================================\n\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
