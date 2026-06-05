/**
 * @file config_ipc_service.hpp
 * @brief Standalone IPC service for configuration (ryu:cfg)
 *
 * This service is registered independently and can be accessed by the
 * Tesla overlay without requiring a game to use ldn:u.
 *
 * Service name: ryu:cfg
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include "config.hpp"

namespace ryu_ldn::ipc {

/**
 * @brief IPC command IDs for ryu:cfg service
 */
enum class ConfigCmd : u32 {
    // Configuration commands (0-17)
    GetVersion          = 0,
    GetConnectionStatus = 1,
    GetPassphrase       = 2,
    SetPassphrase       = 3,
    GetServerAddress    = 4,
    SetServerAddress    = 5,
    GetLdnEnabled      = 6,
    SetLdnEnabled      = 7,
    GetDebugEnabled    = 8,
    SetDebugEnabled    = 9,
    GetDebugLevel       = 10,
    SetDebugLevel       = 11,
    SaveConfig          = 12,
    ReloadConfig        = 13,
    IsServiceActive     = 14,
    GetUsePassphrase    = 16,
    SetUsePassphrase    = 17,

    // Runtime LDN state commands (18-23)
    IsGameActive        = 18,  ///< Returns 1 if a game is using LDN
    GetLdnState         = 19,  ///< Returns CommState (0-6)
    GetSessionInfo      = 20,  ///< Returns SessionInfoIpc struct (8 bytes)
    GetLastRtt          = 21,  ///< Returns last RTT in milliseconds
    ForceReconnect      = 22,  ///< Requests reconnection
    GetActiveProcessId  = 23,  ///< Returns PID of active game (debug)

    // P2P Proxy control (24-25)
    GetDisableP2p       = 24,  ///< Returns 1 if P2P proxy is disabled
    SetDisableP2p       = 25,  ///< Sets P2P proxy disabled state
};

/**
 * @brief Configuration result enum
 */
enum class ConfigResult : u32 {
    Success = 0,
    FileNotFound = 1,
    ParseError = 2,
    IoError = 3,
    InvalidValue = 4,
};

/**
 * @brief Server address structure for IPC
 */
struct ServerAddressIpc {
    char host[64];
    u16 port;
    u16 padding;
};
static_assert(sizeof(ServerAddressIpc) == 68);

/**
 * @brief Session information structure for IPC
 *
 * Contains runtime information about the current LDN session.
 */
struct SessionInfoIpc {
    u8 node_count;      ///< Current number of nodes in session
    u8 max_nodes;       ///< Maximum nodes allowed in session
    u8 local_node_id;   ///< This node's ID in the session
    u8 is_host;         ///< 1 if this node is the host, 0 otherwise
    u8 reserved[4];     ///< Reserved for future use
};
static_assert(sizeof(SessionInfoIpc) == 8);

/**
 * @brief Global configuration instance
 *
 * Shared between MITM service and config IPC service.
 */
extern config::Config g_config;
extern ams::os::SdkMutex g_config_mutex;

/**
 * @brief Initialize global configuration
 */
void InitializeConfig();

/**
 * @brief Configuration IPC service implementation
 */
class ConfigService {
public:
    ConfigService() = default;

    /** @brief Get the sysmodule version string */
    /// @gdb{tag="CONFIG:IPC", msg="GetVersion"}
    ams::Result GetVersion(ams::sf::Out<std::array<char, 32>> out);

    /** @brief Get connection status (0 = service running) */
    /// @gdb{tag="CONFIG:IPC", msg="GetConnectionStatus"}
    ams::Result GetConnectionStatus(ams::sf::Out<u32> out);

    /** @brief Get the current LDN room passphrase */
    /// @gdb{tag="CONFIG:IPC", msg="GetPassphrase"}
    ams::Result GetPassphrase(ams::sf::Out<std::array<char, 64>> out);
    /** @brief Set the LDN room passphrase
     *  @param passphrase Null-terminated passphrase string (max 63 chars)
     */
    /// @gdb{tag="CONFIG:IPC", msg="SetPassphrase"}
    ams::Result SetPassphrase(std::array<char, 64> passphrase);

    /** @brief Get the configured server address (host + port) */
    /// @gdb{tag="CONFIG:IPC", msg="GetServerAddress"}
    ams::Result GetServerAddress(ams::sf::Out<ServerAddressIpc> out);
    /** @brief Set the server address (host + port)
     *  @param address New server address to use
     */
    /// @gdb{tag="CONFIG:IPC", msg="SetServerAddress"}
    ams::Result SetServerAddress(const ServerAddressIpc &address);

    /** @brief Get whether LDN emulation is enabled */
    /// @gdb{tag="CONFIG:IPC", msg="GetLdnEnabled"}
    ams::Result GetLdnEnabled(ams::sf::Out<u32> out);
    /** @brief Enable or disable LDN emulation
     *  @param enabled 1 to enable, 0 to disable
     */
    /// @gdb{tag="CONFIG:IPC", msg="SetLdnEnabled"}
    ams::Result SetLdnEnabled(u32 enabled);

    /** @brief Get whether debug logging is enabled */
    /// @gdb{tag="CONFIG:IPC", msg="GetDebugEnabled"}
    ams::Result GetDebugEnabled(ams::sf::Out<u32> out);
    /** @brief Enable or disable debug logging
     *  @param enabled 1 to enable, 0 to disable
     */
    /// @gdb{tag="CONFIG:IPC", msg="SetDebugEnabled"}
    ams::Result SetDebugEnabled(u32 enabled);
    /** @brief Get the debug log level (0=errors, 1=warnings, 2=info, 3=verbose) */
    /// @gdb{tag="CONFIG:IPC", msg="GetDebugLevel"}
    ams::Result GetDebugLevel(ams::sf::Out<u32> out);
    /** @brief Set the debug log level
     *  @param level 0=errors, 1=warnings, 2=info, 3=verbose
     */
    /// @gdb{tag="CONFIG:IPC", msg="SetDebugLevel"}
    ams::Result SetDebugLevel(u32 level);

    /** @brief Save current configuration to disk
     *  @param out Result of the save operation
     */
    /// @gdb{tag="CONFIG:IPC", msg="SaveConfig"}
    ams::Result SaveConfig(ams::sf::Out<ConfigResult> out);
    /** @brief Reload configuration from disk
     *  @param out Result of the reload operation
     */
    /// @gdb{tag="CONFIG:IPC", msg="ReloadConfig"}
    ams::Result ReloadConfig(ams::sf::Out<ConfigResult> out);

    /** @brief Check if the IPC service is active */
    /// @gdb{tag="CONFIG:IPC", msg="IsServiceActive"}
    ams::Result IsServiceActive(ams::sf::Out<u32> out);

    /** @brief Get whether passphrase filtering is enabled
     *  When use_passphrase is true, LDN rooms are filtered by the
     *  configured passphrase. When false, the sysmodule connects to
     *  public rooms regardless of the passphrase value.
     *  @param out 1 if passphrase filtering is enabled, 0 if public
     */
    /// @gdb{tag="CONFIG:IPC", msg="GetUsePassphrase"}
    ams::Result GetUsePassphrase(ams::sf::Out<u32> out);
    /** @brief Enable or disable passphrase filtering
     *  @param enabled 1 to enable passphrase filtering, 0 for public rooms
     */
    /// @gdb{tag="CONFIG:IPC", msg="SetUsePassphrase"}
    ams::Result SetUsePassphrase(u32 enabled);

    // =========================================================================
    // Runtime LDN State (read from SharedState singleton)
    // =========================================================================

    /** @brief Check if a game is actively using LDN
     *  @param out 1 if a game is using LDN, 0 otherwise
     */
    /// @gdb{tag="CONFIG:IPC", msg="IsGameActive"}
    ams::Result IsGameActive(ams::sf::Out<u32> out);

    /** @brief Get current LDN CommState
     *  @param out CommState value (0=None, 1=Initialized, 2=AccessPoint, 3=AccessPointCreated, 4=Station, 5=StationConnected)
     */
    /// @gdb{tag="CONFIG:IPC", msg="GetLdnState"}
    ams::Result GetLdnState(ams::sf::Out<u32> out);

    /** @brief Get session information
     *  @param out SessionInfoIpc struct (node count, max, local ID, is_host)
     */
    /// @gdb{tag="CONFIG:IPC", msg="GetSessionInfo"}
    ams::Result GetSessionInfo(ams::sf::Out<SessionInfoIpc> out);

    /** @brief Get last measured RTT
     *  @param out RTT in milliseconds
     */
    /// @gdb{tag="CONFIG:IPC", msg="GetLastRtt"}
    ams::Result GetLastRtt(ams::sf::Out<u32> out);

    /** @brief Request the MITM service to reconnect */
    /// @gdb{tag="CONFIG:IPC", msg="ForceReconnect"}
    ams::Result ForceReconnect();

    /** @brief Get the process ID of the active game
     *  @param out Process ID (for debugging)
     */
    /// @gdb{tag="CONFIG:IPC", msg="GetActiveProcessId"}
    ams::Result GetActiveProcessId(ams::sf::Out<u64> out);

    // =========================================================================
    // P2P Proxy Control
    // =========================================================================

    /** @brief Get whether P2P proxy is disabled
     *  @param out 1 if P2P proxy is disabled, 0 if enabled
     */
    /// @gdb{tag="CONFIG:IPC", msg="GetDisableP2p"}
    ams::Result GetDisableP2p(ams::sf::Out<u32> out);

    /** @brief Set P2P proxy disabled state
     *  @param disabled 1 to disable P2P proxy, 0 to enable
     */
    /// @gdb{tag="CONFIG:IPC", msg="SetDisableP2p"}
    ams::Result SetDisableP2p(u32 disabled);
};

} // namespace ryu_ldn::ipc

// =============================================================================
// SF Service Interface Definition (must be outside namespace)
// =============================================================================

/**
 * @brief SF interface macro for ryu:cfg service
 *
 * Defines all IPC commands (0-25) for the configuration service.
 * Command IDs match ConfigCmd in this header.
 * Get/Set pairs: Get even, Set odd (GetUsePassphrase=16, SetUsePassphrase=17).
 * Command 15 is reserved (gap from parity swap).
 * Uses 9-arg form of AMS_SF_METHOD_INFO with explicit version range.
 */
#define AMS_RYU_CFG_SERVICE_INTERFACE(C, H)                                                                                        \
    /* Configuration (0-14) */                                                                                                      \
    AMS_SF_METHOD_INFO(C, H, 0,  ams::Result, GetVersion,         (ams::sf::Out<std::array<char, 32>> out),             (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 1,  ams::Result, GetConnectionStatus,(ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 2,  ams::Result, GetPassphrase,      (ams::sf::Out<std::array<char, 64>> out),            (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 3,  ams::Result, SetPassphrase,      (std::array<char, 64> passphrase),                   (passphrase), ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 4,  ams::Result, GetServerAddress,   (ams::sf::Out<ryu_ldn::ipc::ServerAddressIpc> out),  (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 5,  ams::Result, SetServerAddress,   (const ryu_ldn::ipc::ServerAddressIpc &address),     (address),    ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 6,  ams::Result, GetLdnEnabled,      (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 7,  ams::Result, SetLdnEnabled,      (u32 enabled),                                        (enabled),    ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 8,  ams::Result, GetDebugEnabled,    (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 9,  ams::Result, SetDebugEnabled,    (u32 enabled),                                        (enabled),    ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 10, ams::Result, GetDebugLevel,      (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 11, ams::Result, SetDebugLevel,      (u32 level),                                          (level),      ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 12, ams::Result, SaveConfig,         (ams::sf::Out<ryu_ldn::ipc::ConfigResult> out),      (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 13, ams::Result, ReloadConfig,       (ams::sf::Out<ryu_ldn::ipc::ConfigResult> out),      (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 14, ams::Result, IsServiceActive,    (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    /* Passphrase filtering (16-17, Get even / Set odd) */                                                                          \
    AMS_SF_METHOD_INFO(C, H, 16, ams::Result, GetUsePassphrase,   (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 17, ams::Result, SetUsePassphrase,   (u32 enabled),                                        (enabled),    ams::hos::Version_Min, ams::hos::Version_Max) \
    /* Runtime LDN state (18-23) */                                                                                                 \
    AMS_SF_METHOD_INFO(C, H, 18, ams::Result, IsGameActive,       (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 19, ams::Result, GetLdnState,        (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 20, ams::Result, GetSessionInfo,     (ams::sf::Out<ryu_ldn::ipc::SessionInfoIpc> out),    (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 21, ams::Result, GetLastRtt,         (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 22, ams::Result, ForceReconnect,     (),                                                   (),           ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 23, ams::Result, GetActiveProcessId, (ams::sf::Out<u64> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    /* P2P Proxy control (24-25) */                                                                                                 \
    AMS_SF_METHOD_INFO(C, H, 24, ams::Result, GetDisableP2p,      (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 25, ams::Result, SetDisableP2p,      (u32 disabled),                                       (disabled),   ams::hos::Version_Min, ams::hos::Version_Max)
