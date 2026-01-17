#!/bin/bash
# ==========================================
# ryu_ldn_nx Autonomous Debugger
# ==========================================
# Usage: ./debug.sh <IP> [PID]
#
# Fonctionnement autonome:
# 1. Connexion automatique à la Switch
# 2. Détection du process et ASLR
# 3. Chargement des symboles
# 4. Menu interactif de sélection des composants
# 5. Crash handlers toujours actifs
# 6. Auto-continue sur breakpoints, STOP sur crash
#
# ==========================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Local paths (for file listing)
COMPONENTS_DIR="$SCRIPT_DIR/components"
TOOLS_DIR="$SCRIPT_DIR/tools"
PRESETS_DIR="$SCRIPT_DIR/presets"

# Docker/GDB paths (for generated init file)
GDB_COMPONENTS_DIR="/workspace/scripts/debugger/components"
GDB_TOOLS_DIR="/workspace/scripts/debugger/tools"

# Configuration
GDB="/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gdb"
GDB_PORT="22225"
ELF_FILE="/workspace/sysmodule/ryu_ldn_nx.elf"
LOG_DIR="/workspace/debug_logs"
TITLE_ID="4200000000000010"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
DIM='\033[2m'
NC='\033[0m'

# ==========================================
# Arguments
# ==========================================
SWITCH_IP="${1:-}"
EXPLICIT_PID="${2:-}"

if [ -z "$SWITCH_IP" ]; then
    echo -e "${CYAN}${BOLD}╔══════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}${BOLD}║         ryu_ldn_nx Autonomous Debugger                       ║${NC}"
    echo -e "${CYAN}${BOLD}╚══════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo "Usage: $0 <SWITCH_IP> [PID]"
    echo ""
    echo "Examples:"
    echo "  $0 192.168.1.25           # Auto-detect process"
    echo "  $0 192.168.1.25 134       # Attach to specific PID"
    echo ""
    exit 1
fi

# ==========================================
# Validation
# ==========================================
if ! command -v "$GDB" &> /dev/null; then
    echo -e "${RED}ERROR: GDB not found at $GDB${NC}"
    echo "Run inside Docker: docker compose run --rm gdb"
    exit 1
fi

if [ ! -f "$ELF_FILE" ]; then
    echo -e "${RED}ERROR: ELF not found at $ELF_FILE${NC}"
    echo "Build first: make -C sysmodule"
    exit 1
fi

mkdir -p "$LOG_DIR"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

# Create session subdirectory
SESSION_DIR="$LOG_DIR/session_$TIMESTAMP"
mkdir -p "$SESSION_DIR"

# ==========================================
# Banner
# ==========================================
clear
echo ""
echo -e "${CYAN}${BOLD}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}${BOLD}║         ryu_ldn_nx Autonomous Debugger                       ║${NC}"
echo -e "${CYAN}${BOLD}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""

# ==========================================
# Step 1: Connection
# ==========================================
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}[1/5] Connexion à la Switch${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo -e "Target: ${YELLOW}$SWITCH_IP:$GDB_PORT${NC}"

DETECT_FILE="$SESSION_DIR/detect_$TIMESTAMP.txt"
GDB_BATCH=$(mktemp)

cat > "$GDB_BATCH" << 'EOF'
set pagination off
set confirm off
monitor get info
quit
EOF

"$GDB" -batch -q \
    -ex "set tcp connect-timeout 60" \
    -ex "target extended-remote $SWITCH_IP:$GDB_PORT" \
    -x "$GDB_BATCH" 2>&1 | \
    grep -Ev "(warning:.*auto-loading|add-auto-load-safe-path|Reading)" \
    > "$DETECT_FILE" || {
        echo -e "${RED}ERROR: Cannot connect to $SWITCH_IP:$GDB_PORT${NC}"
        rm -f "$GDB_BATCH"
        exit 1
    }

rm -f "$GDB_BATCH"
echo -e "${GREEN}✓ Connecté${NC}"
echo ""

# ==========================================
# Step 2: Process Detection
# ==========================================
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}[2/5] Détection du process${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

if [ -n "$EXPLICIT_PID" ]; then
    PROCESS_ID="$EXPLICIT_PID"
    echo -e "PID fourni: ${YELLOW}$PROCESS_ID${NC}"
else
    PROCESS_ID=$(grep "$TITLE_ID" "$DETECT_FILE" | awk '{print $1}' | head -1)

    if [ -z "$PROCESS_ID" ]; then
        echo -e "${RED}ERROR: ryu_ldn_nx non trouvé (TID: 0x$TITLE_ID)${NC}"
        echo ""
        echo "Processus disponibles:"
        grep -E "^[0-9]+" "$DETECT_FILE" | head -15
        exit 1
    fi
    echo -e "Process trouvé: ${GREEN}PID $PROCESS_ID${NC} (TID: 0x$TITLE_ID)"
fi
echo ""

# ==========================================
# Step 3: ASLR Detection
# ==========================================
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}[3/5] Détection ASLR${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

MAPPING_FILE="$SESSION_DIR/mapping_$TIMESTAMP.txt"
GDB_ATTACH=$(mktemp)

cat > "$GDB_ATTACH" << EOF
set pagination off
set confirm off
attach $PROCESS_ID
monitor get mappings $PROCESS_ID
info registers pc
detach
quit
EOF

"$GDB" -batch -q \
    -ex "set tcp connect-timeout 60" \
    -ex "target extended-remote $SWITCH_IP:$GDB_PORT" \
    -x "$GDB_ATTACH" 2>&1 | \
    grep -Ev "(warning:.*auto-loading|add-auto-load-safe-path|Reading)" \
    > "$MAPPING_FILE"

rm -f "$GDB_ATTACH"

# Try to find base from r-x mapping
BASE_ADDR=$(grep -oE '^0x[0-9a-fA-F]+.*r-x' "$MAPPING_FILE" 2>/dev/null | head -1 | awk '{print $1}' || true)

# Fallback: align PC to 2MB boundary
if [ -z "$BASE_ADDR" ]; then
    PC_RAW=$(grep -E "^pc\s+" "$MAPPING_FILE" 2>/dev/null | awk '{print $2}' || true)
    if [ -n "$PC_RAW" ]; then
        # Use printf to convert hex, more portable
        BASE_ADDR=$(printf "0x%x" $(( (${PC_RAW}) & ~0x1FFFFF )) 2>/dev/null || true)
    fi
fi

# Still no base? Try another pattern
if [ -z "$BASE_ADDR" ]; then
    # Look for any hex address at start of line
    BASE_ADDR=$(grep -oE '^0x[0-9a-fA-F]+' "$MAPPING_FILE" 2>/dev/null | head -1 || true)
fi

if [ -z "$BASE_ADDR" ]; then
    echo -e "${RED}ERROR: Cannot detect base address${NC}"
    echo -e "${YELLOW}Mapping file content:${NC}"
    cat "$MAPPING_FILE" 2>/dev/null | head -20 || true
    exit 1
fi

echo -e "Base address: ${GREEN}$BASE_ADDR${NC}"
echo ""

# ==========================================
# Step 4: Component Selection (Interactive)
# ==========================================
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}[4/5] Sélection des composants à debugger${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

# Count files per component
count_files() {
    local dir="$COMPONENTS_DIR/$1"
    [ -d "$dir" ] && ls -1 "$dir"/*.gdb 2>/dev/null | wc -l || echo "0"
}

echo -e "${YELLOW}Composants disponibles:${NC}"
echo ""
echo -e "  ${BOLD}1.${NC} BSD          - Socket MITM service       ${DIM}($(count_files bsd) fichiers)${NC}"
echo -e "  ${BOLD}2.${NC} LDN          - LDN service handlers      ${DIM}($(count_files ldn) fichiers)${NC}"
echo -e "  ${BOLD}3.${NC} Network      - Network client             ${DIM}($(count_files network) fichiers)${NC}"
echo -e "  ${BOLD}4.${NC} P2P          - P2P proxy server/client   ${DIM}($(count_files p2p) fichiers)${NC}"
echo -e "  ${BOLD}5.${NC} Config       - Configuration management  ${DIM}($(count_files config) fichiers)${NC}"
echo -e "  ${BOLD}6.${NC} Debug        - Logging utilities         ${DIM}($(count_files debug) fichiers)${NC}"
echo ""
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "  ${BOLD}7.${NC} ${GREEN}TOUS${NC}         - Charger tous les composants"
echo -e "  ${BOLD}8.${NC} ${YELLOW}Minimal${NC}      - Lifecycle uniquement (01-*.gdb)"
echo -e "  ${BOLD}9.${NC} ${MAGENTA}Aucun${NC}        - Crash handlers seulement"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo -e "${MAGENTA}${BOLD}Note:${NC} ${MAGENTA}Les crash handlers (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT)${NC}"
echo -e "${MAGENTA}      sont TOUJOURS actifs automatiquement.${NC}"
echo ""
read -p "Sélection (ex: 1 3 4 ou 7 pour tout) [7]: " COMPONENT_CHOICE

# Parse selection
SELECTED_COMPONENTS=()
LOAD_MODE="full"  # full or minimal

if [ -z "$COMPONENT_CHOICE" ]; then
    COMPONENT_CHOICE="7"
fi

for choice in $COMPONENT_CHOICE; do
    case $choice in
        1) SELECTED_COMPONENTS+=("bsd") ;;
        2) SELECTED_COMPONENTS+=("ldn") ;;
        3) SELECTED_COMPONENTS+=("network") ;;
        4) SELECTED_COMPONENTS+=("p2p") ;;
        5) SELECTED_COMPONENTS+=("config") ;;
        6) SELECTED_COMPONENTS+=("debug") ;;
        7) SELECTED_COMPONENTS=("bsd" "ldn" "network" "p2p" "config" "debug") ;;
        8)
            SELECTED_COMPONENTS=("bsd" "ldn" "network" "p2p")
            LOAD_MODE="minimal"
            ;;
        9) SELECTED_COMPONENTS=() ;;
        *) echo -e "${YELLOW}Choix inconnu: $choice${NC}" ;;
    esac
done

echo ""
if [ ${#SELECTED_COMPONENTS[@]} -eq 0 ]; then
    echo -e "${YELLOW}Mode: Crash handlers uniquement${NC}"
else
    echo -e "${GREEN}Composants: ${SELECTED_COMPONENTS[*]}${NC}"
    [ "$LOAD_MODE" = "minimal" ] && echo -e "${YELLOW}Mode: Minimal (lifecycle seulement)${NC}"
fi
echo ""

# ==========================================
# Step 5: Generate GDB Init & Launch
# ==========================================
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}[5/5] Génération et lancement${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

INIT_FILE="$SESSION_DIR/init_$TIMESTAMP.gdb"
LOG_FILE="$SESSION_DIR/session_$TIMESTAMP.log"

# Generate init file
cat > "$INIT_FILE" << EOF
# ==========================================
# ryu_ldn_nx Autonomous Debugger
# Generated: $(date)
# ==========================================
# Target: $SWITCH_IP:$GDB_PORT
# PID: $PROCESS_ID
# Base: $BASE_ADDR
# Components: ${SELECTED_COMPONENTS[*]:-none}
# Mode: $LOAD_MODE
# ==========================================

set architecture aarch64
set pagination off
set confirm off
set height 0
set width 0
set print pretty on
set breakpoint pending on

# Signal handling - Continue signals (don't stop)
handle SIGINT nostop pass
handle SIGTERM nostop pass
handle SIGPIPE nostop pass

# CRASH SIGNALS - STOP execution for analysis
handle SIGSEGV stop nopass
handle SIGBUS stop nopass
handle SIGILL stop nopass
handle SIGFPE stop nopass
handle SIGABRT stop nopass

# Logging
set logging file $LOG_FILE
set logging overwrite off
set logging enabled on

echo 
echo ══════════════════════════════════════════════════════════════
echo  ryu_ldn_nx Autonomous Debugger
echo ══════════════════════════════════════════════════════════════
echo  Target:  $SWITCH_IP:$GDB_PORT
echo  PID:     $PROCESS_ID
echo  Base:    $BASE_ADDR
echo ══════════════════════════════════════════════════════════════
echo

# Connect and attach
target extended-remote $SWITCH_IP:$GDB_PORT
attach $PROCESS_ID

# Load symbols
echo [SYMBOLS] Loading from $ELF_FILE...
add-symbol-file $ELF_FILE $BASE_ADDR
echo [SYMBOLS] Done\n
echo \n

EOF

# Load common tools
if [ -f "$TOOLS_DIR/common.gdb" ]; then
    echo "source $GDB_TOOLS_DIR/common.gdb" >> "$INIT_FILE"
fi

# ==========================================
# CRASH HANDLERS (Always active)
# ==========================================
cat >> "$INIT_FILE" << 'CRASH_EOF'

# ==========================================
# CRASH HANDLERS (Always Active)
# ==========================================
echo [CRASH] Setting up crash handlers...

catch signal SIGSEGV
commands
    silent
    echo 
    echo ╔══════════════════════════════════════════════════════════════════════════════╗
    echo ║                        SEGMENTATION FAULT (SIGSEGV)                          ║
    echo ╚══════════════════════════════════════════════════════════════════════════════╝
    shell date "+Timestamp: %Y-%m-%d %H:%M:%S"
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ CRASH LOCATION                                                               │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    printf "  PC  = 0x%lx", $pc
    printf "  LR  = 0x%lx", $x30
    printf "  SP  = 0x%lx", $sp
    printf "  FP  = 0x%lx", $x29
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ CALL STACK                                                                   │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    backtrace 30
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ FRAME POINTER CHAIN (manual unwind)                                          │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    echo   [FP] -> [next_FP, return_addr]
    x/2xg $x29
    x/2xg *(void**)$x29
    x/2xg *(void**)(*(void**)$x29)
    x/2xg *(void**)(*(void**)(*(void**)$x29))
    x/2xg *(void**)(*(void**)(*(void**)(*(void**)$x29)))
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ REGISTERS                                                                    │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    info registers
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ THREADS                                                                      │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    info threads
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ DISASSEMBLY @ PC                                                             │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    x/16i $pc-32
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ DISASSEMBLY @ LR                                                             │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    x/8i $x30-16
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ STACK MEMORY                                                                 │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    x/32xg $sp
    echo 
    echo ╔══════════════════════════════════════════════════════════════════════════════╗
    echo ║                           SESSION TERMINATED                                 ║
    echo ╚══════════════════════════════════════════════════════════════════════════════╝
    quit
end

catch signal SIGBUS
commands
    silent
    echo 
    echo ╔══════════════════════════════════════════════════════════════════════════════╗
    echo ║                            BUS ERROR (SIGBUS)                                ║
    echo ╚══════════════════════════════════════════════════════════════════════════════╝
    shell date "+Timestamp: %Y-%m-%d %H:%M:%S"
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ CRASH LOCATION                                                               │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    printf "  PC  = 0x%lx", $pc
    printf "  LR  = 0x%lx", $x30
    printf "  SP  = 0x%lx", $sp
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ CALL STACK                                                                   │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    backtrace 30
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ REGISTERS                                                                    │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    info registers
    echo 
    echo ╔══════════════════════════════════════════════════════════════════════════════╗
    echo ║                           SESSION TERMINATED                                 ║
    echo ╚══════════════════════════════════════════════════════════════════════════════╝
    quit
end

catch signal SIGILL
commands
    silent
    echo 
    echo ╔══════════════════════════════════════════════════════════════════════════════╗
    echo ║                       ILLEGAL INSTRUCTION (SIGILL)                           ║
    echo ╚══════════════════════════════════════════════════════════════════════════════╝
    shell date "+Timestamp: %Y-%m-%d %H:%M:%S"
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ CRASH LOCATION                                                               │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    printf "  PC  = 0x%lx", $pc
    printf "  LR  = 0x%lx", $x30
    printf "  SP  = 0x%lx", $sp
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ CALL STACK                                                                   │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    backtrace 30
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ DISASSEMBLY @ PC                                                             │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    x/10i $pc-20
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ REGISTERS                                                                    │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    info registers
    echo 
    echo ╔══════════════════════════════════════════════════════════════════════════════╗
    echo ║                           SESSION TERMINATED                                 ║
    echo ╚══════════════════════════════════════════════════════════════════════════════╝
    quit
end

catch signal SIGFPE
commands
    silent
    echo 
    echo ╔══════════════════════════════════════════════════════════════════════════════╗
    echo ║                     FLOATING POINT EXCEPTION (SIGFPE)                        ║
    echo ╚══════════════════════════════════════════════════════════════════════════════╝
    shell date "+Timestamp: %Y-%m-%d %H:%M:%S"
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ CRASH LOCATION                                                               │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    printf "  PC  = 0x%lx", $pc
    printf "  LR  = 0x%lx", $x30
    printf "  SP  = 0x%lx", $sp
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ CALL STACK                                                                   │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    backtrace 30
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ REGISTERS                                                                    │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    info registers
    echo 
    echo ╔══════════════════════════════════════════════════════════════════════════════╗
    echo ║                           SESSION TERMINATED                                 ║
    echo ╚══════════════════════════════════════════════════════════════════════════════╝
    quit
end

catch signal SIGABRT
commands
    silent
    echo 
    echo ╔══════════════════════════════════════════════════════════════════════════════╗
    echo ║                              ABORT (SIGABRT)                                 ║
    echo ╚══════════════════════════════════════════════════════════════════════════════╝
    shell date "+Timestamp: %Y-%m-%d %H:%M:%S"
    echo 
    echo   Possible causes: assert(), std::terminate(), unhandled exception
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ CRASH LOCATION                                                               │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    printf "  PC  = 0x%lx", $pc
    printf "  LR  = 0x%lx", $x30
    printf "  SP  = 0x%lx", $sp
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ CALL STACK                                                                   │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    backtrace 30
    echo 
    echo ┌──────────────────────────────────────────────────────────────────────────────┐
    echo │ REGISTERS                                                                    │
    echo └──────────────────────────────────────────────────────────────────────────────┘
    info registers
    echo 
    echo ╔══════════════════════════════════════════════════════════════════════════════╗
    echo ║                           SESSION TERMINATED                                 ║
    echo ╚══════════════════════════════════════════════════════════════════════════════╝
    quit
end

echo [CRASH] 5 signal handlers ready (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT)
echo 

CRASH_EOF

# ==========================================
# Load selected components (from existing files)
# ==========================================
echo "# ==========================================" >> "$INIT_FILE"
echo "# COMPONENT BREAKPOINTS" >> "$INIT_FILE"
echo "# ==========================================" >> "$INIT_FILE"

TOTAL_FILES=0

for component in "${SELECTED_COMPONENTS[@]}"; do
    COMP_DIR="$COMPONENTS_DIR/$component"
    GDB_COMP_DIR="$GDB_COMPONENTS_DIR/$component"

    if [ -d "$COMP_DIR" ]; then
        echo "" >> "$INIT_FILE"
        echo "# --- Component: $component ---" >> "$INIT_FILE"

        if [ "$LOAD_MODE" = "minimal" ]; then
            # Only load 01-*.gdb files (lifecycle)
            for gdb_file in "$COMP_DIR"/01-*.gdb; do
                if [ -f "$gdb_file" ]; then
                    # Get just the filename and use Docker path
                    filename=$(basename "$gdb_file")
                    echo "source $GDB_COMP_DIR/$filename" >> "$INIT_FILE"
                    TOTAL_FILES=$((TOTAL_FILES + 1))
                fi
            done
            echo -e "  ${GREEN}✓${NC} $component ${DIM}(minimal)${NC}"
        else
            # Load all .gdb files in order
            for gdb_file in "$COMP_DIR"/*.gdb; do
                if [ -f "$gdb_file" ]; then
                    # Get just the filename and use Docker path
                    filename=$(basename "$gdb_file")
                    echo "source $GDB_COMP_DIR/$filename" >> "$INIT_FILE"
                    TOTAL_FILES=$((TOTAL_FILES + 1))
                fi
            done
            FILE_COUNT=$(ls -1 "$COMP_DIR"/*.gdb 2>/dev/null | wc -l)
            echo -e "  ${GREEN}✓${NC} $component ${DIM}($FILE_COUNT fichiers)${NC}"
        fi
    fi
done

echo ""
echo -e "Total: ${GREEN}$TOTAL_FILES${NC} fichiers de breakpoints"
echo ""

# ==========================================
# AUTO-CONTINUE: dprintf handles this automatically
# ==========================================
cat >> "$INIT_FILE" << 'MODE_EOF'

echo 
echo [AUTONOMOUS] Mode configured
echo [AUTONOMOUS] dprintf points: log and auto-continue
echo [AUTONOMOUS] Crashes: full dump + EXIT
echo 
echo ════════════════════════════════════════════════════════════════════════════════
echo  DEBUGGER READY - Waiting for events...
echo ════════════════════════════════════════════════════════════════════════════════
echo 

MODE_EOF

# ==========================================
# Session start
# ==========================================
cat >> "$INIT_FILE" << 'START_EOF'

# ==========================================
# SESSION START
# ==========================================
define session-info
    echo 
    echo ══════════════════════════════════════════════════════════════
    echo  Session Ready
    echo ══════════════════════════════════════════════════════════════
    echo 
    echo Commands:
    echo   c / continue    - Resume execution
    echo   bt / backtrace  - Show call stack
    echo   info threads    - List threads
    echo   ryu-help        - Full command reference
    echo   bpl             - List breakpoints
    echo 
    info breakpoints
    echo 
end

session-info

echo 
echo [START] Launching autonomous execution...
echo [START] Press Ctrl+C to interrupt
echo 

continue

START_EOF

echo -e "${GREEN}Init: $INIT_FILE${NC}"
echo -e "${GREEN}Log:  $LOG_FILE${NC}"
echo ""

# ==========================================
# Launch GDB
# ==========================================
echo -e "${MAGENTA}${BOLD}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${MAGENTA}${BOLD}║  AUTONOMOUS MODE                                             ║${NC}"
echo -e "${MAGENTA}${BOLD}║  • Breakpoints: auto-continue + logging                      ║${NC}"
echo -e "${MAGENTA}${BOLD}║  • Crashes: full dump + EXIT                                 ║${NC}"
echo -e "${MAGENTA}${BOLD}║  • Press Ctrl+C to interrupt                                 ║${NC}"
echo -e "${MAGENTA}${BOLD}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""

"$GDB" -q -x "$INIT_FILE" 2>&1 | \
    grep -Ev "(warning:.*auto-loading|add-auto-load-safe-path|set auto-load safe-path|Reading symbols)"

# ==========================================
# Session Complete
# ==========================================
echo ""
echo -e "${CYAN}${BOLD}══════════════════════════════════════════════════════════════${NC}"
echo -e "${CYAN}${BOLD}  Session terminée${NC}"
echo -e "${CYAN}${BOLD}══════════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "Log:  ${YELLOW}$LOG_FILE${NC}"
echo -e "Init: ${YELLOW}$INIT_FILE${NC}"
echo ""
echo -e "Replay: ${CYAN}$GDB -q -x $INIT_FILE${NC}"
echo ""
