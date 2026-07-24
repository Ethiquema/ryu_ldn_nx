# =========================================
# CONFIG:IPC
# =========================================

echo [CONFIG] Loading ipc breakpoints...\n
# Namespace: ryu_ldn::ipc
dprintf ryu_ldn::ipc::ConfigService::GetVersion, "[CONFIG:IPC] GetVersion\n"
dprintf ryu_ldn::ipc::ConfigService::GetConnectionStatus, "[CONFIG:IPC] GetConnectionStatus\n"
dprintf ryu_ldn::ipc::ConfigService::GetPassphrase, "[CONFIG:IPC] GetPassphrase\n"
dprintf ryu_ldn::ipc::ConfigService::SetPassphrase, "[CONFIG:IPC] SetPassphrase\n"
dprintf ryu_ldn::ipc::ConfigService::GetServerAddress, "[CONFIG:IPC] GetServerAddress\n"
dprintf ryu_ldn::ipc::ConfigService::SetServerAddress, "[CONFIG:IPC] SetServerAddress\n"
dprintf ryu_ldn::ipc::ConfigService::GetLdnEnabled, "[CONFIG:IPC] GetLdnEnabled\n"
dprintf ryu_ldn::ipc::ConfigService::SetLdnEnabled, "[CONFIG:IPC] SetLdnEnabled\n"
dprintf ryu_ldn::ipc::ConfigService::GetDebugEnabled, "[CONFIG:IPC] GetDebugEnabled\n"
dprintf ryu_ldn::ipc::ConfigService::SetDebugEnabled, "[CONFIG:IPC] SetDebugEnabled\n"
dprintf ryu_ldn::ipc::ConfigService::GetDebugLevel, "[CONFIG:IPC] GetDebugLevel\n"
dprintf ryu_ldn::ipc::ConfigService::SetDebugLevel, "[CONFIG:IPC] SetDebugLevel\n"
dprintf ryu_ldn::ipc::ConfigService::GetLogToFile, "[CONFIG:IPC] GetLogToFile\n"
dprintf ryu_ldn::ipc::ConfigService::SetLogToFile, "[CONFIG:IPC] SetLogToFile\n"
dprintf ryu_ldn::ipc::ConfigService::GetConnectTimeout, "[CONFIG:IPC] GetConnectTimeout\n"
dprintf ryu_ldn::ipc::ConfigService::SetConnectTimeout, "[CONFIG:IPC] SetConnectTimeout\n"
dprintf ryu_ldn::ipc::ConfigService::GetPingInterval, "[CONFIG:IPC] GetPingInterval\n"
dprintf ryu_ldn::ipc::ConfigService::SetPingInterval, "[CONFIG:IPC] SetPingInterval\n"
dprintf ryu_ldn::ipc::ConfigService::SaveConfig, "[CONFIG:IPC] SaveConfig\n"
dprintf ryu_ldn::ipc::ConfigService::ReloadConfig, "[CONFIG:IPC] ReloadConfig\n"
dprintf ryu_ldn::ipc::ConfigService::IsServiceActive, "[CONFIG:IPC] IsServiceActive\n"
dprintf ryu_ldn::ipc::ConfigService::IsGameActive, "[CONFIG:IPC] IsGameActive\n"
dprintf ryu_ldn::ipc::ConfigService::GetLdnState, "[CONFIG:IPC] GetLdnState\n"
dprintf ryu_ldn::ipc::ConfigService::GetSessionInfo, "[CONFIG:IPC] GetSessionInfo\n"
dprintf ryu_ldn::ipc::ConfigService::GetLastRtt, "[CONFIG:IPC] GetLastRtt\n"
dprintf ryu_ldn::ipc::ConfigService::ForceReconnect, "[CONFIG:IPC] ForceReconnect\n"
dprintf ryu_ldn::ipc::ConfigService::GetActiveProcessId, "[CONFIG:IPC] GetActiveProcessId\n"
dprintf ryu_ldn::ipc::ConfigService::GetDisableP2p, "[CONFIG:IPC] GetDisableP2p\n"
dprintf ryu_ldn::ipc::ConfigService::SetDisableP2p, "[CONFIG:IPC] SetDisableP2p\n"

echo [CONFIG] ipc: 29 dprintf points\n
