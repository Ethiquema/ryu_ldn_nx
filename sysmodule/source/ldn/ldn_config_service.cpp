/**
 * @file ldn_config_service.cpp
 * @brief Configuration IPC service implementation
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ldn_config_service.hpp"
#include "../config/config.hpp"
#include "../config/config_manager.hpp"

#ifndef RYU_LDN_VERSION
#define RYU_LDN_VERSION "0.1.0-dev"
#endif

namespace ams::mitm::ldn {

LdnConfigService::LdnConfigService(LdnICommunication* communication)
    : m_communication(communication)
{
    AMS_UNUSED(m_communication);  // TODO: Use when integration is complete
}

Result LdnConfigService::GetVersion(sf::Out<std::array<char, 32>> out) {
    std::array<char, 32> version{};
    std::strncpy(version.data(), RYU_LDN_VERSION, 31);
    *out = version;
    R_SUCCEED();
}

Result LdnConfigService::GetConnectionStatus(sf::Out<ConnectionStatus> out) {
    // TODO: Get actual connection status from network client
    // For now, return disconnected as placeholder
    *out = ConnectionStatus::Disconnected;
    R_SUCCEED();
}

Result LdnConfigService::GetLdnState(sf::Out<u32> out) {
    // TODO: Get actual state from LdnICommunication
    *out = 0;  // None
    R_SUCCEED();
}

Result LdnConfigService::GetSessionInfo(sf::Out<SessionInfo> out) {
    SessionInfo info{};
    // TODO: Get actual session info from LdnICommunication
    *out = info;
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

Result LdnConfigService::SetServerAddress(ServerAddress address) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    cfg.SetServerHost(address.host);
    cfg.SetServerPort(address.port);
    // TODO: Trigger reconnection if connected
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
    // TODO: Trigger network client reconnection
    // if (m_communication != nullptr) {
    //     m_communication->ForceReconnect();
    // }
    R_SUCCEED();
}

Result LdnConfigService::GetLastRtt(sf::Out<u32> out) {
    // TODO: Get actual RTT from network client
    *out = 0;
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
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    *out = cfg.GetUseTls() ? 1 : 0;
    R_SUCCEED();
}

Result LdnConfigService::SetUseTls(u32 enabled) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    cfg.SetUseTls(enabled != 0);
    R_SUCCEED();
}

Result LdnConfigService::GetConnectTimeout(sf::Out<u32> out) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    *out = cfg.GetConnectTimeout();
    R_SUCCEED();
}

Result LdnConfigService::SetConnectTimeout(u32 timeout_ms) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    cfg.SetConnectTimeout(timeout_ms);
    R_SUCCEED();
}

Result LdnConfigService::GetPingInterval(sf::Out<u32> out) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    *out = cfg.GetPingInterval();
    R_SUCCEED();
}

Result LdnConfigService::SetPingInterval(u32 interval_ms) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    cfg.SetPingInterval(interval_ms);
    R_SUCCEED();
}

Result LdnConfigService::GetReconnectDelay(sf::Out<u32> out) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    *out = cfg.GetReconnectDelay();
    R_SUCCEED();
}

Result LdnConfigService::SetReconnectDelay(u32 delay_ms) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    cfg.SetReconnectDelay(delay_ms);
    R_SUCCEED();
}

Result LdnConfigService::GetMaxReconnectAttempts(sf::Out<u32> out) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    *out = cfg.GetMaxReconnectAttempts();
    R_SUCCEED();
}

Result LdnConfigService::SetMaxReconnectAttempts(u32 attempts) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    cfg.SetMaxReconnectAttempts(attempts);
    R_SUCCEED();
}

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
    *out = cfg.GetLogToFile() ? 1 : 0;
    R_SUCCEED();
}

Result LdnConfigService::SetLogToFile(u32 enabled) {
    auto& cfg = ryu_ldn::config::ConfigManager::Instance();
    cfg.SetLogToFile(enabled != 0);
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
