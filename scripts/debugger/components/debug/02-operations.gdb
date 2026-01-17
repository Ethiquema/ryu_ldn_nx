# ==========================================
# Debug Component - Logging Operations
# ==========================================
# Log writing and output
# Using dprintf for automatic continue

echo [DEBUG] Loading logging operations breakpoints...\n

# Logging
dprintf ryu_ldn::debug::Logger::should_log, "[DEBUG:LOG] should_log\n"
dprintf ryu_ldn::debug::Logger::log, "[DEBUG:LOG] log\n"
dprintf ryu_ldn::debug::Logger::log_v, "[DEBUG:LOG] log_v\n"
dprintf ryu_ldn::debug::Logger::flush, "[DEBUG:LOG] flush\n"
dprintf ryu_ldn::debug::Logger::output_message, "[DEBUG:LOG] output_message\n"

# Buffer operations
dprintf ryu_ldn::debug::LogBuffer::init, "[DEBUG:LOG] LogBuffer init\n"
dprintf ryu_ldn::debug::LogBuffer::add, "[DEBUG:LOG] LogBuffer add\n"
dprintf ryu_ldn::debug::LogBuffer::get_all, "[DEBUG:LOG] LogBuffer get_all\n"
dprintf ryu_ldn::debug::LogBuffer::clear, "[DEBUG:LOG] LogBuffer clear\n"

# Formatting
dprintf ryu_ldn::debug::format_log_message, "[DEBUG:LOG] format_log_message\n"
dprintf ryu_ldn::debug::format_log_message_v, "[DEBUG:LOG] format_log_message_v\n"
dprintf ryu_ldn::debug::get_timestamp, "[DEBUG:LOG] get_timestamp\n"
dprintf ryu_ldn::debug::safe_strcpy, "[DEBUG:LOG] safe_strcpy\n"

echo [DEBUG] Logging operations: 13 dprintf points\n
