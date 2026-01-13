/**
 * @file ldn_config_service.hpp
 * @brief Configuration IPC service for Tesla overlay
 *
 * Provides a custom IPC interface exposed through the ldn:u MITM service
 * for the Tesla overlay to query status and change configuration.
 *
 * ## IPC Interface
 * The service is obtained via command 65000 on ldn:u and exposes:
 * - 65001: GetVersion - Returns sysmodule version string
 * - 65002: GetConnectionStatus - Returns server connection state
 * - 65003: GetLdnState - Returns current LDN state
 * - 65004: GetSessionInfo - Returns current session information
 * - 65005: GetServerAddress - Returns configured server address
 * - 65006: SetServerAddress - Changes server address
 * - 65007: GetDebugEnabled - Returns debug logging state
 * - 65008: SetDebugEnabled - Enables/disables debug logging
 * - 65009: ForceReconnect - Forces server reconnection
 * - 65010: GetLastRtt - Returns last ping RTT
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>

namespace ams::mitm::ldn {

// Forward declarations
class LdnICommunication;

/**
 * @brief Connection status for overlay
 */
enum class ConnectionStatus : u32 {
    Disconnected = 0,   ///< Not connected to server
    Connecting = 1,     ///< Connection in progress
    Connected = 2,      ///< Connected, handshake pending
    Ready = 3,          ///< Fully connected and ready
    Error = 4,          ///< Connection error
};

/**
 * @brief Session information structure for overlay
 */
struct SessionInfo {
    u8 node_count;           ///< Number of players in session
    u8 node_count_max;       ///< Maximum players allowed
    u8 local_node_id;        ///< Our node ID in the session
    u8 is_host;              ///< 1 if we are the host, 0 if client
    u32 session_duration_ms; ///< Time since session started (ms)
    char game_name[64];      ///< Game name (if available)
};
static_assert(sizeof(SessionInfo) == 72);

/**
 * @brief Server address structure
 */
struct ServerAddress {
    char host[64];
    u16 port;
    u16 padding;
};
static_assert(sizeof(ServerAddress) == 68);

/**
 * @brief Configuration service for Tesla overlay
 *
 * This service is created per-session when the overlay requests it
 * via command 65000 on the ldn:u MITM service.
 */
class LdnConfigService {
public:
    /**
     * @brief Constructor
     *
     * @param communication Pointer to parent LdnICommunication service
     */
    explicit LdnConfigService(LdnICommunication* communication);

    /**
     * @brief Get sysmodule version string
     *
     * @param out Output buffer for version string (32 bytes)
     * @return Always succeeds
     */
    Result GetVersion(sf::Out<std::array<char, 32>> out);

    /**
     * @brief Get current connection status
     *
     * @param out Output connection status
     * @return Always succeeds
     */
    Result GetConnectionStatus(sf::Out<ConnectionStatus> out);

    /**
     * @brief Get current LDN state
     *
     * @param out Output LDN state (as u32)
     * @return Always succeeds
     */
    Result GetLdnState(sf::Out<u32> out);

    /**
     * @brief Get session information
     *
     * @param out Output session info structure
     * @return Always succeeds
     */
    Result GetSessionInfo(sf::Out<SessionInfo> out);

    /**
     * @brief Get server address
     *
     * @param out Output server address structure
     * @return Always succeeds
     */
    Result GetServerAddress(sf::Out<ServerAddress> out);

    /**
     * @brief Set server address
     *
     * @param address New server address
     * @return ResultSuccess on success
     */
    Result SetServerAddress(ServerAddress address);

    /**
     * @brief Get debug logging state
     *
     * @param out Output: 1 if enabled, 0 if disabled
     * @return Always succeeds
     */
    Result GetDebugEnabled(sf::Out<u32> out);

    /**
     * @brief Set debug logging state
     *
     * @param enabled 1 to enable, 0 to disable
     * @return Always succeeds
     */
    Result SetDebugEnabled(u32 enabled);

    /**
     * @brief Force server reconnection
     *
     * @return ResultSuccess on success
     */
    Result ForceReconnect();

    /**
     * @brief Get last RTT in milliseconds
     *
     * @param out Output RTT in ms
     * @return Always succeeds
     */
    Result GetLastRtt(sf::Out<u32> out);

private:
    LdnICommunication* m_communication;
};

} // namespace ams::mitm::ldn
