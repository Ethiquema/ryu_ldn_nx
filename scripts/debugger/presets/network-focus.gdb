# ==========================================
# Network Focus Preset
# ==========================================
# Charge les composants liés au réseau
# Use: load-preset network-focus

echo [PRESET] Loading network-focus preset...\n

# Network component (all files)
source /workspace/scripts/debugger/components/network/01-lifecycle.gdb
source /workspace/scripts/debugger/components/network/02-connection.gdb
source /workspace/scripts/debugger/components/network/03-packet.gdb
source /workspace/scripts/debugger/components/network/04-state-machine.gdb

# BSD connection & data transfer
source /workspace/scripts/debugger/components/bsd/01-lifecycle.gdb
source /workspace/scripts/debugger/components/bsd/03-connection.gdb
source /workspace/scripts/debugger/components/bsd/04-data-transfer.gdb

# P2P client/server
source /workspace/scripts/debugger/components/p2p/01-server-lifecycle.gdb
source /workspace/scripts/debugger/components/p2p/04-client.gdb
source /workspace/scripts/debugger/components/p2p/05-messages.gdb

echo [PRESET] Network-focus preset loaded\n
echo [PRESET] Tracking: Network, BSD, P2P connections\n
