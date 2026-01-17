# ==========================================
# Network Component - Connection Management
# ==========================================
# Connect, disconnect, reconnect logic
# Using dprintf for automatic continue

echo [NETWORK] Loading connection management breakpoints...\n

# Connection operations
dprintf ryu_ldn::network::RyuLdnClient::connect, "[NETWORK:CONNECT] Connect initiated\n"
dprintf ryu_ldn::network::RyuLdnClient::disconnect, "[NETWORK:CONNECT] Disconnect initiated\n"
dprintf ryu_ldn::network::RyuLdnClient::try_connect, "[NETWORK:CONNECT] Attempting connection...\n"

# State queries
dprintf ryu_ldn::network::RyuLdnClient::is_connected, "[NETWORK:CONNECT] Connection state queried\n"
dprintf ryu_ldn::network::RyuLdnClient::is_ready, "[NETWORK:CONNECT] Ready state queried\n"
dprintf ryu_ldn::network::RyuLdnClient::is_transitioning, "[NETWORK:CONNECT] Transition state queried\n"

# Reconnection logic
dprintf ryu_ldn::network::RyuLdnClient::start_backoff, "[NETWORK:CONNECT] Backoff started\n"
dprintf ryu_ldn::network::RyuLdnClient::is_backoff_expired, "[NETWORK:CONNECT] Checking backoff expiration\n"
dprintf ryu_ldn::network::RyuLdnClient::is_handshake_timeout, "[NETWORK:CONNECT] Checking handshake timeout\n"

echo [NETWORK] Connection management: 9 dprintf points\n
