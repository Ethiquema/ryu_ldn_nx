# Debugging ryu_ldn_nx avec GDB

Ce guide explique comment débugger le sysmodule et l'overlay avec GDB via le stub intégré d'Atmosphere.

## Prérequis

- Atmosphere >= 1.2.4
- `aarch64-none-elf-gdb` (inclus dans devkitpro)
- libncurses5 (Linux uniquement)

```bash
# Linux - installer libncurses si nécessaire
sudo pacman -S ncurses5-compat-libs  # Arch
sudo apt install libncurses5         # Debian/Ubuntu
```

## Configuration Atmosphere

### 1. Activer le GDB Stub

Créer/modifier le fichier sur la carte SD :
`sd:/atmosphere/config/system_settings.ini`

```ini
[atmosphere]
; Désactiver HTC (Host Target Communication) pour éviter les conflits
enable_htc = u8!0x0
; Activer le stub GDB standalone
enable_standalone_gdbstub = u8!0x1
```

### 2. Désactiver dmnt (important!)

Le debug monitor d'Atmosphere (dmnt) peut entrer en conflit avec GDB.
Si vous utilisez EdiZon ou des cheats, désactivez-les temporairement :

```bash
# Renommer le dossier dmnt
mv sd:/atmosphere/contents/010000000000000D sd:/atmosphere/contents/010000000000000D.bak
```

## Connexion GDB

### 1. Obtenir l'IP de la Switch

Dans les paramètres système : `Paramètres > Internet > État de la connexion`

### 2. Lancer GDB

```bash
# Avec devkitpro
$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gdb

# Ou si dans le PATH
aarch64-none-elf-gdb
```

### 3. Se connecter

```gdb
(gdb) target extended-remote <IP_SWITCH>:22225
```

### 4. Lister les processus

```gdb
(gdb) monitor get info
```

### 5. Attacher au sysmodule

```gdb
(gdb) attach <PID>
```

Le PID de ryu_ldn_nx peut être trouvé avec `monitor get info`.

### 6. Gérer l'ASLR et charger les symboles

La Switch utilise l'ASLR (Address Space Layout Randomization). L'adresse de base
du module change à chaque démarrage. Il faut la récupérer pour charger les symboles.

#### Méthode automatique (recommandée)

Le fichier `.gdbinit` fourni inclut des commandes pour détecter automatiquement
l'adresse de base via le registre PC :

```gdb
# Après attach, charger automatiquement les symboles
(gdb) autoload-sysmodule

# Ou pour l'overlay
(gdb) autoload-overlay

# Pour juste voir l'adresse de base sans charger les symboles
(gdb) autobase
```

Ces commandes lisent le registre PC et l'alignent sur une frontière de 1MB pour
obtenir l'adresse de base du module.

#### Méthode manuelle via mappings

```gdb
# Après attach, obtenir les mappings mémoire du processus
(gdb) monitor get mappings

# Exemple de sortie:
#   Address              Size               Permissions  Name
#   0x0000007100000000   0x0000000000010000 r-x          .text
#   0x0000007100010000   0x0000000000002000 r--          .rodata
#   0x0000007100012000   0x0000000000001000 rw-          .data
#
# L'adresse de base est la première région (ici 0x7100000000)

# Charger les symboles avec l'offset ASLR
(gdb) symbol-file sysmodule/ryu_ldn_nx.elf -o 0x7100000000
```

#### Script de détection standalone

Un script bash est également fourni pour détecter l'adresse de base :

```bash
./scripts/gdb-detect-base.sh <IP_SWITCH> <PID>
```

Ce script se connecte, lit le registre PC, calcule l'adresse de base alignée sur
1MB, et affiche la commande à utiliser.

#### Calcul de l'offset (pour référence)

L'offset = adresse_runtime - adresse_dans_elf

```gdb
# Voir l'adresse de base dans l'ELF (généralement 0x0)
(gdb) !readelf -l sysmodule/ryu_ldn_nx.elf | grep LOAD

# Si l'ELF commence à 0x0 et le runtime à 0x7100000000
# alors l'offset est 0x7100000000
```

## Script de Connexion Rapide

Utiliser le script fourni :

```bash
./scripts/gdb-connect.sh <IP_SWITCH>
```

## Commandes GDB Utiles

```gdb
# Breakpoint sur une fonction
(gdb) break ryu_ldn::config::ConfigManager::Initialize

# Continuer l'exécution
(gdb) continue

# Step into
(gdb) step

# Step over
(gdb) next

# Afficher la pile d'appels
(gdb) backtrace

# Afficher une variable
(gdb) print variable_name

# Afficher la mémoire
(gdb) x/10x 0xADDRESS

# Détacher
(gdb) detach

# Quitter
(gdb) quit
```

## Debugging de l'Overlay

L'overlay Tesla fonctionne de la même manière :

```gdb
(gdb) target extended-remote <IP>:22225
(gdb) monitor get info
# Trouver le PID de ryu_ldn_nx_overlay
(gdb) attach <PID>
(gdb) symbol-file overlay/ryu_ldn_nx_overlay.elf -o 0xOFFSET
```

## Build Debug

Pour compiler avec optimisations réduites (plus facile à débugger) :

```bash
# Sysmodule
cd sysmodule
ATMOSPHERE_BUILD_FOR_DEBUGGING=1 make

# Overlay
cd overlay
make DEBUG=1
```

## Troubleshooting

### "Connection refused"
- Vérifier que `enable_standalone_gdbstub = u8!0x1` est bien dans system_settings.ini
- Vérifier que la Switch est sur le même réseau

### "Cannot attach to process"
- Désactiver dmnt (voir section 2)
- Un seul debugger peut être attaché à la fois

### Symboles non trouvés
- Vérifier que vous utilisez le bon fichier .elf
- Vérifier l'offset avec `monitor get info`
- Recompiler avec les symboles de debug (`-g` flag, inclus par défaut)

### Crash au démarrage
- Le kernel panic `DABRT 0x101` est lié à l'accès SD, pas au debugging
- Utiliser la version avec `ams::fs` API

## Références

- [GDB for Switch Modding Cheatsheet](https://gist.github.com/jam1garner/c9ba6c0cff150f1a2480d0c18ff05e33)
- [Debugging sysmodules on the Switch](https://periwinkle.sh/blog/nxgdb/)
- [sys-gdbstub](https://github.com/mossvr/sys-gdbstub)