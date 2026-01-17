# ==========================================
# Network Component - Packet Processing
# ==========================================
# Packet handling and processing
# Using dprintf for automatic continue

echo [NETWORK] Loading packet processing breakpoints...\n

# Main update loop
dprintf ryu_ldn::network::RyuLdnClient::update, "[NETWORK:PACKET] Update tick\n"

# Packet operations
dprintf ryu_ldn::network::RyuLdnClient::process_packets, "[NETWORK:PACKET] Processing incoming packets\n"
dprintf ryu_ldn::network::RyuLdnClient::handle_packet, "[NETWORK:PACKET] Handling packet\n"
dprintf ryu_ldn::network::RyuLdnClient::process_handshake_response, "[NETWORK:PACKET] Processing handshake response\n"

# MAC address
dprintf ryu_ldn::network::RyuLdnClient::generate_mac_address, "[NETWORK:PACKET] Generating MAC address\n"

echo [NETWORK] Packet processing: 5 dprintf points\n
