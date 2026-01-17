# ==========================================
# LDN Component - Session Handler
# ==========================================
# LDN Session Handler for session state management
# Namespace: ryu_ldn::ldn
# Using dprintf for automatic continue

echo [LDN] Loading session handler breakpoints...\n

# Session handler lifecycle
dprintf ryu_ldn::ldn::LdnSessionHandler::LdnSessionHandler, "[LDN:SESSION] Handler created\n"

# Callback registration
dprintf ryu_ldn::ldn::LdnSessionHandler::set_state_callback, "[LDN:SESSION] State callback registered\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::set_network_updated_callback, "[LDN:SESSION] Network updated callback registered\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::set_scan_result_callback, "[LDN:SESSION] Scan result callback registered\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::set_scan_completed_callback, "[LDN:SESSION] Scan completed callback registered\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::set_disconnected_callback, "[LDN:SESSION] Disconnected callback registered\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::set_error_callback, "[LDN:SESSION] Error callback registered\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::set_rejected_callback, "[LDN:SESSION] Rejected callback registered\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::set_accept_policy_changed_callback, "[LDN:SESSION] Accept policy callback registered\n"

# Packet handlers
dprintf ryu_ldn::ldn::LdnSessionHandler::handle_initialize, "[LDN:SESSION] handle_initialize\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::handle_connected, "[LDN:SESSION] handle_connected\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::handle_sync_network, "[LDN:SESSION] handle_sync_network\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::handle_scan_reply, "[LDN:SESSION] handle_scan_reply\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::handle_scan_reply_end, "[LDN:SESSION] handle_scan_reply_end\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::handle_ping, "[LDN:SESSION] handle_ping\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::handle_disconnect, "[LDN:SESSION] handle_disconnect\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::handle_network_error, "[LDN:SESSION] handle_network_error\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::handle_reject, "[LDN:SESSION] handle_reject\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::handle_reject_reply, "[LDN:SESSION] handle_reject_reply\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::handle_set_accept_policy, "[LDN:SESSION] handle_set_accept_policy\n"

# Actions
dprintf ryu_ldn::ldn::LdnSessionHandler::leave_session, "[LDN:SESSION] leave_session\n"
dprintf ryu_ldn::ldn::LdnSessionHandler::reset, "[LDN:SESSION] reset\n"

echo [LDN] Session Handler: 22 dprintf points\n
