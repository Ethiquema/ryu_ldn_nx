# ==========================================
# LDN Component - Proxy Handler
# ==========================================
# LDN Proxy Handler for P2P connection management
# Using dprintf for automatic continue

echo [LDN] Loading proxy handler breakpoints...\n

# Proxy handler lifecycle
dprintf ryu_ldn::ldn::LdnProxyHandler::LdnProxyHandler, "[LDN:PROXY] Handler created\n"
dprintf ryu_ldn::ldn::LdnProxyHandler::~LdnProxyHandler, "[LDN:PROXY] Handler destroyed\n"

# Callback registration
dprintf ryu_ldn::ldn::LdnProxyHandler::set_config_callback, "[LDN:PROXY] Config callback registered\n"
dprintf ryu_ldn::ldn::LdnProxyHandler::set_connect_callback, "[LDN:PROXY] Connect callback registered\n"
dprintf ryu_ldn::ldn::LdnProxyHandler::set_data_callback, "[LDN:PROXY] Data callback registered\n"
dprintf ryu_ldn::ldn::LdnProxyHandler::set_disconnect_callback, "[LDN:PROXY] Disconnect callback registered\n"

# Connection management
dprintf ryu_ldn::ldn::LdnProxyHandler::get_connection_count, "[LDN:PROXY] Getting connection count\n"
dprintf ryu_ldn::ldn::LdnProxyHandler::find_connection, "[LDN:PROXY] Finding connection\n"
dprintf ryu_ldn::ldn::LdnProxyHandler::add_connection, "[LDN:PROXY] Adding connection\n"
dprintf ryu_ldn::ldn::LdnProxyHandler::remove_connection, "[LDN:PROXY] Removing connection\n"
dprintf ryu_ldn::ldn::LdnProxyHandler::clear_connections, "[LDN:PROXY] Clearing all connections\n"

# Packet handlers
dprintf ryu_ldn::ldn::LdnProxyHandler::handle_proxy_config, "[LDN:PROXY] Handling proxy config\n"
dprintf ryu_ldn::ldn::LdnProxyHandler::handle_proxy_connect, "[LDN:PROXY] Handling proxy connect\n"
dprintf ryu_ldn::ldn::LdnProxyHandler::handle_proxy_connect_reply, "[LDN:PROXY] Handling proxy connect reply\n"
dprintf ryu_ldn::ldn::LdnProxyHandler::handle_proxy_data, "[LDN:PROXY] Handling proxy data\n"
dprintf ryu_ldn::ldn::LdnProxyHandler::handle_proxy_disconnect, "[LDN:PROXY] Handling proxy disconnect\n"

# State queries
dprintf ryu_ldn::ldn::LdnProxyHandler::is_connected, "[LDN:PROXY] Querying connection state\n"

echo [LDN] Proxy Handler: 17 dprintf points\n
