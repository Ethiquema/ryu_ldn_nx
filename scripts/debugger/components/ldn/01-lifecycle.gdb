# ==========================================
# LDN Component - Service Lifecycle
# ==========================================
# LDN service initialization
# Using dprintf for automatic continue

echo [LDN] Loading service lifecycle breakpoints...\n

# Service lifecycle
dprintf ams::mitm::ldn::LdnConfigService::LdnConfigService, "[LDN:LIFECYCLE] Service constructor\n"

# Version and status
dprintf ams::mitm::ldn::LdnConfigService::GetVersion, "[LDN:LIFECYCLE] Version queried\n"
dprintf ams::mitm::ldn::LdnConfigService::GetConnectionStatus, "[LDN:LIFECYCLE] Connection status queried\n"
dprintf ams::mitm::ldn::LdnConfigService::GetLdnState, "[LDN:LIFECYCLE] LDN state queried\n"
dprintf ams::mitm::ldn::LdnConfigService::GetSessionInfo, "[LDN:LIFECYCLE] Session info queried\n"

echo [LDN] Service lifecycle: 5 dprintf points\n
