# ==========================================
# Network Component - State Machine
# ==========================================
# Connection state transitions
# Using dprintf for automatic continue

echo [NETWORK] Loading state machine breakpoints...\n

# State transitions
dprintf ryu_ldn::network::ConnectionStateMachine::transition_to, "[NETWORK:STATE] State transition\n"

# State queries
dprintf ryu_ldn::network::ConnectionStateMachine::is_connected, "[NETWORK:STATE] Connection state check\n"
dprintf ryu_ldn::network::ConnectionStateMachine::is_transitioning, "[NETWORK:STATE] Transition state check\n"
dprintf ryu_ldn::network::ConnectionStateMachine::is_valid_transition, "[NETWORK:STATE] Validating transition\n"

# Configuration
dprintf ryu_ldn::network::ConnectionStateMachine::set_state_change_callback, "[NETWORK:STATE] State callback set\n"
dprintf ryu_ldn::network::ConnectionStateMachine::force_state, "[NETWORK:STATE] Forcing state change\n"

echo [NETWORK] State machine: 6 dprintf points\n
