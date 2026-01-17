# ==========================================
# Crash Analysis Preset
# ==========================================
# Charge les outils de memory trace pour analyse de crash
# Use: load-preset crash-analysis

echo [PRESET] Loading crash-analysis preset...\n

# Memory trace tools
source /workspace/scripts/debugger/tools/memory-trace/01-allocations.gdb
source /workspace/scripts/debugger/tools/memory-trace/02-crash-detection.gdb
source /workspace/scripts/debugger/tools/memory-trace/03-leak-detection.gdb
source /workspace/scripts/debugger/tools/memory-trace/04-corruption.gdb
source /workspace/scripts/debugger/tools/memory-trace/05-stack-analysis.gdb
source /workspace/scripts/debugger/tools/memory-trace/06-snapshots.gdb
source /workspace/scripts/debugger/tools/memory-trace/07-watchpoints.gdb

# Minimal lifecycle for context
source /workspace/scripts/debugger/components/bsd/01-lifecycle.gdb
source /workspace/scripts/debugger/components/network/01-lifecycle.gdb
source /workspace/scripts/debugger/components/p2p/01-server-lifecycle.gdb

echo [PRESET] Crash-analysis preset loaded\n
echo [PRESET] Tools: allocations, crash-detection, leaks, corruption, stack, snapshots, watchpoints\n
echo [PRESET] Commands: stack-dump, memory-snapshot, memory-leak-report\n
