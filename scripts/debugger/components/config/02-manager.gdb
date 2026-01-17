# ==========================================
# Config Component - Manager Operations
# ==========================================
# Configuration manager lifecycle and settings
# Using dprintf for automatic continue

echo [CONFIG] Loading manager breakpoints...\n

# Manager lifecycle
dprintf ryu_ldn::config::ConfigManager::Instance, "[CONFIG:MGR] Getting manager instance\n"
dprintf ryu_ldn::config::ConfigManager::Initialize, "[CONFIG:MGR] Initializing configuration manager\n"
dprintf ryu_ldn::config::ConfigManager::Save, "[CONFIG:MGR] Saving configuration to disk\n"
dprintf ryu_ldn::config::ConfigManager::Reload, "[CONFIG:MGR] Reloading configuration from disk\n"

# Server settings
dprintf ryu_ldn::config::ConfigManager::SetServerHost, "[CONFIG:MGR] Setting server host\n"
dprintf ryu_ldn::config::ConfigManager::SetServerPort, "[CONFIG:MGR] Setting server port\n"
dprintf ryu_ldn::config::ConfigManager::SetUseTls, "[CONFIG:MGR] Setting TLS enabled state\n"

# Network settings
dprintf ryu_ldn::config::ConfigManager::SetConnectTimeout, "[CONFIG:MGR] Setting connect timeout\n"
dprintf ryu_ldn::config::ConfigManager::SetPingInterval, "[CONFIG:MGR] Setting ping interval\n"
dprintf ryu_ldn::config::ConfigManager::SetReconnectDelay, "[CONFIG:MGR] Setting reconnect delay\n"
dprintf ryu_ldn::config::ConfigManager::SetMaxReconnectAttempts, "[CONFIG:MGR] Setting max reconnect attempts\n"

# LDN settings
dprintf ryu_ldn::config::ConfigManager::SetLdnEnabled, "[CONFIG:MGR] Setting LDN enabled\n"
dprintf ryu_ldn::config::ConfigManager::SetPassphrase, "[CONFIG:MGR] Setting passphrase\n"
dprintf ryu_ldn::config::ConfigManager::SetInterfaceName, "[CONFIG:MGR] Setting interface name\n"

# Debug settings
dprintf ryu_ldn::config::ConfigManager::SetDebugEnabled, "[CONFIG:MGR] Setting debug enabled\n"
dprintf ryu_ldn::config::ConfigManager::SetDebugLevel, "[CONFIG:MGR] Setting debug level\n"
dprintf ryu_ldn::config::ConfigManager::SetLogToFile, "[CONFIG:MGR] Setting log to file\n"

# Change tracking
dprintf ryu_ldn::config::ConfigManager::NotifyChange, "[CONFIG:MGR] Config change notified\n"
dprintf ryu_ldn::config::ConfigManager::SetChangeCallback, "[CONFIG:MGR] Setting change callback\n"

# Utility functions
dprintf ryu_ldn::config::IsValidPassphrase, "[CONFIG:MGR] Validating passphrase\n"
dprintf ryu_ldn::config::GenerateRandomPassphrase, "[CONFIG:MGR] Generating random passphrase\n"

echo [CONFIG] Manager: 21 dprintf points\n
