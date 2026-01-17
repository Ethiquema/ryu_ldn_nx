# ==========================================
# BSD Component - Socket Operations
# ==========================================
# Socket creation and management
# Using dprintf for automatic continue

echo [BSD] Loading socket operations breakpoints...\n

# Socket creation (Command 2, 3, 4)
dprintf ams::mitm::bsd::BsdMitmService::Socket, "[BSD:SOCKET] Socket: domain=%d type=%d protocol=%d\n", $x3, $x4, $x5
dprintf ams::mitm::bsd::BsdMitmService::SocketExempt, "[BSD:SOCKET] SocketExempt: domain=%d type=%d protocol=%d\n", $x3, $x4, $x5
dprintf ams::mitm::bsd::BsdMitmService::Open, "[BSD:SOCKET] Open\n"

# Socket close and duplicate (Command 26, 27)
dprintf ams::mitm::bsd::BsdMitmService::Close, "[BSD:SOCKET] Close: fd=%d\n", $x2
dprintf ams::mitm::bsd::BsdMitmService::DuplicateSocket, "[BSD:SOCKET] DuplicateSocket: fd=%d target_pid=%lu\n", $x3, $x4

# ProxySocketManager - singleton for LDN proxy sockets
dprintf ams::mitm::bsd::ProxySocketManager::CreateProxySocket, "[BSD:SOCKET] CreateProxySocket: fd=%d type=%d protocol=%d\n", $x1, $x2, $x3
dprintf ams::mitm::bsd::ProxySocketManager::GetProxySocket, "[BSD:SOCKET] GetProxySocket: fd=%d\n", $x1
dprintf ams::mitm::bsd::ProxySocketManager::CloseProxySocket, "[BSD:SOCKET] CloseProxySocket: fd=%d\n", $x1
dprintf ams::mitm::bsd::ProxySocketManager::CloseAllProxySockets, "[BSD:SOCKET] CloseAllProxySockets\n"

# EphemeralPortPool - port allocation for proxy sockets
dprintf ams::mitm::bsd::EphemeralPortPool::AllocatePort, "[BSD:SOCKET] AllocatePort: protocol=%d\n", $x1
dprintf ams::mitm::bsd::EphemeralPortPool::AllocateSpecificPort, "[BSD:SOCKET] AllocateSpecificPort: port=%u protocol=%d\n", $x1, $x2
dprintf ams::mitm::bsd::EphemeralPortPool::ReleasePort, "[BSD:SOCKET] ReleasePort: port=%u protocol=%d\n", $x1, $x2

echo [BSD] Socket operations: 12 dprintf points\n
