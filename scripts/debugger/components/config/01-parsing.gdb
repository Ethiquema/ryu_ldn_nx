# ==========================================
# Config Component - Parsing
# ==========================================
# Configuration file parsing
# Using dprintf for automatic continue

echo [CONFIG] Loading parsing breakpoints...\n

# Main parsing
dprintf ryu_ldn::config::parse_config_content, "[CONFIG:PARSE] Parsing config content\n"

# Section parsers
dprintf ryu_ldn::config::process_server_key, "[CONFIG:PARSE] Processing server key\n"
dprintf ryu_ldn::config::process_network_key, "[CONFIG:PARSE] Processing network key\n"
dprintf ryu_ldn::config::process_ldn_key, "[CONFIG:PARSE] Processing LDN key\n"
dprintf ryu_ldn::config::process_debug_key, "[CONFIG:PARSE] Processing debug key\n"

# Utility functions
dprintf ryu_ldn::config::parse_bool, "[CONFIG:PARSE] Parsing bool\n"
dprintf ryu_ldn::config::trim_end, "[CONFIG:PARSE] Trimming string\n"
dprintf ryu_ldn::config::safe_strcpy, "[CONFIG:PARSE] Safe string copy\n"

echo [CONFIG] Parsing: 8 dprintf points\n
