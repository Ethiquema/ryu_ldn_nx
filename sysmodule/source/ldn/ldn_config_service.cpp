/**
 * @file ldn_config_service.cpp
 * @brief Configuration IPC service implementation
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ldn_config_service.hpp"
#include "../config/config.hpp"

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
    ServerAddress addr{};

    // Get from config
    // TODO: Use actual config singleton
    std::strncpy(addr.host, "ldn.ryujinx.app", 63);
    addr.port = 30456;

    *out = addr;
    R_SUCCEED();
}

Result LdnConfigService::SetServerAddress(ServerAddress address) {
    AMS_UNUSED(address);
    // TODO: Update config and trigger reconnection
    // Validation and config update would go here
    R_SUCCEED();
}

Result LdnConfigService::GetDebugEnabled(sf::Out<u32> out) {
    // TODO: Get from config
    *out = 0;
    R_SUCCEED();
}

Result LdnConfigService::SetDebugEnabled(u32 enabled) {
    AMS_UNUSED(enabled);
    // TODO: Update config
    // ryu_ldn::Config::Get().SetDebugEnabled(enabled != 0);
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

} // namespace ams::mitm::ldn
