# ==========================================
# Network Component - Lifecycle
# ==========================================
# Constructor, destructor, initialization
# Using dprintf for automatic continue

echo [NETWORK] Loading lifecycle breakpoints...\n

# Client lifecycle
dprintf ryu_ldn::network::RyuLdnClient::RyuLdnClient, "[NETWORK:LIFECYCLE] Client constructor\n"
dprintf ryu_ldn::network::RyuLdnClient::~RyuLdnClient, "[NETWORK:LIFECYCLE] Client destructor\n"

# Configuration
dprintf ryu_ldn::network::RyuLdnClient::set_config, "[NETWORK:LIFECYCLE] Configuration set\n"
dprintf ryu_ldn::network::RyuLdnClient::set_state_callback, "[NETWORK:LIFECYCLE] State callback registered\n"
dprintf ryu_ldn::network::RyuLdnClient::set_packet_callback, "[NETWORK:LIFECYCLE] Packet callback registered\n"

echo [NETWORK] Lifecycle: 5 dprintf points\n
