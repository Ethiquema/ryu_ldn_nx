# ==========================================
# LDN Component - Configuration
# ==========================================
# LDN configuration getters and setters
# Using dprintf for automatic continue

echo [LDN] Loading configuration breakpoints...\n

# Server configuration
dprintf ams::mitm::ldn::LdnConfigService::GetServerAddress, "[LDN:CONFIG] GetServerAddress\n"
dprintf ams::mitm::ldn::LdnConfigService::SetServerAddress, "[LDN:CONFIG] SetServerAddress\n"
dprintf ams::mitm::ldn::LdnConfigService::GetPassphrase, "[LDN:CONFIG] GetPassphrase\n"
dprintf ams::mitm::ldn::LdnConfigService::SetPassphrase, "[LDN:CONFIG] SetPassphrase\n"

# Feature flags
dprintf ams::mitm::ldn::LdnConfigService::GetLdnEnabled, "[LDN:CONFIG] GetLdnEnabled\n"
dprintf ams::mitm::ldn::LdnConfigService::SetLdnEnabled, "[LDN:CONFIG] SetLdnEnabled\n"
dprintf ams::mitm::ldn::LdnConfigService::GetUseTls, "[LDN:CONFIG] GetUseTls\n"
dprintf ams::mitm::ldn::LdnConfigService::SetUseTls, "[LDN:CONFIG] SetUseTls\n"
dprintf ams::mitm::ldn::LdnConfigService::GetDebugEnabled, "[LDN:CONFIG] GetDebugEnabled\n"
dprintf ams::mitm::ldn::LdnConfigService::SetDebugEnabled, "[LDN:CONFIG] SetDebugEnabled\n"

# Network timing
dprintf ams::mitm::ldn::LdnConfigService::GetConnectTimeout, "[LDN:CONFIG] GetConnectTimeout\n"
dprintf ams::mitm::ldn::LdnConfigService::SetConnectTimeout, "[LDN:CONFIG] SetConnectTimeout\n"
dprintf ams::mitm::ldn::LdnConfigService::GetPingInterval, "[LDN:CONFIG] GetPingInterval\n"
dprintf ams::mitm::ldn::LdnConfigService::SetPingInterval, "[LDN:CONFIG] SetPingInterval\n"
dprintf ams::mitm::ldn::LdnConfigService::GetReconnectDelay, "[LDN:CONFIG] GetReconnectDelay\n"
dprintf ams::mitm::ldn::LdnConfigService::SetReconnectDelay, "[LDN:CONFIG] SetReconnectDelay\n"
dprintf ams::mitm::ldn::LdnConfigService::GetMaxReconnectAttempts, "[LDN:CONFIG] GetMaxReconnectAttempts\n"
dprintf ams::mitm::ldn::LdnConfigService::SetMaxReconnectAttempts, "[LDN:CONFIG] SetMaxReconnectAttempts\n"

# Debug settings
dprintf ams::mitm::ldn::LdnConfigService::GetDebugLevel, "[LDN:CONFIG] GetDebugLevel\n"
dprintf ams::mitm::ldn::LdnConfigService::SetDebugLevel, "[LDN:CONFIG] SetDebugLevel\n"
dprintf ams::mitm::ldn::LdnConfigService::GetLogToFile, "[LDN:CONFIG] GetLogToFile\n"
dprintf ams::mitm::ldn::LdnConfigService::SetLogToFile, "[LDN:CONFIG] SetLogToFile\n"

# Config persistence
dprintf ams::mitm::ldn::LdnConfigService::SaveConfig, "[LDN:CONFIG] SaveConfig\n"
dprintf ams::mitm::ldn::LdnConfigService::ReloadConfig, "[LDN:CONFIG] ReloadConfig\n"

echo [LDN] Configuration: 24 dprintf points\n
