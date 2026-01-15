#!/bin/bash
# Script de connexion GDB pour ryu_ldn_nx
# Usage: ./gdb-connect.sh <IP_SWITCH> [sysmodule|overlay]

set -e

SWITCH_IP="${1:-}"
TARGET="${2:-sysmodule}"
GDB_PORT="22225"

# Couleurs
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

if [ -z "$SWITCH_IP" ]; then
    echo -e "${RED}Usage: $0 <IP_SWITCH> [sysmodule|overlay]${NC}"
    echo ""
    echo "Exemples:"
    echo "  $0 192.168.1.100              # Debug sysmodule"
    echo "  $0 192.168.1.100 overlay      # Debug overlay"
    exit 1
fi

# Trouver GDB
if [ -n "$DEVKITPRO" ]; then
    GDB="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gdb"
else
    GDB="aarch64-none-elf-gdb"
fi

if ! command -v "$GDB" &> /dev/null; then
    echo -e "${RED}Erreur: GDB non trouvé${NC}"
    echo "Installer devkitpro ou définir DEVKITPRO"
    exit 1
fi

# Trouver le fichier ELF
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

if [ "$TARGET" = "overlay" ]; then
    ELF_FILE="$PROJECT_DIR/overlay/ryu_ldn_nx_overlay.elf"
    PROCESS_NAME="ryu_ldn_nx_overlay"
else
    ELF_FILE="$PROJECT_DIR/sysmodule/ryu_ldn_nx.elf"
    PROCESS_NAME="ryu_ldn_nx"
fi

if [ ! -f "$ELF_FILE" ]; then
    echo -e "${YELLOW}Attention: $ELF_FILE non trouvé${NC}"
    echo "Compiler d'abord avec: make"
    ELF_FILE=""
fi

echo -e "${GREEN}=== ryu_ldn_nx GDB Debugger ===${NC}"
echo ""
echo "Switch IP: $SWITCH_IP"
echo "Target: $TARGET"
echo "ELF: ${ELF_FILE:-non trouvé}"
echo ""
echo -e "${YELLOW}Commandes utiles:${NC}"
echo "  monitor get info          - Lister les processus"
echo "  attach <PID>              - Attacher au processus"
echo "  symbol-file <elf> -o OFF  - Charger les symboles"
echo "  continue                  - Continuer l'exécution"
echo "  break <function>          - Ajouter un breakpoint"
echo "  backtrace                 - Afficher la pile d'appels"
echo ""

# Créer un fichier de commandes GDB temporaire
GDB_INIT=$(mktemp)
cat > "$GDB_INIT" << EOF
set pagination off
set confirm off

echo Connexion a $SWITCH_IP:$GDB_PORT...\n
target extended-remote $SWITCH_IP:$GDB_PORT

echo \n
echo Connecte! Utilisez 'monitor get info' pour lister les processus.\n
echo Puis 'attach <PID>' pour attacher au processus $PROCESS_NAME.\n
EOF

if [ -n "$ELF_FILE" ]; then
    cat >> "$GDB_INIT" << EOF
echo \nFichier ELF disponible: $ELF_FILE\n
echo Apres attach, charger les symboles avec:\n
echo   symbol-file $ELF_FILE -o 0xOFFSET\n
echo (remplacer OFFSET par la valeur de 'monitor get info')\n
EOF
fi

echo "" >> "$GDB_INIT"

# Lancer GDB
"$GDB" -x "$GDB_INIT"

# Nettoyer
rm -f "$GDB_INIT"
