# ==========================================
# Debug Component - Logger Lifecycle
# ==========================================
# Logger initialization and management
# Using dprintf for automatic continue

echo [DEBUG] Loading logger lifecycle breakpoints...\n

# Logger initialization
dprintf ryu_ldn::debug::Logger::init, "[DEBUG:LOGGER] Logger initialized\n"

# File operations
dprintf ryu_ldn::debug::Logger::open_file, "[DEBUG:LOGGER] Log file opened\n"
dprintf ryu_ldn::debug::Logger::close_file, "[DEBUG:LOGGER] Log file closed\n"

echo [DEBUG] Logger lifecycle: 4 dprintf points\n
