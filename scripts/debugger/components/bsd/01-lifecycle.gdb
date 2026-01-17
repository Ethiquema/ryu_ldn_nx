# ==========================================
# BSD Component - Lifecycle
# ==========================================
# Constructor, destructor, and service lifecycle
# Using dprintf for automatic continue

echo [BSD] Loading lifecycle breakpoints...\n

# Service lifecycle - BsdMitmService class
# Namespace: ams::mitm::bsd
dprintf ams::mitm::bsd::BsdMitmService::BsdMitmService, "[BSD:LIFECYCLE] Constructor: program_id=0x%lx, pid=%lu\n", $x2, $x1
dprintf ams::mitm::bsd::BsdMitmService::~BsdMitmService, "[BSD:LIFECYCLE] Destructor\n"
dprintf ams::mitm::bsd::BsdMitmService::ShouldMitm, "[BSD:LIFECYCLE] ShouldMitm: program_id=0x%lx\n", $x1

# Client registration (Command 0)
dprintf ams::mitm::bsd::BsdMitmService::RegisterClient, "[BSD:LIFECYCLE] RegisterClient: config_size=%u\n", $x2
# Monitoring (Command 1)
dprintf ams::mitm::bsd::BsdMitmService::StartMonitoring, "[BSD:LIFECYCLE] StartMonitoring: pid=%lu\n", $x2

echo [BSD] Lifecycle: 5 dprintf points\n
