# ==========================================
# Config Component - IPC Service
# ==========================================
# Configuration IPC service operations
# Using dprintf for automatic continue

echo [CONFIG] Loading IPC service breakpoints...\n

# IPC Service lifecycle
dprintf ryu_ldn::config::ConfigService::ConfigService, "[CONFIG:IPC] Service constructor\n"
dprintf ryu_ldn::config::ConfigService::~ConfigService, "[CONFIG:IPC] Service destructor\n"

# IPC command handlers
dprintf ryu_ldn::config::ConfigService::GetConfig, "[CONFIG:IPC] GetConfig command\n"
dprintf ryu_ldn::config::ConfigService::SetConfig, "[CONFIG:IPC] SetConfig command\n"
dprintf ryu_ldn::config::ConfigService::GetServerHost, "[CONFIG:IPC] GetServerHost command\n"
dprintf ryu_ldn::config::ConfigService::SetServerHost, "[CONFIG:IPC] SetServerHost command\n"
dprintf ryu_ldn::config::ConfigService::GetServerPort, "[CONFIG:IPC] GetServerPort command\n"
dprintf ryu_ldn::config::ConfigService::SetServerPort, "[CONFIG:IPC] SetServerPort command\n"
dprintf ryu_ldn::config::ConfigService::GetUseTls, "[CONFIG:IPC] GetUseTls command\n"
dprintf ryu_ldn::config::ConfigService::SetUseTls, "[CONFIG:IPC] SetUseTls command\n"

# Network settings via IPC
dprintf ryu_ldn::config::ConfigService::GetConnectTimeout, "[CONFIG:IPC] GetConnectTimeout command\n"
dprintf ryu_ldn::config::ConfigService::SetConnectTimeout, "[CONFIG:IPC] SetConnectTimeout command\n"

# LDN settings via IPC
dprintf ryu_ldn::config::ConfigService::GetLdnEnabled, "[CONFIG:IPC] GetLdnEnabled command\n"
dprintf ryu_ldn::config::ConfigService::SetLdnEnabled, "[CONFIG:IPC] SetLdnEnabled command\n"
dprintf ryu_ldn::config::ConfigService::SetPassphrase, "[CONFIG:IPC] SetPassphrase command\n"

# Debug settings via IPC
dprintf ryu_ldn::config::ConfigService::GetDebugEnabled, "[CONFIG:IPC] GetDebugEnabled command\n"
dprintf ryu_ldn::config::ConfigService::SetDebugEnabled, "[CONFIG:IPC] SetDebugEnabled command\n"
dprintf ryu_ldn::config::ConfigService::SetDebugLevel, "[CONFIG:IPC] SetDebugLevel command\n"

# Save/Load
dprintf ryu_ldn::config::ConfigService::Save, "[CONFIG:IPC] Save command\n"
dprintf ryu_ldn::config::ConfigService::Reload, "[CONFIG:IPC] Reload command\n"

echo [CONFIG] IPC Service: 20 dprintf points\n
