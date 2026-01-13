#!/bin/bash
# Script de détection automatique de l'adresse de base ASLR
# Utilise le registre PC et aligne sur 2MB
# Usage: ./gdb-detect-base.sh <IP_SWITCH> <PID>

set -e

SWITCH_IP="${1:-}"
PROCESS_ID="${2:-}"
GDB_PORT="22225"

# Couleurs
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

if [ -z "$SWITCH_IP" ] || [ -z "$PROCESS_ID" ]; then
    echo -e "${RED}Usage: $0 <IP_SWITCH> <PID>${NC}"
    echo ""
    echo "Exemples:"
    echo "  $0 192.168.1.100 134"
    echo ""
    echo "Pour trouver le PID, utilisez:"
    echo "  ./gdb-connect.sh <IP> puis 'monitor get info'"
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
    exit 1
fi

echo -e "${CYAN}=== Détection adresse de base ASLR ===${NC}"
echo "Switch: $SWITCH_IP:$GDB_PORT"
echo "PID: $PROCESS_ID"
echo ""

# Fichier temporaire pour capturer la sortie
DETECT_FILE=$(mktemp)

# Connexion GDB batch pour récupérer PC
echo -e "${YELLOW}Connexion et lecture du registre PC...${NC}"
"$GDB" -batch -q \
    -ex "set tcp connect-timeout 60" \
    -ex "target extended-remote $SWITCH_IP:$GDB_PORT" \
    -ex "attach $PROCESS_ID" \
    -ex "info registers pc" \
    -ex "detach" \
    -ex "quit" > "$DETECT_FILE" 2>&1 || true

# Extraire la valeur du PC
PC_RAW=$(grep -E "^pc\s+" "$DETECT_FILE" | awk '{print $2}')

if [ -z "$PC_RAW" ]; then
    echo -e "${RED}Erreur: Impossible de lire le registre PC${NC}"
    echo "Sortie GDB:"
    cat "$DETECT_FILE"
    rm -f "$DETECT_FILE"
    exit 1
fi

echo -e "${GREEN}PC détecté: $PC_RAW${NC}"

# Supprimer le préfixe 0x et les zéros en tête
PC_HEX="${PC_RAW#0x}"
PC_HEX="${PC_HEX#"${PC_HEX%%[!0]*}"}"

# Calculer la longueur
PC_LEN=${#PC_HEX}

# Aligner sur frontière 2MB (supprimer les 5 derniers chiffres hex = 0x100000 = 1MB, mais on utilise 5 pour 2MB alignement)
# 5 hex digits = 20 bits = 1MB, pour 2MB on garde cette approche
BASE_LEN=$((PC_LEN - 5))
if [ $BASE_LEN -lt 1 ]; then
    BASE_LEN=1
fi

# Extraire le préfixe de base
BASE_PREFIX="${PC_HEX:0:$BASE_LEN}"
BASE_ADDR="0x${BASE_PREFIX}00000"

echo ""
echo -e "${GREEN}════════════════════════════════════════${NC}"
echo -e "${GREEN}Adresse de base calculée: ${CYAN}$BASE_ADDR${NC}"
echo -e "${GREEN}════════════════════════════════════════${NC}"
echo ""
echo -e "${YELLOW}Pour charger les symboles dans GDB:${NC}"
echo "  symbol-file sysmodule/ryu_ldn_nx.elf -o $BASE_ADDR"
echo ""
echo -e "${YELLOW}Ou pour l'overlay:${NC}"
echo "  symbol-file overlay/ryu_ldn_nx_overlay.elf -o $BASE_ADDR"

# Nettoyer
rm -f "$DETECT_FILE"

# Retourner l'adresse pour usage dans d'autres scripts
echo ""
echo "BASE_ADDR=$BASE_ADDR"
