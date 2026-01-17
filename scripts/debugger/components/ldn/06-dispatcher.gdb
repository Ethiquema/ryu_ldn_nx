# ==========================================
# LDN Component - Packet Dispatcher
# ==========================================
# LDN Packet Dispatcher for routing and packet handling
# Using dprintf for automatic continue

echo [LDN] Loading packet dispatcher breakpoints...\n

# Dispatcher lifecycle
dprintf ryu_ldn::ldn::PacketDispatcher::PacketDispatcher, "[LDN:DISPATCHER] Dispatcher created\n"
dprintf ryu_ldn::ldn::PacketDispatcher::~PacketDispatcher, "[LDN:DISPATCHER] Dispatcher destroyed\n"

# Initialization
dprintf ryu_ldn::ldn::PacketDispatcher::Initialize, "[LDN:DISPATCHER] Initializing dispatcher\n"
dprintf ryu_ldn::ldn::PacketDispatcher::Shutdown, "[LDN:DISPATCHER] Shutting down dispatcher\n"

# Packet routing
dprintf ryu_ldn::ldn::PacketDispatcher::DispatchPacket, "[LDN:DISPATCHER] Dispatching packet\n"
dprintf ryu_ldn::ldn::PacketDispatcher::RoutePacket, "[LDN:DISPATCHER] Routing packet\n"

# Handler registration
dprintf ryu_ldn::ldn::PacketDispatcher::RegisterHandler, "[LDN:DISPATCHER] Registering handler\n"
dprintf ryu_ldn::ldn::PacketDispatcher::UnregisterHandler, "[LDN:DISPATCHER] Unregistering handler\n"

# Protocol handlers
dprintf ryu_ldn::ldn::PacketDispatcher::HandleLdnPacket, "[LDN:DISPATCHER] Handling LDN packet\n"
dprintf ryu_ldn::ldn::PacketDispatcher::HandleProxyPacket, "[LDN:DISPATCHER] Handling proxy packet\n"

# Queue management
dprintf ryu_ldn::ldn::PacketDispatcher::EnqueuePacket, "[LDN:DISPATCHER] Enqueueing packet\n"
dprintf ryu_ldn::ldn::PacketDispatcher::DequeuePacket, "[LDN:DISPATCHER] Dequeueing packet\n"
dprintf ryu_ldn::ldn::PacketDispatcher::ProcessQueue, "[LDN:DISPATCHER] Processing packet queue\n"
dprintf ryu_ldn::ldn::PacketDispatcher::ClearQueue, "[LDN:DISPATCHER] Clearing packet queue\n"

# Statistics
dprintf ryu_ldn::ldn::PacketDispatcher::GetPacketCount, "[LDN:DISPATCHER] Getting packet count\n"
dprintf ryu_ldn::ldn::PacketDispatcher::GetQueueSize, "[LDN:DISPATCHER] Getting queue size\n"

echo [LDN] Packet Dispatcher: 16 dprintf points\n
