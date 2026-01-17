# ==========================================
# Minimal Preset - Lifecycle Only
# ==========================================
# Charge uniquement les fichiers 01-lifecycle.gdb de chaque composant
# Use: load-preset minimal

echo [PRESET] Loading minimal preset (lifecycle only)...\n

# BSD lifecycle
source /workspace/scripts/debugger/components/bsd/01-lifecycle.gdb

# LDN lifecycle
source /workspace/scripts/debugger/components/ldn/01-lifecycle.gdb

# Network lifecycle
source /workspace/scripts/debugger/components/network/01-lifecycle.gdb

# P2P lifecycle
source /workspace/scripts/debugger/components/p2p/01-server-lifecycle.gdb

echo [PRESET] Minimal preset loaded\n
