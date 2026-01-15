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
    // Configuration commands (0-22)
    GetVersion          = 0,
    GetConnectionStatus = 1,
    GetPassphrase       = 2,
    SetPassphrase       = 3,
    GetServerAddress    = 4,
    SetServerAddress    = 5,
    GetLdnEnabled       = 6,
    SetLdnEnabled       = 7,
    GetUseTls           = 8,
    SetUseTls           = 9,
    GetDebugEnabled     = 10,
    SetDebugEnabled     = 11,
    GetDebugLevel       = 12,
    SetDebugLevel       = 13,
    GetLogToFile        = 14,
    SetLogToFile        = 15,
    SaveConfig          = 16,
    ReloadConfig        = 17,
    GetConnectTimeout   = 18,
    SetConnectTimeout   = 19,
    GetPingInterval     = 20,
    SetPingInterval     = 21,
    IsServiceActive     = 22,

    // Runtime LDN state commands (23-28)
    IsGameActive        = 23,  ///< Returns 1 if a game is using LDN
    GetLdnState         = 24,  ///< Returns CommState (0-6)
    GetSessionInfo      = 25,  ///< Returns SessionInfo struct (8 bytes)
    GetLastRtt          = 26,  ///< Returns last RTT in milliseconds
    ForceReconnect      = 27,  ///< Requests reconnection
    GetActiveProcessId  = 28,  ///< Returns PID of active game (debug)

    // P2P Proxy control (29-30)
    GetDisableP2p       = 29,  ///< Returns 1 if P2P proxy is disabled
    SetDisableP2p       = 30,  ///< Sets P2P proxy disabled state (like Ryujinx MultiplayerDisableP2p)
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

    // Version
    ams::Result GetVersion(ams::sf::Out<std::array<char, 32>> out);

    // Connection status (0 = service running)
    ams::Result GetConnectionStatus(ams::sf::Out<u32> out);

    // Passphrase
    ams::Result GetPassphrase(ams::sf::Out<std::array<char, 64>> out);
    ams::Result SetPassphrase(std::array<char, 64> passphrase);

    // Server address
    ams::Result GetServerAddress(ams::sf::Out<ServerAddressIpc> out);
    ams::Result SetServerAddress(ServerAddressIpc address);

    // LDN enabled
    ams::Result GetLdnEnabled(ams::sf::Out<u32> out);
    ams::Result SetLdnEnabled(u32 enabled);

    // TLS
    ams::Result GetUseTls(ams::sf::Out<u32> out);
    ams::Result SetUseTls(u32 enabled);

    // Debug
    ams::Result GetDebugEnabled(ams::sf::Out<u32> out);
    ams::Result SetDebugEnabled(u32 enabled);
    ams::Result GetDebugLevel(ams::sf::Out<u32> out);
    ams::Result SetDebugLevel(u32 level);
    ams::Result GetLogToFile(ams::sf::Out<u32> out);
    ams::Result SetLogToFile(u32 enabled);

    // Timeouts
    ams::Result GetConnectTimeout(ams::sf::Out<u32> out);
    ams::Result SetConnectTimeout(u32 timeout_ms);
    ams::Result GetPingInterval(ams::sf::Out<u32> out);
    ams::Result SetPingInterval(u32 interval_ms);

    // Config file operations
    ams::Result SaveConfig(ams::sf::Out<ConfigResult> out);
    ams::Result ReloadConfig(ams::sf::Out<ConfigResult> out);

    // Service check
    ams::Result IsServiceActive(ams::sf::Out<u32> out);

    // =========================================================================
    // Runtime LDN State (read from SharedState singleton)
    // =========================================================================

    /// Returns 1 if a game is actively using LDN, 0 otherwise
    ams::Result IsGameActive(ams::sf::Out<u32> out);

    /// Returns current LDN CommState (0=None, 1=Initialized, etc.)
    ams::Result GetLdnState(ams::sf::Out<u32> out);

    /// Returns session information (node count, max, local id, is_host)
    ams::Result GetSessionInfo(ams::sf::Out<SessionInfoIpc> out);

    /// Returns last measured RTT in milliseconds
    ams::Result GetLastRtt(ams::sf::Out<u32> out);

    /// Requests the MITM service to reconnect
    ams::Result ForceReconnect();

    /// Returns the process ID of the active game (for debugging)
    ams::Result GetActiveProcessId(ams::sf::Out<u64> out);

    // =========================================================================
    // P2P Proxy Control
    // =========================================================================

    /// Returns 1 if P2P proxy is disabled
    ams::Result GetDisableP2p(ams::sf::Out<u32> out);

    /// Sets P2P proxy disabled state (like Ryujinx MultiplayerDisableP2p)
    ams::Result SetDisableP2p(u32 disabled);
};

} // namespace ryu_ldn::ipc

// =============================================================================
// SF Service Interface Definition (must be outside namespace)
// =============================================================================

/**
 * @brief SF interface macro for ryu:cfg service
 *
 * Defines all IPC commands (0-28) for the configuration service.
 * Commands 0-22: Configuration commands
 * Commands 23-28: Runtime LDN state commands
 * Uses 9-arg form of AMS_SF_METHOD_INFO with explicit version range.
 */
#define AMS_RYU_CFG_SERVICE_INTERFACE(C, H)                                                                                        \
    /* Configuration commands (0-22) */                                                                                            \
    AMS_SF_METHOD_INFO(C, H, 0,  ams::Result, GetVersion,         (ams::sf::Out<std::array<char, 32>> out),            (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 1,  ams::Result, GetConnectionStatus,(ams::sf::Out<u32> out),                             (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 2,  ams::Result, GetPassphrase,      (ams::sf::Out<std::array<char, 64>> out),            (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 3,  ams::Result, SetPassphrase,      (std::array<char, 64> passphrase),                   (passphrase),ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 4,  ams::Result, GetServerAddress,   (ams::sf::Out<ryu_ldn::ipc::ServerAddressIpc> out),  (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 5,  ams::Result, SetServerAddress,   (ryu_ldn::ipc::ServerAddressIpc address),            (address),   ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 6,  ams::Result, GetLdnEnabled,      (ams::sf::Out<u32> out),                             (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 7,  ams::Result, SetLdnEnabled,      (u32 enabled),                                       (enabled),   ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 8,  ams::Result, GetUseTls,          (ams::sf::Out<u32> out),                             (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 9,  ams::Result, SetUseTls,          (u32 enabled),                                       (enabled),   ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 10, ams::Result, GetDebugEnabled,    (ams::sf::Out<u32> out),                             (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 11, ams::Result, SetDebugEnabled,    (u32 enabled),                                       (enabled),   ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 12, ams::Result, GetDebugLevel,      (ams::sf::Out<u32> out),                             (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 13, ams::Result, SetDebugLevel,      (u32 level),                                         (level),     ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 14, ams::Result, GetLogToFile,       (ams::sf::Out<u32> out),                             (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 15, ams::Result, SetLogToFile,       (u32 enabled),                                       (enabled),   ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 16, ams::Result, SaveConfig,         (ams::sf::Out<ryu_ldn::ipc::ConfigResult> out),      (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 17, ams::Result, ReloadConfig,       (ams::sf::Out<ryu_ldn::ipc::ConfigResult> out),      (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 18, ams::Result, GetConnectTimeout,  (ams::sf::Out<u32> out),                             (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 19, ams::Result, SetConnectTimeout,  (u32 timeout_ms),                                    (timeout_ms),ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 20, ams::Result, GetPingInterval,    (ams::sf::Out<u32> out),                             (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 21, ams::Result, SetPingInterval,    (u32 interval_ms),                                   (interval_ms),ams::hos::Version_Min, ams::hos::Version_Max)   \
    AMS_SF_METHOD_INFO(C, H, 22, ams::Result, IsServiceActive,    (ams::sf::Out<u32> out),                             (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    /* Runtime LDN state commands (23-28) */                                                                                       \
    AMS_SF_METHOD_INFO(C, H, 23, ams::Result, IsGameActive,       (ams::sf::Out<u32> out),                             (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 24, ams::Result, GetLdnState,        (ams::sf::Out<u32> out),                             (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 25, ams::Result, GetSessionInfo,     (ams::sf::Out<ryu_ldn::ipc::SessionInfoIpc> out),    (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 26, ams::Result, GetLastRtt,         (ams::sf::Out<u32> out),                             (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 27, ams::Result, ForceReconnect,     (),                                                  (),          ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 28, ams::Result, GetActiveProcessId, (ams::sf::Out<u64> out),                             (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    /* P2P Proxy control commands (29-30) */                                                                                       \
    AMS_SF_METHOD_INFO(C, H, 29, ams::Result, GetDisableP2p,      (ams::sf::Out<u32> out),                             (out),       ams::hos::Version_Min, ams::hos::Version_Max)    \
    AMS_SF_METHOD_INFO(C, H, 30, ams::Result, SetDisableP2p,      (u32 disabled),                                      (disabled),  ams::hos::Version_Min, ams::hos::Version_Max)

/**
 * @brief Define the IConfigService interface
 *
 * Interface ID: 0x52594343 ("RYCC" - RYu Config Controller)
 */
AMS_SF_DEFINE_INTERFACE(ryu_ldn::ipc, IConfigService, AMS_RYU_CFG_SERVICE_INTERFACE, 0x52594343)
