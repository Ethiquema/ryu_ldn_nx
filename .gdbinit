# GDB configuration pour ryu_ldn_nx
# Usage: gdb -x .gdbinit
#
# ASLR: La Switch utilise l'Address Space Layout Randomization.
# L'adresse de base du module change à chaque démarrage.
# Utilisez 'getbase' après attach pour récupérer l'offset.

set pagination off
set print pretty on
set print array on
set print array-indexes on

# Définir l'architecture
set architecture aarch64

# ============================================================================
# Commandes de connexion
# ============================================================================

define connect
    if $argc == 1
        target extended-remote $arg0:22225
        echo Connecte! Utilisez 'lsproc' pour lister les processus.\n
    else
        echo Usage: connect <IP_SWITCH>\n
        echo Exemple: connect 192.168.1.100\n
    end
end
document connect
Connecter à la Switch via GDB stub Atmosphere (port 22225).
Usage: connect 192.168.1.100
end

define lsproc
    echo === Liste des processus ===\n
    monitor get info
end
document lsproc
Lister les processus en cours sur la Switch avec leur PID.
end

# ============================================================================
# Commandes ASLR / Adresse de base
# ============================================================================

# Variable pour stocker l'adresse de base calculée
set $ryu_base = 0

define getbase
    echo === Mappings memoire (ASLR) ===\n
    echo L'adresse de base est la premiere region .text (r-x)\n\n
    monitor get mappings
end
document getbase
Afficher les mappings mémoire pour trouver l'adresse de base (ASLR).
L'adresse de base est généralement la première région avec permissions r-x.
end

define showmaps
    info proc mappings
end
document showmaps
Alternative: afficher les mappings via info proc.
end

define autobase
    echo === Detection automatique adresse de base (via PC) ===\n
    echo Lecture du registre PC...\n
    set $pc_val = $pc
    printf "PC actuel: 0x%lx\n", $pc_val
    # Aligner sur frontiere 2MB (0x200000) - masquer les 21 bits inferieurs
    # Mais pour la Switch, on aligne souvent sur 0x100000 (1MB)
    # Le script batch utilise 5 hex digits = 20 bits = 1MB
    set $ryu_base = $pc_val & ~0xFFFFF
    printf "Adresse de base (alignee 1MB): 0x%lx\n", $ryu_base
    echo \n
    echo Utilisez maintenant:\n
    printf "  loadsym-sysmodule 0x%lx\n", $ryu_base
    echo ou:\n
    printf "  loadsym-overlay 0x%lx\n", $ryu_base
    echo \n
end
document autobase
Detecter automatiquement l'adresse de base ASLR via le registre PC.
Aligne la valeur du PC sur une frontiere de 1MB.
Utiliser apres 'attach <PID>'.
end

define autoload-sysmodule
    echo === Chargement automatique symboles sysmodule ===\n
    # Calculer la base
    set $pc_val = $pc
    set $ryu_base = $pc_val & ~0xFFFFF
    printf "Base detectee: 0x%lx\n", $ryu_base
    # Charger les symboles
    printf "Chargement: sysmodule/ryu_ldn_nx.elf -o 0x%lx\n", $ryu_base
    symbol-file sysmodule/ryu_ldn_nx.elf -o $ryu_base
    echo Symboles charges!\n
    info functions main
end
document autoload-sysmodule
Detecter la base ASLR et charger automatiquement les symboles du sysmodule.
Utiliser apres 'attach <PID>'.
end

define autoload-overlay
    echo === Chargement automatique symboles overlay ===\n
    # Calculer la base
    set $pc_val = $pc
    set $ryu_base = $pc_val & ~0xFFFFF
    printf "Base detectee: 0x%lx\n", $ryu_base
    # Charger les symboles
    printf "Chargement: overlay/ryu_ldn_nx_overlay.elf -o 0x%lx\n", $ryu_base
    symbol-file overlay/ryu_ldn_nx_overlay.elf -o $ryu_base
    echo Symboles charges!\n
end
document autoload-overlay
Detecter la base ASLR et charger automatiquement les symboles de l'overlay.
Utiliser apres 'attach <PID>'.
end

# ============================================================================
# Chargement des symboles
# ============================================================================

define loadsym-sysmodule
    if $argc == 1
        echo Chargement symboles sysmodule avec offset $arg0...\n
        symbol-file sysmodule/ryu_ldn_nx.elf -o $arg0
        echo Symboles charges! Utilisez 'info functions' pour verifier.\n
    else
        echo Usage: loadsym-sysmodule <OFFSET_ASLR>\n
        echo \n
        echo Etapes:\n
        echo   1. attach <PID>\n
        echo   2. getbase           <- trouver l'adresse de base\n
        echo   3. loadsym-sysmodule 0x7100000000\n
    end
end
document loadsym-sysmodule
Charger les symboles du sysmodule avec l'offset ASLR.
Usage: loadsym-sysmodule 0x7100000000
Utilisez 'getbase' pour trouver l'offset.
end

define loadsym-overlay
    if $argc == 1
        echo Chargement symboles overlay avec offset $arg0...\n
        symbol-file overlay/ryu_ldn_nx_overlay.elf -o $arg0
        echo Symboles charges! Utilisez 'info functions' pour verifier.\n
    else
        echo Usage: loadsym-overlay <OFFSET_ASLR>\n
        echo \n
        echo Etapes:\n
        echo   1. attach <PID>\n
        echo   2. getbase           <- trouver l'adresse de base\n
        echo   3. loadsym-overlay 0x7100000000\n
    end
end
document loadsym-overlay
Charger les symboles de l'overlay avec l'offset ASLR.
Usage: loadsym-overlay 0x7100000000
Utilisez 'getbase' pour trouver l'offset.
end

# ============================================================================
# Workflow complet
# ============================================================================

define debug-sysmodule
    if $argc == 2
        echo === Debug sysmodule ryu_ldn_nx ===\n
        echo IP: $arg0, PID: $arg1\n\n
        target extended-remote $arg0:22225
        attach $arg1
        echo \nAttache! Utilisez 'getbase' pour l'adresse ASLR.\n
        echo Puis: loadsym-sysmodule <OFFSET>\n
    else
        echo Usage: debug-sysmodule <IP> <PID>\n
        echo \n
        echo Workflow complet:\n
        echo   1. connect <IP>\n
        echo   2. lsproc              <- trouver le PID de ryu_ldn_nx\n
        echo   3. attach <PID>\n
        echo   4. getbase             <- trouver l'offset ASLR\n
        echo   5. loadsym-sysmodule <OFFSET>\n
        echo   6. break main          <- ajouter breakpoints\n
        echo   7. continue\n
    end
end
document debug-sysmodule
Workflow complet pour débugger le sysmodule.
Usage: debug-sysmodule 192.168.1.100 <PID>
end

# ============================================================================
# Breakpoints utiles
# ============================================================================

define bp-config
    echo Ajout breakpoints sur ConfigManager...\n
    break ryu_ldn::config::ConfigManager::Initialize
    break ryu_ldn::config::ConfigManager::Save
    break ryu_ldn::config::ConfigManager::Reload
    break ryu_ldn::config::load_config
    break ryu_ldn::config::save_config
    info breakpoints
end
document bp-config
Ajouter des breakpoints sur les fonctions de configuration.
end

# ============================================================================
# Message d'accueil
# ============================================================================

echo \n
echo ╔══════════════════════════════════════════════════════════════╗\n
echo ║              ryu_ldn_nx GDB Debugger                         ║\n
echo ╠══════════════════════════════════════════════════════════════╣\n
echo ║ Connexion:                                                   ║\n
echo ║   connect <IP>              Connecter a la Switch            ║\n
echo ║   lsproc                    Lister les processus             ║\n
echo ║                                                              ║\n
echo ║ ASLR (adresse de base):                                      ║\n
echo ║   autobase                  Auto-detect base via PC          ║\n
echo ║   getbase                   Afficher mappings memoire        ║\n
echo ║                                                              ║\n
echo ║ Symboles (automatique):                                      ║\n
echo ║   autoload-sysmodule        Auto-detect + charger sysmodule  ║\n
echo ║   autoload-overlay          Auto-detect + charger overlay    ║\n
echo ║                                                              ║\n
echo ║ Symboles (manuel):                                           ║\n
echo ║   loadsym-sysmodule <OFF>   Charger avec offset manuel       ║\n
echo ║   loadsym-overlay <OFF>     Charger avec offset manuel       ║\n
echo ║                                                              ║\n
echo ║ Workflow:                                                    ║\n
echo ║   debug-sysmodule <IP> <PID>  Connexion + attach             ║\n
echo ║   bp-config                   Breakpoints config             ║\n
echo ╚══════════════════════════════════════════════════════════════╝\n
echo \n
