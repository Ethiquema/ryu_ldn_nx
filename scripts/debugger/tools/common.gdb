# ==========================================
# Common Helper Commands (no app breakpoints)
# ==========================================

# Basic GDB settings
set breakpoint pending on
set pagination off
set confirm off
set auto-load safe-path /

# Memory diagnostics are NOT auto-loaded for performance
# Load them manually with: load-memory-tools
# Or source specific modules:
#   source /workspace/scripts/debugger/tools/memory-trace/01-allocations.gdb
#   source /workspace/scripts/debugger/tools/memory-trace/02-crash-detection.gdb
# etc.

define load-memory-tools
    echo \n
    echo [MEMORY] Loading memory diagnostic tools...\n
    source /workspace/scripts/debugger/tools/memory-trace/01-allocations.gdb
    source /workspace/scripts/debugger/tools/memory-trace/02-crash-detection.gdb
    source /workspace/scripts/debugger/tools/memory-trace/03-leak-detection.gdb
    source /workspace/scripts/debugger/tools/memory-trace/04-corruption.gdb
    source /workspace/scripts/debugger/tools/memory-trace/05-stack-analysis.gdb
    source /workspace/scripts/debugger/tools/memory-trace/06-snapshots.gdb
    source /workspace/scripts/debugger/tools/memory-trace/07-watchpoints.gdb
    echo [MEMORY] All tools loaded\n
    echo \n
end

document load-memory-tools
Load all memory diagnostic tools (allocations, crash detection, leaks, etc.)
This is not loaded by default for performance - call this command when needed.
end

# Helper: List all breakpoints
define bpl
    info breakpoints
end
document bpl
List all breakpoints
end

# Helper: Delete all breakpoints
define bpd
    delete breakpoints
end
document bpd
Delete all breakpoints
end

# Helper: Continue with status
define c-status
    echo \nContinuing execution...\n
    continue
end
document c-status
Continue execution with status message
end

# Stack helpers (no automatic breakpoints)
define stack-dump
    echo \n
    echo ==========================================\n
    echo STACK DUMP\n
    echo ==========================================\n
    echo \n
    echo [STACK] Stack pointer:\n
    print /x $sp
    echo \n
    echo [STACK] Frame pointer:\n
    print /x $x29
    echo \n
    echo [STACK] Link register:\n
    print /x $x30
    echo \n
    echo [STACK] Stack dump (256 bytes):\n
    x/64xg $sp
    echo \n
    echo [STACK] Backtrace:\n
    backtrace full
    echo \n
    echo ==========================================\n
end
document stack-dump
Dump complete stack information including registers and memory
end

# Stack alignment check
define stack-check
    set $sp_aligned = ($sp & 0xf)
    if $sp_aligned != 0
        echo \n
        echo [STACK:WARNING] Stack pointer not 16-byte aligned!\n
        printf "SP = 0x%lx (alignment = %d)\n", $sp, $sp_aligned
        echo \n
    end
end
document stack-check
Check stack pointer alignment and validity
end

# Recursion checker
define recursion-check
    echo [STACK] Checking for recursion...\n
    backtrace
end
document recursion-check
Display backtrace to check for recursion
end

# Memory usage summary (non-intrusive)
define memory-usage
    echo \n
    echo [MEMORY-USAGE] Current memory usage (mappings):\n
    info proc mappings
    echo \n
end
document memory-usage
Display current memory mappings/usage
end

# Lightweight snapshot (no breakpoints set)
define memory-snapshot-lite
    echo \n
    echo ==========================================\n
    echo MEMORY SNAPSHOT (lite)\n
    echo ==========================================\n
    echo \n
    echo [SNAPSHOT] Register state:\n
    info registers
    echo \n
    echo [SNAPSHOT] Backtrace:\n
    backtrace
    echo \n
    echo ==========================================\n
end
document memory-snapshot-lite
Capture a lightweight snapshot (registers + backtrace)
end

# ==========================================
# ryu_ldn_nx Debugger Help System
# ==========================================

# Dynamic component loader
define load
    if $argc == 0
        echo \n
        echo [LOAD] Usage: load <component> [subcomponent]\n
        echo \n
        echo Available components:\n
        echo   bsd config debug ldn network p2p\n
        echo \n
        echo Examples:\n
        echo   load bsd               - Load all BSD component files\n
        echo   load bsd lifecycle     - Load BSD lifecycle only\n
        echo   load ldn               - Load all LDN component files\n
        echo \n
        echo Use 'list-components' to see all available files\n
        echo \n
    else
        if $argc == 1
            # Load all files from component
            shell find /workspace/scripts/debugger/components/$arg0 -name "*.gdb" -exec echo "source {}" \; | sed 's/source/echo Loading:/' | /opt/devkitpro/devkitA64/bin/aarch64-none-elf-gdb --batch -x -
            shell find /workspace/scripts/debugger/components/$arg0 -name "*.gdb" -exec echo "source {}" \; | /opt/devkitpro/devkitA64/bin/aarch64-none-elf-gdb --batch -x - 2>&1 | grep -v "^$"
            source /workspace/scripts/debugger/components/$arg0/*.gdb
            printf "\n[LOAD] ✓ Loaded component: %s (all files)\n\n", "$arg0"
        else
            # Load specific subcomponent
            shell ls /workspace/scripts/debugger/components/$arg0/*$arg1*.gdb 2>/dev/null | head -1 | xargs -I {} echo "Loading: {}"
            shell ls /workspace/scripts/debugger/components/$arg0/*$arg1*.gdb 2>/dev/null | head -1 | xargs -I {} echo "source {}" > /tmp/load_component.gdb
            source /tmp/load_component.gdb
            printf "\n[LOAD] ✓ Loaded: %s/%s\n\n", "$arg0", "$arg1"
        end
    end
end
document load
Dynamically load debugger components or subcomponents.
Usage: load <component> [subcomponent]
Examples:
  load bsd              - Load all BSD files
  load ldn lifecycle    - Load LDN lifecycle only
  load network packet   - Load network packet handling
end

# Preset loader
define load-preset
    if $argc == 0
        echo \n
        echo [LOAD-PRESET] Usage: load-preset <preset-name>\n
        echo \n
        echo Use 'list-presets' to see available presets\n
        echo \n
    else
        set $preset_file = "/workspace/scripts/debugger/presets/$arg0.gdb"
        shell test -f /workspace/scripts/debugger/presets/$arg0.gdb && echo "[LOAD-PRESET] Loading preset: $arg0" || echo "[LOAD-PRESET] ✗ Preset not found: $arg0"
        source /workspace/scripts/debugger/presets/$arg0.gdb
        printf "\n[LOAD-PRESET] ✓ Loaded preset: %s\n\n", "$arg0"
    end
end
document load-preset
Load a debug preset configuration.
Usage: load-preset <preset-name>
Examples:
  load-preset quick
  load-preset network-focus
  load-preset crash-debug
end

# List available components
define list-components
    echo \n
    echo ==========================================\n
    echo AVAILABLE DEBUG COMPONENTS\n
    echo ==========================================\n
    echo \n
    shell echo "[BSD] BSD socket MITM service:" && ls -1 /workspace/scripts/debugger/components/bsd/*.gdb | sed 's|.*/||' | sed 's/\.gdb$//' | sed 's/^/  - /'
    echo \n
    shell echo "[CONFIG] Configuration management:" && ls -1 /workspace/scripts/debugger/components/config/*.gdb | sed 's|.*/||' | sed 's/\.gdb$//' | sed 's/^/  - /'
    echo \n
    shell echo "[DEBUG] Logging utilities:" && ls -1 /workspace/scripts/debugger/components/debug/*.gdb | sed 's|.*/||' | sed 's/\.gdb$//' | sed 's/^/  - /'
    echo \n
    shell echo "[LDN] LDN service handlers:" && ls -1 /workspace/scripts/debugger/components/ldn/*.gdb | sed 's|.*/||' | sed 's/\.gdb$//' | sed 's/^/  - /'
    echo \n
    shell echo "[NETWORK] Network client:" && ls -1 /workspace/scripts/debugger/components/network/*.gdb | sed 's|.*/||' | sed 's/\.gdb$//' | sed 's/^/  - /'
    echo \n
    shell echo "[P2P] P2P proxy:" && ls -1 /workspace/scripts/debugger/components/p2p/*.gdb | sed 's|.*/||' | sed 's/\.gdb$//' | sed 's/^/  - /'
    echo \n
    echo ==========================================\n
    echo \n
end
document list-components
List all available debug components and subcomponents
end

# List available presets
define list-presets
    echo \n
    echo ==========================================\n
    echo AVAILABLE DEBUG PRESETS\n
    echo ==========================================\n
    echo \n
    shell ls /workspace/scripts/debugger/presets/*.gdb 2>/dev/null | sed 's|.*/||' | sed 's/\.gdb$//' | sed 's/^/  - /' || echo "  (No presets available)"
    echo \n
    echo ==========================================\n
    echo \n
end
document list-presets
List all available debug presets
end

define ryu-help
    echo \n
    echo ==========================================\n
    echo ryu_ldn_nx GDB DEBUGGER HELP\n
    echo ==========================================\n
    echo \n
    echo [DYNAMIC LOADING] Load components on demand:\n
    echo   load <component>             : Load all files from a component\n
    echo   load <component> <sub>       : Load specific subcomponent\n
    echo   load-preset <name>           : Load a preset configuration\n
    echo   list-components              : List all available components\n
    echo   list-presets                 : List all available presets\n
    echo \n
    echo   Examples:\n
    echo     load bsd                   - Load all BSD files\n
    echo     load ldn lifecycle         - Load LDN lifecycle only\n
    echo     load-preset quick          - Load quick preset\n
    echo \n
    echo [MEMORY TOOLS] Memory diagnostics commands:\n
    echo   memory-snapshot      : Capture full memory state\n
    echo   memory-snapshot-lite : Lightweight snapshot (registers + backtrace)\n
    echo   memory-leak-report   : Generate memory leak analysis\n
    echo   memory-usage         : Display memory mappings\n
    echo   stack-dump           : Dump complete stack information\n
    echo   stack-check          : Check stack alignment\n
    echo   recursion-check      : Display backtrace for recursion detection\n
    echo \n
    echo [WATCHPOINTS] State monitoring commands:\n
    echo   watch-client-state   : Watch P2P client state changes\n
    echo   watch-network-state  : Watch network connection state\n
    echo   watch-connection     : Watch connection establishment\n
    echo   watch-cond <expr>    : Set conditional watchpoint\n
    echo \n
    echo [BREAKPOINT HELPERS] Breakpoint management:\n
    echo   bpl                  : List all breakpoints\n
    echo   bpd                  : Delete all breakpoints\n
    echo   info breakpoints     : Detailed breakpoint info\n
    echo \n
    echo [NAVIGATION] Execution control:\n
    echo   c / continue         : Continue execution\n
    echo   c-status             : Continue with status message\n
    echo   s / step             : Step into\n
    echo   n / next             : Step over\n
    echo   finish               : Step out\n
    echo \n
    echo [INFO] Session information:\n
    echo   info threads         : List all threads\n
    echo   info registers       : Show all registers\n
    echo   backtrace / bt       : Show call stack\n
    echo \n
    echo [AUTO-DEBUG] Automatic features enabled:\n
    echo   - Auto-continue observer (hook-stop)\n
    echo   - Crash detection & memory diagnostics\n
    echo   - Allocation tracking\n
    echo   - Memory corruption detection\n
    echo \n
    echo Type 'help <command>' for detailed help on any command\n
    echo ==========================================\n
    echo \n
end
document ryu-help
Display ryu_ldn_nx debugger help and available commands
end

