# ==========================================
# P2P Component - Proxy Messages
# ==========================================
# Proxy protocol message handlers
# Using dprintf for automatic continue

echo [P2P] Loading proxy message breakpoints...\n

# Client message sending
dprintf ams::mitm::p2p::P2pProxyClient::SendProxyData, "[P2P:MSG] Client sending proxy data\n"
dprintf ams::mitm::p2p::P2pProxyClient::SendProxyConnect, "[P2P:MSG] Client sending proxy connect\n"
dprintf ams::mitm::p2p::P2pProxyClient::SendProxyConnectReply, "[P2P:MSG] Client sending connect reply\n"
dprintf ams::mitm::p2p::P2pProxyClient::SendProxyDisconnect, "[P2P:MSG] Client sending disconnect\n"

# Client message handling
dprintf ams::mitm::p2p::P2pProxyClient::HandleProxyConfig, "[P2P:MSG] Client handling proxy config\n"
dprintf ams::mitm::p2p::P2pProxyClient::HandleProxyData, "[P2P:MSG] Client handling proxy data\n"
dprintf ams::mitm::p2p::P2pProxyClient::HandleProxyConnect, "[P2P:MSG] Client handling proxy connect\n"
dprintf ams::mitm::p2p::P2pProxyClient::HandleProxyConnectReply, "[P2P:MSG] Client handling connect reply\n"
dprintf ams::mitm::p2p::P2pProxyClient::HandleProxyDisconnect, "[P2P:MSG] Client handling disconnect\n"

echo [P2P] Proxy messages: 9 dprintf points\n
