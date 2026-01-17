# ==========================================
# LDN Focus Preset
# ==========================================
# Charge tous les composants LDN
# Use: load-preset ldn-focus

echo [PRESET] Loading ldn-focus preset...\n

# LDN component (all 7 files)
source /workspace/scripts/debugger/components/ldn/01-lifecycle.gdb
source /workspace/scripts/debugger/components/ldn/02-config.gdb
source /workspace/scripts/debugger/components/ldn/03-operations.gdb
source /workspace/scripts/debugger/components/ldn/04-proxy-handler.gdb
source /workspace/scripts/debugger/components/ldn/05-session-handler.gdb
source /workspace/scripts/debugger/components/ldn/06-dispatcher.gdb
source /workspace/scripts/debugger/components/ldn/07-state-communication.gdb

echo [PRESET] LDN-focus preset loaded (7 files)\n
echo [PRESET] Tracking: lifecycle, config, proxy, session, dispatch, state\n
