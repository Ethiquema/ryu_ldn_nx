# ==========================================
# BSD Component - Data Transfer
# ==========================================
# Send, receive, read, write operations
# LDN proxy routing points
# Using dprintf for automatic continue

echo [BSD] Loading data transfer breakpoints...\n

# Send operations (Command 10, 11, 24)
dprintf ams::mitm::bsd::BsdMitmService::Send, "[BSD:DATA] Send: fd=%d flags=%d\n", $x3, $x4
dprintf ams::mitm::bsd::BsdMitmService::SendTo, "[BSD:DATA] SendTo: fd=%d flags=%d\n", $x3, $x4
dprintf ams::mitm::bsd::BsdMitmService::Write, "[BSD:DATA] Write: fd=%d\n", $x3

# Receive operations (Command 8, 9, 25)
dprintf ams::mitm::bsd::BsdMitmService::Recv, "[BSD:DATA] Recv: fd=%d flags=%d\n", $x3, $x4
dprintf ams::mitm::bsd::BsdMitmService::RecvFrom, "[BSD:DATA] RecvFrom: fd=%d flags=%d\n", $x3, $x4
dprintf ams::mitm::bsd::BsdMitmService::Read, "[BSD:DATA] Read: fd=%d\n", $x3

# ProxySocketManager - data routing for LDN traffic
dprintf ams::mitm::bsd::ProxySocketManager::RouteIncomingData, "[BSD:DATA] RouteIncomingData: dest_ip=0x%x dest_port=%u\n", $x3, $x4
dprintf ams::mitm::bsd::ProxySocketManager::SendProxyData, "[BSD:DATA] SendProxyData: src=0x%x:%u -> dst=0x%x:%u\n", $x1, $x2, $x3, $x4
dprintf ams::mitm::bsd::ProxySocketManager::SendProxyConnect, "[BSD:DATA] SendProxyConnect: src=0x%x:%u -> dst=0x%x:%u\n", $x1, $x2, $x3, $x4
dprintf ams::mitm::bsd::ProxySocketManager::RouteConnectResponse, "[BSD:DATA] RouteConnectResponse\n"
dprintf ams::mitm::bsd::ProxySocketManager::RouteConnectRequest, "[BSD:DATA] RouteConnectRequest\n"

echo [BSD] Data transfer: 11 dprintf points\n
