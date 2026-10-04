# =========================================
# LDN:CONFIG
# =========================================

echo [LDN] Loading config breakpoints...\n
# Namespace: ams::mitm::ldn
dprintf ams::mitm::ldn::LdnConfigService::GetServerAddress, "[LDN:CONFIG] GetServerAddress\n"
dprintf ams::mitm::ldn::LdnConfigService::SetServerAddress, "[LDN:CONFIG] SetServerAddress\n"
dprintf ams::mitm::ldn::LdnConfigService::GetDebugEnabled, "[LDN:CONFIG] GetDebugEnabled\n"
dprintf ams::mitm::ldn::LdnConfigService::SetDebugEnabled, "[LDN:CONFIG] SetDebugEnabled\n"
dprintf ams::mitm::ldn::LdnConfigService::GetPassphrase, "[LDN:CONFIG] GetPassphrase\n"
dprintf ams::mitm::ldn::LdnConfigService::SetPassphrase, "[LDN:CONFIG] SetPassphrase\n"
dprintf ams::mitm::ldn::LdnConfigService::GetLdnEnabled, "[LDN:CONFIG] GetLdnEnabled\n"
dprintf ams::mitm::ldn::LdnConfigService::SetLdnEnabled, "[LDN:CONFIG] SetLdnEnabled\n"
dprintf ams::mitm::ldn::LdnConfigService::GetConnectTimeout, "[LDN:CONFIG] GetConnectTimeout\n"
dprintf ams::mitm::ldn::LdnConfigService::SetConnectTimeout, "[LDN:CONFIG] SetConnectTimeout\n"
dprintf ams::mitm::ldn::LdnConfigService::GetPingInterval, "[LDN:CONFIG] GetPingInterval\n"
dprintf ams::mitm::ldn::LdnConfigService::SetPingInterval, "[LDN:CONFIG] SetPingInterval\n"
dprintf ams::mitm::ldn::LdnConfigService::GetReconnectDelay, "[LDN:CONFIG] GetReconnectDelay\n"
dprintf ams::mitm::ldn::LdnConfigService::SetReconnectDelay, "[LDN:CONFIG] SetReconnectDelay\n"
dprintf ams::mitm::ldn::LdnConfigService::GetMaxReconnectAttempts, "[LDN:CONFIG] GetMaxReconnectAttempts\n"
dprintf ams::mitm::ldn::LdnConfigService::SetMaxReconnectAttempts, "[LDN:CONFIG] SetMaxReconnectAttempts\n"
dprintf ams::mitm::ldn::LdnConfigService::GetDebugLevel, "[LDN:CONFIG] GetDebugLevel\n"
dprintf ams::mitm::ldn::LdnConfigService::SetDebugLevel, "[LDN:CONFIG] SetDebugLevel\n"
dprintf ams::mitm::ldn::LdnConfigService::GetLogToFile, "[LDN:CONFIG] GetLogToFile\n"
dprintf ams::mitm::ldn::LdnConfigService::SetLogToFile, "[LDN:CONFIG] SetLogToFile\n"
dprintf ams::mitm::ldn::LdnConfigService::SaveConfig, "[LDN:CONFIG] SaveConfig\n"
dprintf ams::mitm::ldn::LdnConfigService::ReloadConfig, "[LDN:CONFIG] ReloadConfig\n"

echo [LDN] config: 22 dprintf points\n
