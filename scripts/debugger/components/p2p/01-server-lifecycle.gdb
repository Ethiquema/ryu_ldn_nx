# ==========================================
# P2P Component - Server Lifecycle
# ==========================================
# Proxy server initialization and management
# Using dprintf for automatic continue

echo [P2P] Loading server lifecycle breakpoints...\n

# Server lifecycle
dprintf ams::mitm::p2p::P2pProxyServer::Start, "[P2P:SERVER] Server starting\n"
dprintf ams::mitm::p2p::P2pProxyServer::Stop, "[P2P:SERVER] Server stopping\n"
dprintf ams::mitm::p2p::P2pProxyServer::IsRunning, "[P2P:SERVER] Server state queried\n"
dprintf ams::mitm::p2p::P2pProxyServer::Configure, "[P2P:SERVER] Server configuration updated\n"

# Accept loop
dprintf ams::mitm::p2p::P2pProxyServer::AcceptLoop, "[P2P:SERVER] Accept loop running\n"
dprintf ams::mitm::p2p::AcceptThreadEntry, "[P2P:SERVER] Accept thread started\n"

echo [P2P] Server lifecycle: 6 dprintf points\n
