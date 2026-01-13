# Architecture Technique : ryu_ldn_nx

**Version** : 1.0
**Date** : 2026-01-12
**Statut** : Draft

---

## 1. Vue d'ensemble

### 1.1 Objectif

ryu_ldn_nx est un sysmodule Atmosphere qui remplace ldn_mitm pour permettre aux jeux Switch d'utiliser le mode multijoueur local (LDN) via les serveurs Ryujinx, sans configuration réseau complexe.

### 1.2 Décisions architecturales clés

| Décision | Choix | Justification |
|----------|-------|---------------|
| Base de code | Fork ldn_mitm | Architecture sysmodule éprouvée, MITM LDN fonctionnel |
| Langage | C++ (Stratosphere) | Requis pour sysmodule Switch, performance optimale |
| Configuration | Fichier INI sur SD | Flexibilité sans recompilation |
| Compatibilité ldn_mitm | Remplacement total | Simplicité, pas de conflit |
| Reconnexion | Backoff exponentiel | Standard industrie, évite surcharge serveur |
| Versioning protocole | Négocié au handshake | Robustesse, évolutivité |
| Interface utilisateur | Overlay Tesla | Debug et configuration en jeu |
| Licence | GPLv3 | Copyleft, cohérent avec ldn_mitm |
| Contributions | DCO Sign-off | Standard industrie open source |

### 1.3 Contraintes techniques

| Contrainte | Limite | Source |
|------------|--------|--------|
| Mémoire totale | < 2 MB | Budget sysmodule Switch |
| Heap statique | 1 MB | Basé sur ldn_mitm |
| Heap dynamique | 128 KB | Basé sur ldn_mitm |
| Threads max | 4 | Minimiser overhead |
| Latence réseau | < 10 ms processing | UX acceptable |
| CPU usage | < 1% idle, < 5% actif | Pas d'impact jeu |

---

## 2. Architecture système

### 2.1 Diagramme de contexte

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Nintendo Switch                              │
│                                                                      │
│  ┌──────────────┐         ┌─────────────────────────────────────┐  │
│  │              │         │         ryu_ldn_nx                   │  │
│  │     Jeu      │◄───────►│           (sysmodule)                │  │
│  │  (ldn:u IPC) │  MITM   │                                      │  │
│  │              │         │  ┌─────────────────────────────────┐ │  │
│  └──────────────┘         │  │     LDN Service (IPC)           │ │  │
│                           │  │  - GetState, Scan, Connect...   │ │  │
│                           │  └──────────────┬──────────────────┘ │  │
│                           │                 │                     │  │
│                           │  ┌──────────────▼──────────────────┐ │  │
│                           │  │   Protocol Translator           │ │  │
│                           │  │  - LDN ←→ RyuLdn conversion     │ │  │
│                           │  └──────────────┬──────────────────┘ │  │
│                           │                 │                     │  │
│                           │  ┌──────────────▼──────────────────┐ │  │
│                           │  │   Network Client                │ │  │
│                           │  │  - TCP to ryu_ldn server        │ │  │
│                           │  │  - Reconnection logic           │ │  │
│                           │  └──────────────┬──────────────────┘ │  │
│                           └─────────────────┼───────────────────┘  │
│                                             │                       │
│  ┌──────────────┐                          │                       │
│  │ Tesla Overlay│◄─────────────────────────┤                       │
│  │  (debug/cfg) │                          │                       │
│  └──────────────┘                          │                       │
└────────────────────────────────────────────┼───────────────────────┘
                                             │ TCP/IP (WiFi)
                                             ▼
                              ┌──────────────────────────┐
                              │    Serveur ryu_ldn       │
                              │   (ldn.ryujinx.app)      │
                              │                          │
                              │  - Matchmaking           │
                              │  - Session management    │
                              │  - P2P relay             │
                              └──────────────────────────┘
```

### 2.2 Composants principaux

#### 2.2.1 LDN Service (Couche IPC)

**Responsabilités** :
- Intercepter les appels IPC `ldn:u` des jeux
- Maintenir la machine à états LDN
- Exposer les événements de changement d'état

**Interface IPC** (identique à Nintendo):
```cpp
class IUserLocalCommunicationService {
    // Lifecycle
    Result Initialize(ClientProcessId pid);
    Result Finalize();

    // State
    Result GetState(Out<u32> state);
    Result AttachStateChangeEvent(OutCopyHandle event);

    // Network info
    Result GetNetworkInfo(OutBuffer<NetworkInfo> info);
    Result GetIpv4Address(Out<u32> addr, Out<u32> mask);
    Result GetDisconnectReason(Out<u32> reason);

    // Access Point (Host)
    Result OpenAccessPoint();
    Result CloseAccessPoint();
    Result CreateNetwork(CreateNetworkConfig config);
    Result SetAdvertiseData(InBuffer<u8> data);

    // Station (Client)
    Result OpenStation();
    Result CloseStation();
    Result Scan(Out<u32> count, OutBuffer<NetworkInfo> networks,
                u16 channel, ScanFilter filter);
    Result Connect(ConnectNetworkData data, NetworkInfo info);
    Result Disconnect();
};
```

#### 2.2.2 Protocol Translator

**Responsabilités** :
- Convertir les structures LDN Switch → RyuLdn
- Convertir les paquets RyuLdn → événements LDN
- Gérer le mapping des sessions/nodes

**Conversions clés** :

| LDN (Switch) | RyuLdn (Serveur) |
|--------------|------------------|
| `CreateNetwork()` | `CreateAccessPoint` packet |
| `Scan()` | `Scan` + `ScanReply[]` packets |
| `Connect()` | `Connect` + `Connected` packets |
| `NetworkInfo` | Struct identique (0x480 bytes) |
| `NodeInfo` | Struct identique |

#### 2.2.3 Network Client

**Responsabilités** :
- Maintenir connexion TCP persistante au serveur
- Gérer reconnexion avec backoff exponentiel
- Parser/encoder protocole RyuLdn binaire
- Ping/keepalive

**Machine à états connexion** :
```
┌─────────────┐     connect()     ┌─────────────┐
│ Disconnected│─────────────────►│ Connecting  │
└─────────────┘                   └──────┬──────┘
       ▲                                 │
       │ max retries                     │ success
       │                                 ▼
┌──────┴──────┐     error         ┌─────────────┐
│  Backoff    │◄──────────────────│  Connected  │
│  (waiting)  │                   └─────────────┘
└─────────────┘                          │
       │                                 │ server close
       │ timer                           │
       ▼                                 ▼
┌─────────────┐                   ┌─────────────┐
│  Retrying   │◄──────────────────│ Disconnecting│
└─────────────┘                   └─────────────┘
```

**Paramètres backoff** :
- Initial delay: 1 seconde
- Multiplier: 2x
- Max delay: 30 secondes
- Max retries: illimité (tant que jeu actif)

#### 2.2.4 Configuration Manager

**Fichier** : `/config/ryu_ldn_nx/config.ini`

```ini
[server]
; Adresse du serveur ryu_ldn
host = ldn.ryujinx.app
port = 30456

; Serveur de fallback (optionnel)
fallback_host =
fallback_port =

[network]
; Timeout connexion en ms
connect_timeout = 5000

; Intervalle ping en ms
ping_interval = 10000

[debug]
; Activer logs debug (0/1)
enabled = 0

; Niveau de log (0=off, 1=error, 2=warn, 3=info, 4=debug)
level = 1
```

#### 2.2.5 Tesla Overlay

**Fonctionnalités** :
- Afficher statut connexion (Connecté/Déconnecté/Erreur)
- Afficher nombre de joueurs dans session
- Permettre changement de serveur
- Afficher logs récents (mode debug)

---

## 3. Architecture des données

### 3.1 Structures partagées (compatibilité binaire)

Ces structures DOIVENT être identiques bit-pour-bit avec le serveur ryu_ldn :

```cpp
// Header protocole RyuLdn (10 bytes)
struct LdnHeader {
    u32 magic;      // 0x4E444C52 = "RLDN"
    u8 type;        // PacketId
    u8 version;     // 1
    u32 data_size;  // Taille payload
} __attribute__((packed));

// Info réseau (0x480 bytes)
struct NetworkInfo {
    NetworkId network_id;           // 32 bytes
    CommonNetworkInfo common;       // Variable
    LdnNetworkInfo ldn;            // Variable
} __attribute__((packed));

// Info noeud
struct NodeInfo {
    u32 ipv4_address;
    MacAddress mac_address;         // 6 bytes
    s8 node_id;                     // 0-7
    s8 is_connected;
    char user_name[33];
    s16 local_communication_version;
    u8 reserved[16];
} __attribute__((packed));
```

### 3.2 Types de paquets

```cpp
enum class PacketId : u8 {
    // Handshake
    Initialize = 0,
    Passphrase = 1,

    // Session management
    CreateAccessPoint = 2,
    CreateAccessPointPrivate = 3,
    Scan = 10,
    ScanReply = 11,
    ScanReplyEnd = 12,
    Connect = 13,
    ConnectPrivate = 14,
    Connected = 15,
    Disconnect = 16,

    // Data relay
    ProxyConfig = 17,
    ProxyConnect = 18,
    ProxyConnectReply = 19,
    ProxyData = 20,
    ProxyDisconnect = 21,

    // Config
    SetAcceptPolicy = 22,
    SetAdvertiseData = 23,

    // Lifecycle
    Ping = 254,
    NetworkError = 255,
};
```

### 3.3 Allocation mémoire

```cpp
// Allocation statique au démarrage
namespace memory {
    // Heap principal (1 MB)
    alignas(0x1000) u8 g_heap_buffer[1_MB];

    // Heap dynamique (128 KB)
    alignas(0x1000) u8 g_dynamic_heap[128_KB];

    // Buffers réseau (pré-alloués)
    alignas(64) u8 g_send_buffer[2048];
    alignas(64) u8 g_recv_buffer[4096];

    // Buffer NetworkInfo pour scan (8 max)
    NetworkInfo g_scan_results[8];

    // Config (lecture seule après init)
    Config g_config;
}
```

---

## 4. Flux de données

### 4.1 Flux: Création de partie (Host)

```
Jeu                    ryu_ldn_nx              Serveur
 │                         │                      │
 │ OpenAccessPoint()       │                      │
 ├────────────────────────►│                      │
 │                         │ [Connect TCP]        │
 │                         ├─────────────────────►│
 │                         │ Initialize           │
 │                         ├─────────────────────►│
 │                         │◄─────────────────────┤ Ack
 │◄────────────────────────┤                      │
 │ Result::Success         │                      │
 │                         │                      │
 │ CreateNetwork(config)   │                      │
 ├────────────────────────►│                      │
 │                         │ CreateAccessPoint    │
 │                         ├─────────────────────►│
 │                         │◄─────────────────────┤ GameId
 │◄────────────────────────┤                      │
 │ Result::Success         │                      │
 │                         │                      │
 │ SetAdvertiseData(data)  │                      │
 ├────────────────────────►│                      │
 │                         │ SetAdvertiseData     │
 │                         ├─────────────────────►│
 │◄────────────────────────┤                      │
```

### 4.2 Flux: Rejoindre une partie (Client)

```
Jeu                    ryu_ldn_nx              Serveur
 │                         │                      │
 │ OpenStation()           │                      │
 ├────────────────────────►│                      │
 │                         │ [Connect TCP]        │
 │                         ├─────────────────────►│
 │                         │ Initialize           │
 │                         ├─────────────────────►│
 │◄────────────────────────┤                      │
 │ Result::Success         │                      │
 │                         │                      │
 │ Scan(filter)            │                      │
 ├────────────────────────►│                      │
 │                         │ Scan                 │
 │                         ├─────────────────────►│
 │                         │◄─────────────────────┤ ScanReply[0]
 │                         │◄─────────────────────┤ ScanReply[1]
 │                         │◄─────────────────────┤ ScanReplyEnd
 │◄────────────────────────┤                      │
 │ networks[], count       │                      │
 │                         │                      │
 │ Connect(network)        │                      │
 ├────────────────────────►│                      │
 │                         │ Connect              │
 │                         ├─────────────────────►│
 │                         │◄─────────────────────┤ Connected
 │                         │◄─────────────────────┤ SyncNetwork
 │ StateChangeEvent        │                      │
 │◄────────────────────────┤                      │
 │ State = Connected       │                      │
```

### 4.3 Flux: Données de jeu (Proxy)

```
Jeu A                  ryu_ldn_nx A            Serveur            ryu_ldn_nx B          Jeu B
 │                         │                      │                     │                 │
 │ send(data)              │                      │                     │                 │
 ├────────────────────────►│                      │                     │                 │
 │                         │ ProxyData(to=B)      │                     │                 │
 │                         ├─────────────────────►│                     │                 │
 │                         │                      │ ProxyData(from=A)   │                 │
 │                         │                      ├────────────────────►│                 │
 │                         │                      │                     │ recv(data)      │
 │                         │                      │                     ├────────────────►│
```

---

## 5. Threads et synchronisation

### 5.1 Modèle de threads

| Thread | Priorité | Stack | Responsabilité |
|--------|----------|-------|----------------|
| Main | 42 | 16 KB | IPC service, état LDN |
| Network | 44 | 16 KB | Connexion TCP, I/O |
| Ping | 48 | 8 KB | Keepalive périodique |

### 5.2 Synchronisation

```cpp
// Événements kernel
os::SystemEvent state_change_event;    // Signalé sur changement état
os::SystemEvent network_event;         // Signalé sur réception paquet

// Mutex pour état partagé
os::Mutex state_mutex;                 // Protège CommState, NetworkInfo
os::Mutex send_mutex;                  // Protège buffer envoi

// Condition variables
os::ConditionVariable scan_complete;   // Attente fin scan
os::ConditionVariable connect_complete; // Attente connexion
```

### 5.3 Communication inter-threads

```
Main Thread                    Network Thread
     │                              │
     │ [state_mutex lock]           │
     │ request = SCAN               │
     │ [state_mutex unlock]         │
     │                              │
     │ [wait scan_complete]         │
     │                              │ [recv ScanReply]
     │                              │ [state_mutex lock]
     │                              │ results[] = ...
     │                              │ [state_mutex unlock]
     │                              │ [signal scan_complete]
     │                              │
     │ [wake up]                    │
     │ return results               │
```

---

## 6. Gestion des erreurs

### 6.1 Codes d'erreur

```cpp
namespace result {
    // Succès
    constexpr Result Success = 0;

    // Erreurs réseau
    constexpr Result NetworkUnavailable = 0x1234;  // Pas de WiFi
    constexpr Result ServerUnreachable = 0x1235;   // Serveur down
    constexpr Result ConnectionLost = 0x1236;      // Connexion perdue
    constexpr Result Timeout = 0x1237;             // Timeout

    // Erreurs protocole
    constexpr Result VersionMismatch = 0x1240;     // Version incompatible
    constexpr Result InvalidPacket = 0x1241;       // Paquet malformé
    constexpr Result SessionFull = 0x1242;         // Session pleine

    // Erreurs état
    constexpr Result InvalidState = 0x1250;        // Opération invalide dans cet état
    constexpr Result AlreadyConnected = 0x1251;    // Déjà connecté
}
```

### 6.2 Stratégie de récupération

| Erreur | Action |
|--------|--------|
| ServerUnreachable | Backoff + retry automatique |
| ConnectionLost | Reconnexion immédiate, notifier jeu si en session |
| VersionMismatch | Log erreur, refuser connexion, notifier utilisateur via overlay |
| InvalidPacket | Ignorer paquet, log warning |
| SessionFull | Retourner erreur au jeu, pas de retry |

---

## 7. Sécurité

### 7.1 Considérations

| Risque | Mitigation |
|--------|------------|
| Données sensibles | Pas de données utilisateur stockées localement |
| Man-in-the-middle | TLS optionnel (v2), pour MVP: faire confiance au serveur |
| DoS local | Rate limiting sur IPC (100 req/s max) |
| Buffer overflow | Validation stricte taille paquets |

### 7.2 Validation des entrées

```cpp
// Validation paquet reçu
bool validate_packet(const LdnHeader& header, size_t received) {
    // Magic valide
    if (header.magic != RLDN_MAGIC) return false;

    // Version supportée
    if (header.version != 1) return false;

    // Taille raisonnable
    if (header.data_size > MAX_PACKET_SIZE) return false;

    // Données complètes
    if (received < sizeof(LdnHeader) + header.data_size) return false;

    return true;
}
```

---

## 8. Structure du projet

### 8.1 Arborescence complète

```
ryu_ldn_nx/
│
├── .github/
│   ├── ISSUE_TEMPLATE/
│   │   ├── bug_report.md
│   │   ├── feature_request.md
│   │   └── config.yml
│   ├── PULL_REQUEST_TEMPLATE.md
│   ├── CODEOWNERS
│   ├── dependabot.yml
│   └── workflows/
│       ├── build.yml              # CI build + tests
│       ├── release.yml            # Auto-release sur tag
│       └── docs.yml               # Déploiement Starlight
│
├── .devcontainer/
│   ├── Dockerfile
│   └── devcontainer.json
│
├── docs/
│   ├── product-brief.md
│   ├── architecture.md            # Ce document
│   ├── research-notes.md
│   ├── codebase-analysis.md
│   └── sprint-status.yaml
│
├── sysmodule/
│   ├── source/
│   │   ├── main.cpp               # Entry point
│   │   ├── ldn_service.hpp
│   │   ├── ldn_service.cpp        # IPC service MITM
│   │   ├── protocol/
│   │   │   ├── ryu_protocol.hpp
│   │   │   ├── ryu_protocol.cpp   # Encodage/décodage RyuLdn
│   │   │   └── types.hpp          # Structures partagées
│   │   ├── network/
│   │   │   ├── client.hpp
│   │   │   ├── client.cpp         # Client TCP
│   │   │   ├── reconnect.hpp
│   │   │   └── reconnect.cpp      # Logique reconnexion
│   │   ├── config/
│   │   │   ├── config.hpp
│   │   │   └── config.cpp         # Parsing config.ini
│   │   └── debug/
│   │       ├── log.hpp
│   │       └── log.cpp            # Logging conditionnel
│   ├── res/
│   │   └── app.json               # Config NPDM
│   └── Makefile
│
├── overlay/
│   ├── source/
│   │   └── main.cpp               # Tesla overlay
│   └── Makefile
│
├── common/
│   └── include/
│       └── ryu_ldn_nx/
│           └── ipc.hpp            # Interface IPC partagée
│
├── tests/
│   ├── protocol_tests.cpp
│   ├── reconnect_tests.cpp
│   ├── config_tests.cpp
│   └── Makefile
│
├── config/
│   └── ryu_ldn_nx/
│       └── config.ini.example
│
├── starlight/                     # Documentation auto-générée
│   ├── astro.config.mjs
│   ├── package.json
│   └── src/
│       └── content/
│           └── docs/
│               └── ...            # Auto-généré depuis code
│
├── scripts/
│   ├── build.sh
│   ├── test.sh
│   └── release.sh
│
├── README.md
├── LICENSE                        # GPLv3
├── CONTRIBUTING.md
├── CODE_OF_CONDUCT.md
├── SECURITY.md
├── CHANGELOG.md
└── Makefile                       # Build principal
```

### 8.2 Fichiers contributeur GitHub

#### README.md
Structure attendue :
- Badges (build, license, version)
- Description courte
- Features
- Installation
- Configuration
- Building from source
- Contributing
- License

#### CONTRIBUTING.md
- Comment contribuer
- Style de code
- Process de PR
- **DCO Sign-off obligatoire**
- Tests requis

#### CODE_OF_CONDUCT.md
- Contributor Covenant v2.1 (standard)

#### SECURITY.md
- Comment reporter des vulnérabilités
- Scope de sécurité

#### ISSUE_TEMPLATE/
- `bug_report.md` : Template bug
- `feature_request.md` : Template feature
- `config.yml` : Configuration des templates

#### PULL_REQUEST_TEMPLATE.md
- Checklist PR
- Description des changements
- Tests effectués
- DCO sign-off reminder

### 8.3 Dépendances

| Dépendance | Version | Usage |
|------------|---------|-------|
| devkitPro | Latest | Toolchain ARM |
| libnx | Latest | APIs Switch |
| Atmosphere-libs | 1.0+ | Stratosphere framework |
| Tesla-Menu | Latest | Overlay |
| Starlight | Latest | Documentation site |

---

## 9. CI/CD

### 9.1 GitHub Actions Workflows

#### build.yml
```yaml
# Déclenché sur: push, PR
# Jobs:
#   - build-sysmodule: Compile sysmodule
#   - build-overlay: Compile overlay
#   - test: Run unit tests
#   - lint: Check code style
```

#### release.yml
```yaml
# Déclenché sur: tag v*
# Jobs:
#   - build: Compile release
#   - package: Créer ZIP pour SD
#   - release: Créer GitHub Release
```

#### docs.yml
```yaml
# Déclenché sur: push main
# Jobs:
#   - build-docs: Starlight build
#   - deploy: GitHub Pages
```

### 9.2 DCO Enforcement

Le CI vérifie automatiquement que tous les commits ont le sign-off :
```
Signed-off-by: Nom <email@example.com>
```

---

## 10. Plan de test

### 10.1 Tests unitaires

| Composant | Tests |
|-----------|-------|
| Protocol encoder | Encoding/decoding tous les types de paquets |
| Protocol decoder | Parsing paquets valides et invalides |
| Config parser | Parsing INI, valeurs par défaut, erreurs |
| Reconnect logic | États, backoff timing, max retries |

### 10.2 Tests d'intégration

| Scénario | Validation |
|----------|------------|
| Connexion serveur | Handshake complet, version négociée |
| Création partie | Host visible dans scan |
| Rejoindre partie | Connexion réussie, sync réseau |
| Déconnexion | Cleanup propre, état reset |
| Reconnexion | Recovery après perte connexion |

### 10.3 Tests de performance

| Métrique | Cible | Méthode |
|----------|-------|---------|
| Latence scan | < 500ms | Mesure temps Scan() |
| Latence connect | < 1s | Mesure temps Connect() |
| Mémoire idle | < 1.5 MB | Monitoring sysmodule |
| CPU idle | < 1% | Monitoring système |

---

## 11. Risques et mitigations

| Risque | Impact | Probabilité | Mitigation |
|--------|--------|-------------|------------|
| Changement protocole serveur | Élevé | Moyenne | Versioning, tests CI avec serveur |
| Performance insuffisante | Élevé | Faible | Profiling, optimisation itérative |
| Incompatibilité certains jeux | Moyen | Moyenne | Tests extensifs, logs détaillés |
| Stabilité long terme | Élevé | Moyenne | Tests stress, monitoring |

---

## 12. Décisions ouvertes / À valider

| Question | Options | Décision |
|----------|---------|----------|
| TLS pour connexion serveur | Oui/Non/Plus tard | À décider avec équipe serveur |
| Compression paquets | zlib/lz4/aucune | Suivre protocole existant |
| Logs persistants | Fichier SD / Mémoire seule | Mémoire seule (MVP) |

---

## Changelog

| Version | Date | Changements |
|---------|------|-------------|
| 1.0 | 2026-01-12 | Version initiale |

---

*Document généré via workflow BMM `/create-architecture`*
