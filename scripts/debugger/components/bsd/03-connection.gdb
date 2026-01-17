# ==========================================
# BSD Component - Connection Management
# ==========================================
# Bind, connect, accept, listen, shutdown
# Using dprintf for automatic continue

echo [BSD] Loading connection management breakpoints...\n

# Binding (Command 13) - LDN detection point
dprintf ams::mitm::bsd::BsdMitmService::Bind, "[BSD:CONNECT] Bind: fd=%d\n", $x2

# Connecting (Command 14) - LDN detection point
dprintf ams::mitm::bsd::BsdMitmService::Connect, "[BSD:CONNECT] Connect: fd=%d\n", $x2

# Accepting connections (Command 12)
dprintf ams::mitm::bsd::BsdMitmService::Accept, "[BSD:CONNECT] Accept: fd=%d\n", $x3

# Listening (Command 18)
dprintf ams::mitm::bsd::BsdMitmService::Listen, "[BSD:CONNECT] Listen: fd=%d backlog=%d\n", $x2, $x3

# Shutdown operations (Command 22, 23)
dprintf ams::mitm::bsd::BsdMitmService::Shutdown, "[BSD:CONNECT] Shutdown: fd=%d how=%d\n", $x2, $x3
dprintf ams::mitm::bsd::BsdMitmService::ShutdownAllSockets, "[BSD:CONNECT] ShutdownAllSockets: pid=%lu how=%d\n", $x2, $x3

# Address operations (Command 15, 16)
dprintf ams::mitm::bsd::BsdMitmService::GetPeerName, "[BSD:CONNECT] GetPeerName: fd=%d\n", $x2
dprintf ams::mitm::bsd::BsdMitmService::GetSockName, "[BSD:CONNECT] GetSockName: fd=%d\n", $x2

echo [BSD] Connection management: 8 dprintf points\n
