# ==========================================
# LDN Component - Operations
# ==========================================
# ICommunicationService - Network operations
# Using dprintf for automatic continue

echo [LDN] Loading operations breakpoints...\n

# Connection management
dprintf ams::mitm::ldn::LdnConfigService::ForceReconnect, "[LDN:OPS] ForceReconnect\n"
dprintf ams::mitm::ldn::LdnConfigService::GetLastRtt, "[LDN:OPS] GetLastRtt\n"

# ICommunicationService - Query operations
dprintf ams::mitm::ldn::ICommunicationService::GetState, "[LDN:OPS] GetState\n"
dprintf ams::mitm::ldn::ICommunicationService::GetNetworkInfo, "[LDN:OPS] GetNetworkInfo\n"
dprintf ams::mitm::ldn::ICommunicationService::GetIpv4Address, "[LDN:OPS] GetIpv4Address\n"
dprintf ams::mitm::ldn::ICommunicationService::GetDisconnectReason, "[LDN:OPS] GetDisconnectReason\n"
dprintf ams::mitm::ldn::ICommunicationService::GetSecurityParameter, "[LDN:OPS] GetSecurityParameter\n"
dprintf ams::mitm::ldn::ICommunicationService::GetNetworkConfig, "[LDN:OPS] GetNetworkConfig\n"
dprintf ams::mitm::ldn::ICommunicationService::AttachStateChangeEvent, "[LDN:OPS] AttachStateChangeEvent\n"
dprintf ams::mitm::ldn::ICommunicationService::GetNetworkInfoLatestUpdate, "[LDN:OPS] GetNetworkInfoLatestUpdate\n"

# Scan operations
dprintf ams::mitm::ldn::ICommunicationService::Scan, "[LDN:OPS] Scan\n"
dprintf ams::mitm::ldn::ICommunicationService::ScanPrivate, "[LDN:OPS] ScanPrivate\n"

# Access Point operations
dprintf ams::mitm::ldn::ICommunicationService::OpenAccessPoint, "[LDN:OPS] OpenAccessPoint\n"
dprintf ams::mitm::ldn::ICommunicationService::CloseAccessPoint, "[LDN:OPS] CloseAccessPoint\n"
dprintf ams::mitm::ldn::ICommunicationService::CreateNetwork, "[LDN:OPS] CreateNetwork\n"
dprintf ams::mitm::ldn::ICommunicationService::CreateNetworkPrivate, "[LDN:OPS] CreateNetworkPrivate\n"
dprintf ams::mitm::ldn::ICommunicationService::DestroyNetwork, "[LDN:OPS] DestroyNetwork\n"
dprintf ams::mitm::ldn::ICommunicationService::SetAdvertiseData, "[LDN:OPS] SetAdvertiseData\n"
dprintf ams::mitm::ldn::ICommunicationService::SetStationAcceptPolicy, "[LDN:OPS] SetStationAcceptPolicy\n"

# Station operations
dprintf ams::mitm::ldn::ICommunicationService::OpenStation, "[LDN:OPS] OpenStation\n"
dprintf ams::mitm::ldn::ICommunicationService::CloseStation, "[LDN:OPS] CloseStation\n"
dprintf ams::mitm::ldn::ICommunicationService::Connect, "[LDN:OPS] Connect\n"
dprintf ams::mitm::ldn::ICommunicationService::ConnectPrivate, "[LDN:OPS] ConnectPrivate\n"
dprintf ams::mitm::ldn::ICommunicationService::Disconnect, "[LDN:OPS] Disconnect\n"

# Other operations
dprintf ams::mitm::ldn::ICommunicationService::Reject, "[LDN:OPS] Reject: nodeId=%u\n", $x1

echo [LDN] Operations: 26 dprintf points\n
