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

// =============================================================================
// Type wrappers for template types used in SF macros (MUST be at global scope)
// =============================================================================
//
// The C preprocessor cannot handle angle brackets or double-colons inside
// macro arguments when they appear inside AMS_SF_METHOD_INFO macros.
// Even when wrapped in parentheses, `<` and `::` corrupt the macro
// argument count and cause all subsequent methods in the same CMD_MACRO
// expansion to receive garbage parameter types (typically `int`).
//
// We define plain structs for the data types at global scope so they can
// be referenced without any `::` qualifier inside the SF macro.
// The member is named `buf` (not `data`) to avoid conflicting with
// std::array::data().
struct RyuCfgVersionString {
    std::array<char, 32> buf;
    static constexpr size_t size() { return 32; }
};
struct RyuCfgPassphraseString {
    std::array<char, 64> buf;
    static constexpr size_t size() { return 64; }
};

// Global-scope typedefs for the wrapper structs (used in SF macro)
using RyuCfgOutVersionString    = ams::sf::Out<RyuCfgVersionString>;
using RyuCfgOutPassphraseString = ams::sf::Out<RyuCfgPassphraseString>;

/**
 * @brief IPC command IDs for ryu:cfg service
 */
enum class ConfigCmd : u32 {
    // Sysmodule Status (0-8)
    GetVersion          = 0,   ///< Get sysmodule version string
    GetConnectionStatus = 1,   ///< Get current connection state
    IsServiceActive     = 2,   ///< Check if IPC service is active
    IsGameActive        = 3,   ///< Returns 1 if a game is using LDN
    GetLdnState         = 4,   ///< Returns CommState (0-6)
    GetSessionInfo      = 5,   ///< Returns SessionInfoIpc struct (8 bytes)
    GetLastRtt          = 6,   ///< Returns last RTT in milliseconds
    ForceReconnect      = 7,   ///< Requests reconnection
    GetActiveProcessId  = 8,   ///< Returns PID of active game (debug)

    // Sysmodule General Settings (9-14)
    GetDebugEnabled     = 9,   ///< Check debug logging state
    SetDebugEnabled     = 10,  ///< Toggle debug logging
    GetDebugLevel       = 11,  ///< Get log verbosity (0-3)
    SetDebugLevel       = 12,  ///< Set log verbosity
    SaveConfig          = 13,  ///< Persist config to SD card
    ReloadConfig        = 14,  ///< Reload config from SD card

    // Sysmodule Configuration Manager (15-24)
    GetServerAddress    = 15,  ///< Get server host and port
    SetServerAddress    = 16,  ///< Set server host and port
    GetLdnEnabled       = 17,  ///< Check if LDN emulation is on
    SetLdnEnabled       = 18,  ///< Toggle LDN emulation
    GetDisableP2p       = 19,  ///< Returns 1 if P2P proxy is disabled
    SetDisableP2p       = 20,  ///< Sets P2P proxy disabled state
    GetUsePassphrase    = 21,  ///< Check passphrase filtering state
    SetUsePassphrase    = 22,  ///< Toggle passphrase filtering
    GetPassphrase       = 23,  ///< Get room passphrase
    SetPassphrase       = 24,  ///< Set room passphrase
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
 * @brief Result codes returned by IPC setters on invalid input
 *
 * The ryu_ldn_nx config IPC service uses libstratosphere module 353
 * (a value chosen to be clearly outside the official Horizon module
 * range so it never collides with a real system error). Description 1
 * denotes an invalid argument passed by the IPC caller.
 *
 * Returned by SetPassphrase / SetServerAddress / SetDebugLevel when the
 * caller supplies out-of-range lengths, non-printable characters, or
 * shell-injection metacharacters.
 */
R_DEFINE_NAMESPACE_RESULT_MODULE(ryu_ldn::ipc::result, 353);

namespace ryu_ldn::ipc::result {
    R_DEFINE_ERROR_RESULT(InvalidIpcInput, 1);
}

namespace ryu_ldn::ipc {

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

} // namespace ryu_ldn::ipc

// =============================================================================
// Global-scope typedefs for namespace types used in SF macro
// =============================================================================
//
// These MUST be at global scope because AMS_SF_DEFINE_INTERFACE is at
// global scope. The C preprocessor cannot handle `::` inside macro
// arguments, so we create plain aliases for every namespaced type.
using RyuCfgOutSessionInfo    = ams::sf::Out<ryu_ldn::ipc::SessionInfoIpc>;
using RyuCfgOutConfigResult   = ams::sf::Out<ConfigResult>;
using RyuCfgOutServerAddress  = ams::sf::Out<ryu_ldn::ipc::ServerAddressIpc>;
using RyuCfgInServerAddress   = const ryu_ldn::ipc::ServerAddressIpc &;

// =============================================================================
// SF Service Interface Definition (MUST be at global scope)
// =============================================================================

/**
 * @brief SF interface macro for ryu:cfg service
 *
 * Defines all IPC commands (0-24) for the configuration service.
 * Command IDs match ConfigCmd in this header.
 * Groups: Status (0-8), General Settings (9-14), Config Manager (15-24).
 * Uses 9-arg form of AMS_SF_METHOD_INFO with explicit version range.
 */
#define AMS_RYU_CFG_SERVICE_INTERFACE(C, H)                                                                                              \
    /* Sysmodule Status (0-8) */                                                                                                           \
    AMS_SF_METHOD_INFO(C, H, 0,  ams::Result, GetVersion,         (RyuCfgOutVersionString out),                      (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 1,  ams::Result, GetConnectionStatus,(ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 2,  ams::Result, IsServiceActive,    (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 3,  ams::Result, IsGameActive,       (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 4,  ams::Result, GetLdnState,        (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 5,  ams::Result, GetSessionInfo,     (RyuCfgOutSessionInfo out),                        (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 6,  ams::Result, GetLastRtt,         (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 7,  ams::Result, ForceReconnect,     (),                                                   (),           ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 8,  ams::Result, GetActiveProcessId, (ams::sf::Out<u64> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    /* Sysmodule General Settings (9-14) */                                                                                                \
    AMS_SF_METHOD_INFO(C, H, 9,  ams::Result, GetDebugEnabled,    (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 10, ams::Result, SetDebugEnabled,    (u32 enabled),                                        (enabled),    ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 11, ams::Result, GetDebugLevel,      (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 12, ams::Result, SetDebugLevel,      (u32 level),                                          (level),      ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 13, ams::Result, SaveConfig,         (RyuCfgOutConfigResult out),                       (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 14, ams::Result, ReloadConfig,       (RyuCfgOutConfigResult out),                       (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    /* Sysmodule Configuration Manager (15-24) */                                                                                           \
    AMS_SF_METHOD_INFO(C, H, 15, ams::Result, GetServerAddress,   (RyuCfgOutServerAddress out),                      (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 16, ams::Result, SetServerAddress,   (RyuCfgInServerAddress address),                    (address),    ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 17, ams::Result, GetLdnEnabled,      (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 18, ams::Result, SetLdnEnabled,      (u32 enabled),                                        (enabled),    ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 19, ams::Result, GetDisableP2p,      (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 20, ams::Result, SetDisableP2p,      (u32 disabled),                                       (disabled),   ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 21, ams::Result, GetUsePassphrase,   (ams::sf::Out<u32> out),                              (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 22, ams::Result, SetUsePassphrase,   (u32 enabled),                                        (enabled),    ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 23, ams::Result, GetPassphrase,      (RyuCfgOutPassphraseString out),                   (out),        ams::hos::Version_Min, ams::hos::Version_Max) \
    AMS_SF_METHOD_INFO(C, H, 24, ams::Result, SetPassphrase,      (RyuCfgPassphraseString passphrase),                 (passphrase), ams::hos::Version_Min, ams::hos::Version_Max)

/**
 * @brief Define the IConfigService interface
 *
 * Interface ID: 0x52594343 ("RYCC" - RYu Config Controller)
 */
// codeql[cpp/unused-local-variable,cpp/unused-static-variable] — macro
// expansion uses `args` via perfect forwarding
AMS_SF_DEFINE_INTERFACE(ryu_ldn::ipc, IConfigService, AMS_RYU_CFG_SERVICE_INTERFACE, 0x52594343)

namespace ryu_ldn::ipc {

/**
 * @brief Configuration IPC service implementation
 */
class ConfigService {
public:
    ConfigService() = default;

    // =========================================================================
    // Sysmodule Status (IDs 0-8)
    // =========================================================================

    /** @brief Get the sysmodule version string */
    /// @gdb{tag="CONFIG:IPC", msg="GetVersion"}
    ams::Result GetVersion(RyuCfgOutVersionString out);

    /** @brief Get connection status (0 = service running) */
    /// @gdb{tag="CONFIG:IPC", msg="GetConnectionStatus"}
    ams::Result GetConnectionStatus(ams::sf::Out<u32> out);

    /** @brief Check if the IPC service is active */
    /// @gdb{tag="CONFIG:IPC", msg="IsServiceActive"}
    ams::Result IsServiceActive(ams::sf::Out<u32> out);

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
    ams::Result GetSessionInfo(RyuCfgOutSessionInfo out);

    /** @brief Get last measured RTT
     *  @param out RTT in milliseconds
     */
    /// @gdb{tag="CONFIG:IPC", msg="GetLastRtt"}
    ams::Result GetLastRtt(ams::sf::Out<u32> out);

    /** @brief Request the MITM service to reconnect */
    /// @gdb{tag="CONFIG:IPC", msg="ForceReconnect"}
    ams::Result ForceReconnect();

    /** @brief Get the process ID of the active game
     *  @param out Process id (for debugging)
     */
    /// @gdb{tag="CONFIG:IPC", msg="GetActiveProcessId"}
    ams::Result GetActiveProcessId(ams::sf::Out<u64> out);

    // =========================================================================
    // Sysmodule General Settings (IDs 9-14)
    // =========================================================================

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
    ams::Result SaveConfig(RyuCfgOutConfigResult out);
    /** @brief Reload configuration from disk
     *  @param out Result of the reload operation
     */
    /// @gdb{tag="CONFIG:IPC", msg="ReloadConfig"}
    ams::Result ReloadConfig(RyuCfgOutConfigResult out);

    // =========================================================================
    // Sysmodule Configuration Manager (IDs 15-24)
    // =========================================================================

    /** @brief Get the configured server address (host + port) */
    /// @gdb{tag="CONFIG:IPC", msg="GetServerAddress"}
    ams::Result GetServerAddress(RyuCfgOutServerAddress out);
    /** @brief Set the server address (host + port)
     *  @param address New server address to use
     */
    /// @gdb{tag="CONFIG:IPC", msg="SetServerAddress"}
    ams::Result SetServerAddress(RyuCfgInServerAddress address);

    /** @brief Get whether LDN emulation is enabled */
    /// @gdb{tag="CONFIG:IPC", msg="GetLdnEnabled"}
    ams::Result GetLdnEnabled(ams::sf::Out<u32> out);
    /** @brief Enable or disable LDN emulation
     *  @param enabled 1 to enable, 0 to disable
     */
    /// @gdb{tag="CONFIG:IPC", msg="SetLdnEnabled"}
    ams::Result SetLdnEnabled(u32 enabled);

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

    /** @brief Get the current LDN room passphrase */
    /// @gdb{tag="CONFIG:IPC", msg="GetPassphrase"}
    ams::Result GetPassphrase(RyuCfgOutPassphraseString out);
    /** @brief Set the LDN room passphrase
     *  @param passphrase Null-terminated passphrase string (max 63 chars)
     */
    /// @gdb{tag="CONFIG:IPC", msg="SetPassphrase"}
    ams::Result SetPassphrase(RyuCfgPassphraseString passphrase);
};

} // namespace ryu_ldn::ipc
