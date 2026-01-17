# ==========================================
# LDN Component - Shared State & ICommunication
# ==========================================
# LDN Shared State and ICommunication service
# Using dprintf for automatic continue

echo [LDN] Loading shared state and communication breakpoints...\n

# Shared state lifecycle
dprintf ryu_ldn::ldn::SharedState::SharedState, "[LDN:STATE] Shared state created\n"
dprintf ryu_ldn::ldn::SharedState::~SharedState, "[LDN:STATE] Shared state destroyed\n"

# State management
dprintf ryu_ldn::ldn::SharedState::SetSessionInfo, "[LDN:STATE] Setting session info\n"
dprintf ryu_ldn::ldn::SharedState::GetSessionInfo, "[LDN:STATE] Getting session info\n"
dprintf ryu_ldn::ldn::SharedState::SetNetworkState, "[LDN:STATE] Setting network state\n"
dprintf ryu_ldn::ldn::SharedState::GetNetworkState, "[LDN:STATE] Getting network state\n"

# Member state
dprintf ryu_ldn::ldn::SharedState::UpdateMemberState, "[LDN:STATE] Updating member state\n"
dprintf ryu_ldn::ldn::SharedState::GetMemberState, "[LDN:STATE] Getting member state\n"

# ICommunication service lifecycle
dprintf ryu_ldn::ldn::ICommunicationService::ICommunicationService, "[LDN:COMM] Communication service created\n"
dprintf ryu_ldn::ldn::ICommunicationService::~ICommunicationService, "[LDN:COMM] Communication service destroyed\n"

# Communication operations
dprintf ryu_ldn::ldn::ICommunicationService::CreateNetworkRelay, "[LDN:COMM] Creating network relay\n"
dprintf ryu_ldn::ldn::ICommunicationService::DestroyNetworkRelay, "[LDN:COMM] Destroying network relay\n"
dprintf ryu_ldn::ldn::ICommunicationService::SetProxyConfiguration, "[LDN:COMM] Setting proxy configuration\n"
dprintf ryu_ldn::ldn::ICommunicationService::GetProxyConfiguration, "[LDN:COMM] Getting proxy configuration\n"

# Data transfer
dprintf ryu_ldn::ldn::ICommunicationService::SendData, "[LDN:COMM] Sending data\n"
dprintf ryu_ldn::ldn::ICommunicationService::ReceiveData, "[LDN:COMM] Receiving data\n"

# Status queries
dprintf ryu_ldn::ldn::ICommunicationService::IsConnected, "[LDN:COMM] Checking connection status\n"
dprintf ryu_ldn::ldn::ICommunicationService::GetConnectStatus, "[LDN:COMM] Getting connect status\n"

echo [LDN] Shared State & Communication: 18 dprintf points\n
