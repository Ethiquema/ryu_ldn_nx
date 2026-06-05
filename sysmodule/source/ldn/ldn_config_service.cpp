/**
 * @file ldn_config_service.cpp
 * @brief Configuration IPC service implementation
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ldn_config_service.hpp"
#include "ldn_shared_state.hpp"
#include "../config/config.hpp"
#include "../config/config_manager.hpp"

#ifndef RYU_LDN_VERSION
#define RYU_LDN_VERSION "0.1.0-dev"
#endif

namespace ams::mitm::ldn {

LdnConfigService::LdnConfigService(LdnICommunication* communication)
    : m_communication(communication)
{
    // m_communication can be null if created without a parent service
}

Result LdnConfigService::GetVersion(sf::Out<std::array<char, 32>> out) {
    std::array<char, 32> version{};
    std::strncpy(version.data(), RYU_LDN_VERSION, 31);
    *out = version;
    R_SUCCEED();
}

Result LdnConfigService::GetConnectionStatus(sf::Out<ConnectionStatus> out) {
    auto& shared_state = SharedState::GetInstance();
    CommState ldn_state = shared_state.GetLdnState();

    // Map LDN state to connection status
    switch (ldn_state) {
        case CommState::AccessPointCreated:
        case CommState::StationConnected:
            *out = ConnectionStatus::Ready;
            break;
        case CommState::AccessPoint:
        case CommState::Station:
            *out = ConnectionStatus::Connected;
            break;
        case CommState::Initialized:
            *out = ConnectionStatus::Connecting;
            break;
        default:
            *out = ConnectionStatus::Disconnected;
            break;
    }
    R_SUCCEED();
}

Result LdnConfigService::GetLdnState(sf::Out<u32> out) {
    auto& shared_state = SharedState::GetInstance();
    *out = static_cast<u32>(shared_state.GetLdnState());
    R_SUCCEED();
}

Result LdnConfigService::GetSessionInfo(sf::Out<SessionInfo> out) {
    auto& shared_state = SharedState::GetInstance();
    *out = shared_state.GetSessionInfoStruct();
    R_SUCCEED();
}

Result LdnConfigService::GetServerAddress(sf::Out<ServerAddress> out) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    ServerAddress addr{};
    std::strncpy(addr.host, cfg.GetServerHost(), 63);
    addr.host[63] = '\0';
    addr.port = cfg.GetServerPort();
    *out = addr;
    R_SUCCEED();
}

Result LdnConfigService::SetServerAddress(const ServerAddress &address) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    cfg.SetServerHost(address.host);
    cfg.SetServerPort(address.port);

    // Trigger reconnection if connected
    auto& shared_state = SharedState::GetInstance();
    CommState state = shared_state.GetLdnState();
    if (state != CommState::None && state != CommState::Initialized) {
        shared_state.RequestReconnect();
    }
    R_SUCCEED();
}

Result LdnConfigService::GetDebugEnabled(sf::Out<u32> out) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    *out = cfg.GetDebugEnabled() ? 1 : 0;
    R_SUCCEED();
}

Result LdnConfigService::SetDebugEnabled(u32 enabled) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    cfg.SetDebugEnabled(enabled != 0);
    R_SUCCEED();
}

Result LdnConfigService::ForceReconnect() {
    // Request reconnection via shared state
    auto& shared_state = SharedState::GetInstance();
    shared_state.RequestReconnect();
    R_SUCCEED();
}

Result LdnConfigService::GetLastRtt(sf::Out<u32> out) {
    auto& shared_state = SharedState::GetInstance();
    *out = shared_state.GetLastRtt();
    R_SUCCEED();
}

// =============================================================================
// Extended Configuration Commands (65011-65030)
// =============================================================================

Result LdnConfigService::GetPassphrase(sf::Out<Passphrase> out) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    Passphrase p{};
    std::strncpy(p.passphrase, cfg.GetPassphrase(), 63);
    p.passphrase[63] = '\0';
    *out = p;
    R_SUCCEED();
}

Result LdnConfigService::SetPassphrase(Passphrase passphrase) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    cfg.SetPassphrase(passphrase.passphrase);
    R_SUCCEED();
}

Result LdnConfigService::GetLdnEnabled(sf::Out<u32> out) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    *out = cfg.GetLdnEnabled() ? 1 : 0;
    R_SUCCEED();
}

Result LdnConfigService::SetLdnEnabled(u32 enabled) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    cfg.SetLdnEnabled(enabled != 0);
    R_SUCCEED();
}

Result LdnConfigService::GetUseTls(sf::Out<u32> out) {
    *out = 0;  // TLS is never used — always plain TCP
    R_SUCCEED();
}

Result LdnConfigService::SetUseTls(u32 enabled) {
    // TLS is not implemented. Log warning if enabled is requested.
    if (enabled != 0) {
        // TLS not supported — no-op
    }
    R_SUCCEED();
}


// ============================================================================

// Network Timeout Stubs (hardcoded values)
// ============================================================================

Result LdnConfigService::GetConnectTimeout(sf::Out<u32> out) {
    *out = 5000;  // Hardcoded: 5s connection timeout
    R_SUCCEED();
}

Result LdnConfigService::SetConnectTimeout(u32 timeout_ms) {
    // Network timeouts are now hardcoded. Kept for IPC protocol compatibility.
    (void)timeout_ms;  // Suppress unused warning
    R_SUCCEED();
}

Result LdnConfigService::GetPingInterval(sf::Out<u32> out) {
    *out = 0;  // Hardcoded: server drives pings
    R_SUCCEED();
}

Result LdnConfigService::SetPingInterval(u32 interval_ms) {
    // Ping interval is now hardcoded at 0. Kept for IPC protocol compatibility.
    (void)interval_ms;  // Suppress unused warning
    R_SUCCEED();
}

Result LdnConfigService::GetReconnectDelay(sf::Out<u32> out) {
    *out = 1000;  // Hardcoded: 1s initial reconnect delay
    R_SUCCEED();
}

Result LdnConfigService::SetReconnectDelay(u32 delay_ms) {
    // Reconnect delay is now hardcoded. Kept for IPC protocol compatibility.
    (void)delay_ms;  // Suppress unused warning
    R_SUCCEED();
}

Result LdnConfigService::GetMaxReconnectAttempts(sf::Out<u32> out) {
    *out = 0;  // Hardcoded: auto-reconnect disabled
    R_SUCCEED();
}

Result LdnConfigService::SetMaxReconnectAttempts(u32 attempts) {
    // Max reconnect attempts is now hardcoded. Kept for IPC protocol compatibility.
    (void)attempts;  // Suppress unused warning
    R_SUCCEED();
}

// ============================================================================
// Debug Settings
// ============================================================================

Result LdnConfigService::GetDebugLevel(sf::Out<u32> out) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    *out = cfg.GetDebugLevel();
    R_SUCCEED();
}

Result LdnConfigService::SetDebugLevel(u32 level) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    cfg.SetDebugLevel(level);
    R_SUCCEED();
}

Result LdnConfigService::GetLogToFile(sf::Out<u32> out) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    *out = cfg.GetDebugEnabled() ? 1 : 0;  // Maps to debug.enabled
    R_SUCCEED();
}

Result LdnConfigService::SetLogToFile(u32 enabled) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    cfg.SetDebugEnabled(enabled != 0);  // Maps to debug.enabled
    R_SUCCEED();
}

Result LdnConfigService::SaveConfig(sf::Out<ConfigResult> out) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    auto result = cfg.Save();

    // Convert ryu_ldn::config::ConfigResult to IPC ConfigResult
    switch (result) {
        case ryu_ldn::config::ConfigResult::Success:
            *out = ConfigResult::Success;
            break;
        case ryu_ldn::config::ConfigResult::FileNotFound:
            *out = ConfigResult::FileNotFound;
            break;
        case ryu_ldn::config::ConfigResult::ParseError:
            *out = ConfigResult::ParseError;
            break;
        case ryu_ldn::config::ConfigResult::IoError:
        default:
            *out = ConfigResult::IoError;
            break;
    }
    R_SUCCEED();
}

Result LdnConfigService::ReloadConfig(sf::Out<ConfigResult> out) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    auto result = cfg.Reload();

    // Convert ryu_ldn::config::ConfigResult to IPC ConfigResult
    switch (result) {
        case ryu_ldn::config::ConfigResult::Success:
            *out = ConfigResult::Success;
            break;
        case ryu_ldn::config::ConfigResult::FileNotFound:
            *out = ConfigResult::FileNotFound;
            break;
        case ryu_ldn::config::ConfigResult::ParseError:
            *out = ConfigResult::ParseError;
            break;
        case ryu_ldn::config::ConfigResult::IoError:
        default:
            *out = ConfigResult::IoError;
            break;
    }
    R_SUCCEED();
}

} // namespace ams::mitm::ldn
