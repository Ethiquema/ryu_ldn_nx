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
# BSD MITM Breakpoints
# ============================================================================

define bp-bsd-mitm
    echo Ajout breakpoints BSD MITM (auto-continue)...\n

    # ShouldMitm - called to decide if we intercept a process
    break ams::mitm::bsd::BsdMitmService::ShouldMitm
    commands
        silent
        printf "[BSD] ShouldMitm pid=%lu\n", $x1
        continue
    end

    # Socket creation
    break ams::mitm::bsd::BsdMitmService::Socket
    commands
        silent
        printf "[BSD] Socket(domain=%d, type=%d, proto=%d)\n", $x4, $x5, $x6
        continue
    end

    # Bind
    break ams::mitm::bsd::BsdMitmService::Bind
    commands
        silent
        printf "[BSD] Bind(fd=%d)\n", $x3
        continue
    end

    # Connect
    break ams::mitm::bsd::BsdMitmService::Connect
    commands
        silent
        printf "[BSD] Connect(fd=%d)\n", $x3
        continue
    end

    # SendTo
    break ams::mitm::bsd::BsdMitmService::SendTo
    commands
        silent
        printf "[BSD] SendTo(fd=%d)\n", $x4
        continue
    end

    # RecvFrom
    break ams::mitm::bsd::BsdMitmService::RecvFrom
    commands
        silent
        printf "[BSD] RecvFrom(fd=%d)\n", $x4
        continue
    end

    # Close
    break ams::mitm::bsd::BsdMitmService::Close
    commands
        silent
        printf "[BSD] Close(fd=%d)\n", $x3
        continue
    end

    info breakpoints
end
document bp-bsd-mitm
Breakpoints sur le BSD MITM service (trace automatique).
Les appels sont loggues et l'execution continue.
end

define bp-bsd-verbose
    echo Ajout breakpoints BSD MITM (mode verbose - arret a chaque appel)...\n
    break ams::mitm::bsd::BsdMitmService::ShouldMitm
    break ams::mitm::bsd::BsdMitmService::Socket
    break ams::mitm::bsd::BsdMitmService::Bind
    break ams::mitm::bsd::BsdMitmService::Connect
    break ams::mitm::bsd::BsdMitmService::SendTo
    break ams::mitm::bsd::BsdMitmService::RecvFrom
    break ams::mitm::bsd::BsdMitmService::Close
    info breakpoints
end
document bp-bsd-verbose
Breakpoints BSD MITM en mode verbose (arret a chaque appel).
end

# ============================================================================
# Proxy Socket Breakpoints
# ============================================================================

define bp-proxy-socket
    echo Ajout breakpoints Proxy Socket...\n

    break ams::mitm::bsd::ProxySocketManager::CreateProxySocket
    commands
        silent
        printf "[ProxySocket] CreateProxySocket\n"
        continue
    end

    break ams::mitm::bsd::ProxySocketManager::FindSocketByDestination
    commands
        silent
        printf "[ProxySocket] FindSocketByDest(ip=0x%x, port=%d)\n", $x1, $x2
        continue
    end

    break ams::mitm::bsd::ProxySocketManager::DeliverProxyData
    commands
        silent
        printf "[ProxySocket] DeliverProxyData\n"
        continue
    end

    info breakpoints
end
document bp-proxy-socket
Breakpoints sur le ProxySocketManager (trace automatique).
end

# ============================================================================
# LDN MITM Breakpoints
# ============================================================================

define bp-ldn-mitm
    echo Ajout breakpoints LDN MITM...\n

    break ams::mitm::ldn::LdnMitMService::ShouldMitm
    commands
        silent
        printf "[LDN] ShouldMitm program_id=0x%lx\n", $x1
        continue
    end

    break ams::mitm::ldn::LdnMitMService::LdnMitMService
    commands
        silent
        printf "[LDN] Constructor\n"
        continue
    end

    break ams::mitm::ldn::LdnMitMService::~LdnMitMService
    commands
        silent
        printf "[LDN] Destructor\n"
        continue
    end

    break ams::mitm::ldn::LdnMitMService::CreateUserLocalCommunicationService
    commands
        silent
        printf "[LDN] CreateUserLocalCommunicationService\n"
        continue
    end

    info breakpoints
end
document bp-ldn-mitm
Breakpoints sur le LDN MITM service (trace automatique).
end

# ============================================================================
# PID Tracker Breakpoints
# ============================================================================

define bp-pid-tracker
    echo Ajout breakpoints PID Tracker...\n

    break ryu_ldn::LdnPidTracker::RegisterPid
    commands
        silent
        printf "[PidTracker] RegisterPid(pid=%lu)\n", $x1
        continue
    end

    break ryu_ldn::LdnPidTracker::UnregisterPid
    commands
        silent
        printf "[PidTracker] UnregisterPid(pid=%lu)\n", $x1
        continue
    end

    break ryu_ldn::LdnPidTracker::IsLdnPid
    commands
        silent
        printf "[PidTracker] IsLdnPid(pid=%lu)\n", $x1
        continue
    end

    info breakpoints
end
document bp-pid-tracker
Breakpoints sur le LdnPidTracker (trace automatique).
end

# ============================================================================
# Communication Service Breakpoints
# ============================================================================

define bp-comm-service
    echo Ajout breakpoints ICommunicationService...\n

    break ams::mitm::ldn::ICommunicationService::ICommunicationService
    commands
        silent
        printf "[CommSvc] Constructor\n"
        continue
    end

    break ams::mitm::ldn::ICommunicationService::Initialize
    commands
        silent
        printf "[CommSvc] Initialize\n"
        continue
    end

    break ams::mitm::ldn::ICommunicationService::Scan
    commands
        silent
        printf "[CommSvc] Scan\n"
        continue
    end

    break ams::mitm::ldn::ICommunicationService::Connect
    commands
        silent
        printf "[CommSvc] Connect\n"
        continue
    end

    break ams::mitm::ldn::ICommunicationService::ConnectToServer
    commands
        silent
        printf "[CommSvc] ConnectToServer\n"
        continue
    end

    break ams::mitm::ldn::ICommunicationService::DisconnectFromServer
    commands
        silent
        printf "[CommSvc] DisconnectFromServer\n"
        continue
    end

    info breakpoints
end
document bp-comm-service
Breakpoints sur ICommunicationService (trace automatique).
end

# ============================================================================
# Quick Debug Setups
# ============================================================================

define quick-bsd
    echo === Quick BSD MITM Debug Setup ===\n
    catch signal SIGABRT
    catch signal SIGSEGV
    bp-bsd-mitm
    bp-proxy-socket
    bp-pid-tracker
    echo \nPret! Tapez 'continue' pour lancer.\n
end
document quick-bsd
Setup rapide pour debugger le BSD MITM.
Inclut: BSD MITM + Proxy Socket + PID Tracker.
end

define quick-ldn
    echo === Quick LDN Debug Setup ===\n
    catch signal SIGABRT
    catch signal SIGSEGV
    bp-ldn-mitm
    bp-comm-service
    echo \nPret! Tapez 'continue' pour lancer.\n
end
document quick-ldn
Setup rapide pour debugger le LDN MITM.
Inclut: LDN MITM + Communication Service.
end

define quick-full
    echo === Full Debug Setup ===\n
    catch signal SIGABRT
    catch signal SIGSEGV
    bp-bsd-mitm
    bp-proxy-socket
    bp-ldn-mitm
    bp-pid-tracker
    bp-comm-service
    echo \nPret! Tapez 'continue' pour lancer.\n
end
document quick-full
Setup complet pour debugger tout ryu_ldn_nx.
end

# ============================================================================
# Memory Info
# ============================================================================

define ryu-meminfo
    printf "=== ryu_ldn_nx Memory Layout ===\n\n"
    printf "Static allocations:\n"
    printf "  MallocBufferSize:    256 KB\n"
    printf "  g_heap_memory:        64 KB\n"
    printf "  PacketBuffer:          8 KB\n"
    printf "  LdnProxyBuffer:      ~8.5 KB\n"
    printf "  ScanResults:          ~9 KB (8 x NetworkInfo)\n"
    printf "  Thread stacks:       ~48 KB (3 x 16 KB)\n"
    printf "  Socket buffers:      ~32 KB\n"
    printf "\n"
    printf "Total estimate: ~425 KB\n"
    printf "(Switch sysmodules share ~10 MB)\n"
end
document ryu-meminfo
Afficher les tailles memoire du sysmodule.
end

# ============================================================================
# Message d'accueil
# ============================================================================

echo \n
echo ╔═══════════════════════════════════════════════════════════════════╗\n
echo ║                   ryu_ldn_nx GDB Debugger                         ║\n
echo ╠═══════════════════════════════════════════════════════════════════╣\n
echo ║ CONNEXION:                                                        ║\n
echo ║   connect <IP>               Connecter a la Switch                ║\n
echo ║   lsproc                     Lister les processus                 ║\n
echo ║   attach <PID>               Attacher au processus                ║\n
echo ║                                                                   ║\n
echo ║ ASLR + SYMBOLES (automatique):                                    ║\n
echo ║   autoload-sysmodule         Auto-detect base + charger symboles  ║\n
echo ║   autobase                   Juste afficher la base detectee      ║\n
echo ║                                                                   ║\n
echo ║ QUICK DEBUG (recommande):                                         ║\n
echo ║   quick-bsd                  BSD MITM + Proxy + PID tracker       ║\n
echo ║   quick-ldn                  LDN MITM + Communication service     ║\n
echo ║   quick-full                 Tous les breakpoints                 ║\n
echo ║                                                                   ║\n
echo ║ BREAKPOINTS INDIVIDUELS:                                          ║\n
echo ║   bp-bsd-mitm                BSD MITM (trace auto)                ║\n
echo ║   bp-bsd-verbose             BSD MITM (arret a chaque appel)      ║\n
echo ║   bp-proxy-socket            ProxySocketManager                   ║\n
echo ║   bp-ldn-mitm                LDN MITM service                     ║\n
echo ║   bp-pid-tracker             PID tracker                          ║\n
echo ║   bp-comm-service            ICommunicationService                ║\n
echo ║   bp-config                  ConfigManager                        ║\n
echo ║                                                                   ║\n
echo ║ INFO:                                                             ║\n
echo ║   ryu-meminfo                Afficher layout memoire              ║\n
echo ╚═══════════════════════════════════════════════════════════════════╝\n
echo \n
echo Workflow typique:\n
echo   1. connect 192.168.1.x       <- IP de la Switch\n
echo   2. lsproc                    <- trouver PID de ryu_ldn_nx\n
echo   3. attach <PID>\n
echo   4. autoload-sysmodule        <- charger symboles\n
echo   5. quick-bsd                 <- setup breakpoints BSD\n
echo   6. continue                  <- lancer\n
echo \n
