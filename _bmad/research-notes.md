# Research Notes: ryu_ldn_nx

**Date**: 2026-01-12
**Project**: Port de ryu_ldn vers Nintendo Switch native

---

## Résumé exécutif

Ce document couvre les exigences techniques et contraintes pour porter ryu_ldn (implémentation réseau LDN de Ryujinx) en sysmodule natif Nintendo Switch.

---

## 1. Contraintes Hardware

### 1.1 Modèles Nintendo Switch

| Modèle | Codename | SoC | CPU | RAM | Sortie |
|--------|----------|-----|-----|-----|--------|
| Switch originale (HAC-001) | Erista | Tegra X1 (T210) | 4x ARM Cortex-A57 @ 1.02 GHz | 4 GB LPDDR4 | Mars 2017 |
| Switch Lite (HDH-001) | Mariko Lite | Tegra X1+ (T214) | 4x ARM Cortex-A57 @ 1.02 GHz | 4 GB LPDDR4 | Sept 2019 |
| Switch OLED (HEG-001) | Mariko | Tegra X1+ (T214) | 4x ARM Cortex-A57 @ 1.02 GHz | 4 GB LPDDR4 | Oct 2021 |

**Note critique**: La Switch originale (Erista/T210) est la plus faible et la plus commune. Toute optimisation doit cibler cette baseline.

### 1.2 Contraintes CPU

**Modes de fréquence**:
- **Mode portable**: CPU @ 1020 MHz, GPU @ 307.2-384 MHz
- **Mode dock**: CPU @ 1020 MHz, GPU @ 307.2-768 MHz
- **Mode boost** (spécifique au jeu): CPU jusqu'à 1785 MHz (rarement disponible)

**Accès CPU sysmodule**:
- Sysmodules tournent typiquement sur **CPU core 3**
- Priorité thread par défaut: 42 (plus bas = plus haute priorité, range 0-63)
- Priorité recommandée pour sysmodules réseau: 40-50

### 1.3 Contraintes mémoire

**Mémoire système totale**:
- **4 GB LPDDR4** RAM totale
- **~3.5 GB** disponible après réservations hardware
- **~2.5-3 GB** typiquement disponible pour les jeux
- **~500 MB - 1 GB** pour services système et OS

**Budgets mémoire sysmodule** (basé sur analyse ldn_mitm):

| Composant | Allocation typique | Maximum safe |
|-----------|-------------------|--------------|
| Heap statique (g_malloc_buffer) | 1 MB | 2 MB |
| Heap dynamique (g_heap_memory) | 128 KB | 256 KB |
| Stack thread | 16 KB (0x4000) par thread | 32 KB |
| Mémoire transfert socket | ~64 KB | 128 KB |
| **Total footprint sysmodule** | **~1.5 MB** | **~3 MB** |

**Usage mémoire actuel ldn_mitm** (depuis source):
```cpp
constexpr size_t MallocBufferSize = 1_MB;           // 1 MB allocation statique
alignas(os::MemoryPageSize) u8 g_heap_memory[128_KB]; // 128 KB heap
const size_t ThreadStackSize = 0x4000;               // 16 KB par thread
```

### 1.4 Contraintes performance pour processus background

- **Usage CPU cible**: <1% moyen, <5% pic pour sysmodule
- **Mémoire cible**: <2 MB footprint total
- **Latence cible**: <10ms pour traitement paquet réseau
- **Wake-ups cible**: Event-driven, pas de polling (sauf si requis)

---

## 2. Protocoles réseau Nintendo

### 2.1 Protocole LDN (Local wireless)

**États LDN**:
```cpp
enum class CommState {
    None,               // Non initialisé
    Initialized,        // Service initialisé, non connecté
    AccessPoint,        // Création réseau (hôte préparant)
    AccessPointCreated, // Réseau créé (hôte prêt)
    Station,            // Rejoindre réseau (client préparant)
    StationConnected,   // Connecté au réseau (client prêt)
    Error               // État erreur
};
```

**Constantes LDN**:
```cpp
const size_t SsidLengthMax = 32;
const size_t AdvertiseDataSizeMax = 384;
const size_t UserNameBytesMax = 32;
const int NodeCountMax = 8;           // Joueurs max par session
const int StationCountMax = 7;        // Clients max (NodeCountMax - 1 hôte)
const size_t PassphraseLengthMax = 64;
```

### 2.2 Protocole mode LAN (ldn_mitm)

**Structure paquet**:
```cpp
struct LANPacketHeader {
    u32 magic;           // 0x11451400 (LANMagic)
    LANPacketType type;  // Scan, ScanResp, Connect, SyncNetwork
    u8 compressed;       // Flag compression
    u16 length;          // Longueur payload
    u16 decompress_length;
    u8 _reserved[2];
};
```

**Topologie réseau**:
- **Port UDP 11452**: Discovery/scanning (broadcast)
- **Port TCP 11452**: Communication session jeu (point-à-point)

### 2.3 Protocole ryu_ldn (Protocole serveur Ryujinx)

**Header protocole**:
```cpp
struct LdnHeader {
    uint Magic;      // 'RLDN' = 0x4E444C52
    byte Type;       // PacketId enum
    byte Version;    // Version protocole (1)
    int DataSize;    // Taille payload
}; // 10 bytes
```

**Types de paquets clés**:
- Initialize, Passphrase (handshake client)
- CreateAccessPoint, Scan, Connect (gestion session)
- ProxyConfig, ProxyData, ProxyDisconnect (relais données jeu)
- Ping, NetworkError (lifecycle)

**Différences clés avec ldn_mitm**:

| Feature | ldn_mitm | ryu_ldn |
|---------|----------|---------|
| Discovery | Broadcast UDP local | Serveur central |
| Transport | TCP P2P direct | Relayé par serveur |
| NAT Traversal | Requiert accès LAN | Géré par serveur |
| Taille max paquet | 2048 bytes | 131072 bytes |

---

## 3. Développement Switchbrew

### 3.1 Configuration sysmodule (app.json)

```json
{
    "name": "ldn.mitm",
    "title_id": "0x4200000000000010",
    "main_thread_stack_size": "0x20000",  // 128 KB
    "main_thread_priority": 42,
    "default_cpu_id": 3,
    "process_category": 1,
    "pool_partition": 2,
    "is_64_bit": true
}
```

### 3.2 Mécanismes IPC

**Stratosphere Server Manager**:
```cpp
struct LdnMitmManagerOptions {
    static constexpr size_t PointerBufferSize = 0x1000;  // 4 KB
    static constexpr size_t MaxDomains = 0x10;           // 16
    static constexpr size_t MaxDomainObjects = 0x100;    // 256
    static constexpr bool CanManageMitmServers = true;
};
constexpr size_t MaxSessions = 3;
```

### 3.3 Bonnes pratiques allocation mémoire

1. **Pré-allouer toute la mémoire au démarrage** - éviter allocations runtime
2. **Utiliser buffers statiques** pour structures taille fixe
3. **Allocation pool** pour objets taille variable
4. **Éviter std::vector, std::string** avec allocation dynamique
5. **Override global new/delete** pour utiliser heap contrôlé

### 3.4 APIs réseau

**Configuration BSD Socket**:
```cpp
const ::SocketInitConfig LibnxSocketInitConfig = {
    .tcp_tx_buf_size = 0x800,      // 2 KB
    .tcp_rx_buf_size = 0x1000,     // 4 KB
    .tcp_tx_buf_max_size = 0x2000, // 8 KB
    .tcp_rx_buf_max_size = 0x2000, // 8 KB
    .udp_tx_buf_size = 0x2000,     // 8 KB
    .udp_rx_buf_size = 0x2000,     // 8 KB
    .sb_efficiency = 4,
    .num_bsd_sessions = 3,
};
```

**Syscalls essentiels pour sysmodule réseau**:
```cpp
// Mémoire
svcSetHeapSize, svcMapMemory, svcUnmapMemory, svcQueryMemory

// Threading
svcCreateThread, svcStartThread, svcExitThread, svcSleepThread
svcWaitSynchronization

// IPC
svcConnectToNamedPort, svcSendSyncRequest, svcReplyAndReceive

// Events
svcCreateEvent, svcSignalEvent, svcClearEvent, svcResetSignal
```

---

## 4. Intégration Atmosphere CFW

### 4.1 Structure répertoire

```
/atmosphere/contents/<title_id>/
  exefs/
    main                  # NSO compilé
    main.npdm            # Metadata processus
  flags/
    boot2.flag           # Auto-start au boot
```

### 4.2 Conventions Title ID

- `0100000000000xxx`: Titres système Nintendo
- `010000000000xxxx`: Modules système Nintendo
- `0420000000000xxx`: Sysmodules Atmosphere (mitm)
- `0100000000010xxx`: Sysmodules homebrew utilisateur

### 4.3 Pattern service Mitm

```cpp
// 1. Définir interface service mitm
class LdnMitMService {
public:
    Result GetState(sf::Out<u32> state);
    Result Scan(...);
};

// 2. Enregistrer comme mitm
sm::ServiceName name = sm::ServiceName::Encode("ldn:u");
server_manager.RegisterMitmServer<LdnMitMService>(port, name);

// 3. Logique décision mitm
static bool ShouldMitm(const sm::MitmProcessInfo &client_info) {
    return true; // Mitm tout par défaut
}
```

### 4.4 Limites ressources sysmodules

| Ressource | Limite typique | Maximum hard |
|-----------|----------------|--------------|
| Mémoire | 2-4 MB | 8 MB |
| Threads | 8-16 | 32 |
| Sessions | 4-8 | 16 |
| Handles | 128 | 512 |

---

## 5. Recommandations implémentation

### 5.1 Architecture proposée

```
ryu_ldn_nx/sysmodule/
  source/
    main.cpp              # Point d'entrée, enregistrement service
    ryu_protocol.hpp/cpp  # Implémentation protocole ryu_ldn
    ldn_service.hpp/cpp   # Interface service IPC
    network_client.hpp/cpp # Gestion connexion serveur
    types.hpp             # Définitions types partagés
```

### 5.2 Décisions implémentation clés

1. **Couche traduction protocole**: Convertir protocole local ldn_mitm vers protocole serveur ryu_ldn
2. **Stratégie connexion**: TCP persistant unique vers serveur ryu_ldn avec reconnexion auto
3. **Budget mémoire**: 1 MB statique + 128 KB dynamique + 64 KB sockets = ~1.5 MB total
4. **Modèle thread**: Thread principal (IPC) + Thread réseau (communication serveur)

### 5.3 Stratégies optimisation

**Mémoire**:
- Éliminer allocation dynamique (utiliser arrays statiques)
- Utiliser pools objets taille fixe
- Éviter opérations string

**CPU**:
- Event-driven plutôt que polling
- Batch opérations où possible
- Utiliser structures données linéaires efficaces

**Réseau**:
- Protocole binaire (pas JSON)
- TCP_NODELAY pour faible latence
- Keepalive pour monitoring connexion

### 5.4 Facteurs de risque

| Risque | Mitigation |
|--------|------------|
| Épuisement mémoire | Allocation statique, budgets stricts |
| Latence réseau | Protocole efficace, cache local |
| Indisponibilité serveur | Dégradation gracieuse |
| Overhead CPU | Event-driven, optimiser hot paths |

---

## 6. Références

### Sources primaires
- [Switchbrew Wiki - Hardware](https://switchbrew.org/wiki/Hardware)
- [Switchbrew Wiki - Main Page](https://switchbrew.org/wiki/Main_Page)
- [NintendoClients Wiki](https://github.com/kinnay/NintendoClients/wiki)
- [Atmosphere GitHub](https://github.com/Atmosphere-NX/Atmosphere)
- [ldn_mitm Source](https://github.com/spacemeowx2/ldn_mitm)
- [Ryujinx LDN Implementation](https://git.ryujinx.app/ryubing/ryujinx)

### Documentation technique
- [libnx Documentation](https://switchbrew.github.io/libnx/index.html)
- [devkitPro Documentation](https://devkitpro.org/wiki/Getting_Started)

---

*Document généré via workflow BMM `/research`*
