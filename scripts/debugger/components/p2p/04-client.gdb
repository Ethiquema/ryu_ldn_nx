# ==========================================
# P2P Component - Client Operations
# ==========================================
# P2P proxy client functionality
# Using dprintf for automatic continue

echo [P2P] Loading client operations breakpoints...\n

# Client lifecycle
dprintf ams::mitm::p2p::P2pProxyClient::Connect, "[P2P:CLIENT] Client connecting\n"
dprintf ams::mitm::p2p::P2pProxyClient::Disconnect, "[P2P:CLIENT] Client disconnecting\n"
dprintf ams::mitm::p2p::P2pProxyClient::IsConnected, "[P2P:CLIENT] Connection state queried\n"
dprintf ams::mitm::p2p::P2pProxyClient::IsReady, "[P2P:CLIENT] Ready state queried\n"
dprintf ams::mitm::p2p::ClientRecvThreadEntry, "[P2P:CLIENT] Receive thread started\n"

# Authentication
dprintf ams::mitm::p2p::P2pProxyClient::PerformAuth, "[P2P:CLIENT] Performing authentication\n"
dprintf ams::mitm::p2p::P2pProxyClient::EnsureProxyReady, "[P2P:CLIENT] Ensuring proxy ready\n"

# Data operations
dprintf ams::mitm::p2p::P2pProxyClient::Send, "[P2P:CLIENT] Sending data\n"
dprintf ams::mitm::p2p::P2pProxyClient::ReceiveLoop, "[P2P:CLIENT] Receive loop running\n"
dprintf ams::mitm::p2p::P2pProxyClient::ProcessData, "[P2P:CLIENT] Processing received data\n"

echo [P2P] Client operations: 10 dprintf points\n
