# ==========================================
# P2P Component - Data Routing
# ==========================================
# Message routing and proxy operations
# Using dprintf for automatic continue

echo [P2P] Loading data routing breakpoints...\n

# Server routing
dprintf ams::mitm::p2p::P2pProxyServer::RouteMessage, "[P2P:ROUTE] Message routed\n"
dprintf ams::mitm::p2p::P2pProxyServer::HandleProxyData, "[P2P:ROUTE] Proxy data handled (server)\n"
dprintf ams::mitm::p2p::P2pProxyServer::HandleProxyConnect, "[P2P:ROUTE] Proxy connect handled (server)\n"
dprintf ams::mitm::p2p::P2pProxyServer::HandleProxyConnectReply, "[P2P:ROUTE] Proxy connect reply handled (server)\n"
dprintf ams::mitm::p2p::P2pProxyServer::HandleProxyDisconnect, "[P2P:ROUTE] Proxy disconnect handled (server)\n"

# Session data handling
dprintf ams::mitm::p2p::P2pProxySession::Send, "[P2P:ROUTE] Session send\n"
dprintf ams::mitm::p2p::P2pProxySession::ProcessData, "[P2P:ROUTE] Session data processing\n"
dprintf ams::mitm::p2p::P2pProxySession::HandleExternalProxy, "[P2P:ROUTE] External proxy configured\n"
dprintf ams::mitm::p2p::P2pProxySession::HandleProxyData, "[P2P:ROUTE] Proxy data handled (session)\n"
dprintf ams::mitm::p2p::P2pProxySession::HandleProxyConnect, "[P2P:ROUTE] Proxy connect handled (session)\n"
dprintf ams::mitm::p2p::P2pProxySession::HandleProxyConnectReply, "[P2P:ROUTE] Proxy connect reply handled (session)\n"
dprintf ams::mitm::p2p::P2pProxySession::HandleProxyDisconnect, "[P2P:ROUTE] Proxy disconnect handled (session)\n"

echo [P2P] Data routing: 12 dprintf points\n
