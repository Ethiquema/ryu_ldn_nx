# Codebase Analysis: ryu_ldn_nx

**Date**: 2026-01-12
**Project**: Port de ryu_ldn vers Nintendo Switch native

---

## Vue d'ensemble

Ce document analyse les trois codebases pré-clonées qui forment la base du projet ryu_ldn_nx:

1. **ldn_mitm** - Sysmodule de référence fonctionnel sur Switch
2. **ryujinx** - Émulateur contenant l'implémentation ryu_ldn à porter
3. **ldn** - Serveur LAN play pour ryuldn

---

## 1. LDN_MITM: Implémentation Switch de référence

### 1.1 Architecture

**Localisation**: `/home/gabriel/GIT/ryu_ldn_nx/ldn_mitm/`

**Framework**: Atmosphere Stratosphere (libstratosphere)

**Structure**:
```
ldn_mitm/ldn_mitm/source/
├── ldnmitm_main.cpp          # Point d'entrée, threading
├── ldn_icommunication.cpp    # Implémentation service IPC
├── lan_discovery.cpp         # Machine d'état LAN (~24KB)
├── lan_protocol.cpp          # I/O Socket (~8KB)
├── ldn_types.cpp             # Conversions de données
└── debug.cpp                 # Logging
```

### 1.2 Service IPC

**Service mitm**: `ldn:u` (IUserLocalCommunicationService)

**Commandes principales**:
| ID | Commande | Description |
|----|----------|-------------|
| 0 | GetState | État actuel du réseau |
| 1 | GetNetworkInfo | Infos réseau complètes |
| 100 | Initialize | Initialisation service |
| 200 | OpenAccessPoint | Ouvrir mode hôte |
| 201 | CloseAccessPoint | Fermer mode hôte |
| 202 | CreateNetwork | Créer réseau |
| 203 | Destroy | Détruire réseau |
| 206 | Scan | Scanner réseaux |
| 300 | OpenStation | Ouvrir mode client |
| 301 | CloseStation | Fermer mode client |
| 302 | Connect | Rejoindre réseau |

### 1.3 Composants clés

#### LANDiscovery (lan_discovery.cpp)

Machine d'état centrale:
```cpp
enum class CommState {
    None,               // Non initialisé
    Initialized,        // Service initialisé
    AccessPoint,        // Création réseau (hôte)
    AccessPointCreated, // Réseau créé (hôte prêt)
    Station,            // Rejoindre réseau (client)
    StationConnected,   // Connecté (client prêt)
    Error               // État erreur
};
```

#### Protocole LAN (lan_protocol.cpp)

**Format de paquet**:
```cpp
struct LANPacketHeader {
    u32 magic;           // 0x11451400
    LANPacketType type;  // Scan, ScanResp, Connect, SyncNetwork
    u8 compressed;       // Flag compression (zlib)
    u16 length;          // Taille payload
    u16 decompress_length;
    u8 _reserved[2];
};
```

**Taille max paquet**: 2048 bytes

### 1.4 Gestion mémoire

```cpp
constexpr size_t MallocBufferSize = 1_MB;           // 1 MB allocation statique
alignas(os::MemoryPageSize) u8 g_heap_memory[128_KB]; // 128 KB heap
const size_t ThreadStackSize = 0x4000;               // 16 KB par thread
```

**Total footprint**: ~1.5 MB

---

## 2. RYUJINX: Implémentation émulateur LDN

### 2.1 Architecture

**Localisation**: `/home/gabriel/GIT/ryu_ldn_nx/ryujinx/src/Ryujinx.HLE/HOS/Services/Ldn/`

**Langage**: C#

**Backends disponibles**:
- **LdnRyu**: Connexion directe au serveur LAN play (ryu_ldn natif)
- **LdnMitm**: Pont vers service ldn_mitm

### 2.2 Structure

```
Ldn/
├── IUserServiceCreator.cs              # Factory service
├── LdnConst.cs                         # Constantes
├── ResultCode.cs                       # Codes résultat IPC
├── Types/                              # Structures de données
│   ├── NetworkInfo.cs
│   ├── NodeInfo.cs
│   └── [20+ définitions de types]
│
└── UserServiceCreator/
    ├── IUserLocalCommunicationService.cs  # Service LDN principal
    ├── INetworkClient.cs               # Interface client réseau
    │
    ├── LdnRyu/                         # Mode serveur natif
    │   ├── LdnMasterProxyClient.cs     # Client TCP vers serveur
    │   ├── RyuLdnProtocol.cs          # Sérialisation protocole
    │   └── Proxy/                      # Gestion proxy P2P
    │
    └── LdnMitm/                        # Mode pont MITM
        └── LdnMitmClient.cs           # Client IPC vers ldn_mitm
```

### 2.3 Protocole RyuLdn

**Header protocole** (10 bytes):
```csharp
[StructLayout(LayoutKind.Sequential, Size = 0xA)]
public struct LdnHeader {
    public uint Magic;       // "RLDN" (0x524C444E)
    public byte Type;        // PacketId
    public byte Version;     // 1
    public int DataSize;     // 0 - 131,072 bytes
}
```

**Types de paquets**:
```csharp
public enum PacketId : byte {
    // Client → Serveur
    Initialize = 0,
    Passphrase,
    CreateAccessPoint,
    Scan, ScanReply, ScanReplyEnd,
    Connect, Connected, Disconnect,

    // Proxy
    ProxyConfig = 17,
    ProxyConnect, ProxyConnectReply,
    ProxyData, ProxyDisconnect,

    // Lifecycle
    Ping = 254,
    NetworkError = 255
}
```

### 2.4 Client LdnMasterProxyClient

**Base**: `TcpClient` (NetCoreServer library)

**Responsabilités**:
- Connexion TCP persistante au serveur LAN play
- Échange de messages protocole
- Setup serveur proxy P2P (optionnel)
- Gestion timeouts (6s inactivité, 4s échec, 1s scan)

---

## 3. LDN: Serveur standalone

### 3.1 Architecture

**Localisation**: `/home/gabriel/GIT/ryu_ldn_nx/ldn/`

**Langage**: C# (.NET Core)

**Fonctionnalités**:
- Serveur master pour clients ryu_ldn
- Gestion des lobbies/sessions de jeu
- Services proxy pour connexions P2P
- Analytics publiées vers Redis

### 3.2 Structure

```
LdnServer/
├── Program.cs                      # Point d'entrée
├── LdnServer.cs                    # Serveur TCP principal
├── HostedGame.cs                   # État session de jeu
├── BanList.cs                      # Bans IP
│
├── Session/
│   ├── LdnSession.cs              # Session par client
│   └── Handlers/                   # Gestionnaires de messages
│       ├── Initialize.cs
│       ├── CreateAccessPoint.cs
│       ├── Scan.cs
│       ├── Connect.cs
│       └── [autres handlers]
│
├── Network/
│   ├── RyuLdnProtocol.cs          # Parser protocole
│   └── VirtualDhcp.cs             # Simulateur DHCP
│
└── Stats/
    └── Statistics.cs              # Analytics serveur
```

### 3.3 Configuration

**Variables d'environnement**:
```
LDN_HOST         → Adresse bind (défaut: 0.0.0.0)
LDN_PORT         → Port serveur (défaut: 30456)
LDN_GAMELIST_PATH → JSON avec titres de jeux
LDN_REDIS_HOST   → Hôte Redis pour analytics
```

### 3.4 Réseau virtuel

```csharp
private const uint NetworkBaseAddress = 0x0a720000;  // 10.114.0.0
private const uint NetworkSubnetMask = 0xffff0000;   // 255.255.0.0
```

Chaque joueur reçoit:
- NodeId (0-7)
- IP dans la plage 10.114.0.0/16
- Node 0 = hôte (AP)

---

## 4. Matrice de compatibilité protocole

| Composant | Protocole | Langage | Classes clés |
|-----------|----------|----------|--------------|
| ldn_mitm | LAN UDP + TCP | C++ (Stratosphere) | LANDiscovery, LanSocket |
| Ryujinx | RyuLdn (TCP binaire) | C# | RyuLdnProtocol, LdnMasterProxyClient |
| ldn Server | RyuLdn (TCP binaire) | C# | RyuLdnProtocol, LdnServer |

---

## 5. Flux de communication complet

```
┌─────────────┐              ┌──────────────┐              ┌────────────────┐
│  Switch     │              │  Ryujinx     │              │  ldn Server    │
│ (ldn_mitm)  │              │  Emulator    │              │  (ryu_ldn)     │
└──────┬──────┘              └──────┬───────┘              └────────┬───────┘
       │                             │                              │
       │  [A] UDP Scan Broadcast     │                              │
       ├────────────────────────────►│                              │
       │                             │ [B] TCP Connect to Server    │
       │                             ├─────────────────────────────►│
       │                             │  InitializeMessage           │
       │                             │◄─────────────────────────────┤
       │                             │ [C] CreateAccessPointReq     │
       │                             ├─────────────────────────────►│
       │                             │◄─────────────────────────────┤
       │                             │  HostedGame Created          │
       │ [D] Broadcast ScanReq       │                              │
       │◄────────────────────────────┤                              │
       │ [E] ScanResp w/ NetworkInfo │                              │
       ├────────────────────────────►│ [F] ScanReply Packet         │
       │                             ├─────────────────────────────►│
       │ [G] Application sélectionne │                              │
       │ [H] ConnectRequest          │                              │
       │◄────────────────────────────┤                              │
       │                             │ [I] ConnectRequest           │
       │                             ├─────────────────────────────►│
       │                             │◄─────────────────────────────┤
       │                             │  NodeId assigned             │
       │ [J] SyncNetwork             │                              │
       │◄────────────────────────────┤◄─────────────────────────────┤
       │                             │ [K] Connected State          │
       │  [L] Game Data Flow         │                              │
       └─────────────────────────────►─────────────────────────────►│
```

---

## 6. Considérations critiques pour le portage

### 6.1 Défis principaux

1. **Empreinte mémoire**
   - ldn_mitm utilise ~1.5 MB
   - Éliminer le heap managé C# → C++
   - Code proxy doit utiliser allocation stack

2. **Modèle de threading**
   - ldn_mitm: threading Stratosphere (simple)
   - Pas de async/await .NET (runtime managé)
   - I/O basé polling avec primitives Stratosphere

3. **API Socket**
   - Utiliser service BSD Switch (via stratosphere/libnx)
   - Pas de sockets POSIX standard

4. **Compatibilité protocole**
   - RyuLdnProtocol doit rester identique (compatibilité binaire)
   - Layout struct NetworkInfo fixé (0x480 bytes)
   - Pas de changement de sérialisation

### 6.2 Structure cible pour ryu_ldn_nx

**Structure ldn_mitm** (référence fonctionnelle Switch):
```
ldn_mitm/ldn_mitm/source/
├── ldnmitm_main.cpp          # Entrée: ~10KB
├── ldn_icommunication.cpp    # Impl service IPC: ~7KB
├── lan_discovery.cpp         # Machine d'état: ~24KB
├── lan_protocol.cpp          # Socket I/O: ~8KB
└── ldn_types.cpp             # Conversions: ~1KB
```

**Structure cible ryu_ldn_nx** (C++ sur Switch, porté depuis Ryujinx C#):
- LdnMasterProxyClient → Client UDP connecteur + récepteur TCP
- RyuLdnProtocol → Parser protocole binaire (préserver exactement)
- Proxy handling → Forwarding P2P/Master proxy
- Session management → Tracking état par connexion

---

## Conclusion

Le portage ryu_ldn_nx nécessite l'intégration de trois implémentations distinctes:

1. **ldn_mitm** fournit l'architecture sysmodule de référence mais utilise un protocole UDP (inadapté aux serveurs internet publics)

2. **LdnRyu de Ryujinx** implémente le protocole TCP binaire nécessaire pour la communication serveur master mais en C# managé

3. **ldn Server** démontre la gestion complète des sessions côté serveur avec analytics mais tourne uniquement sur .NET Core

La tâche de portage implique **d'extraire la sérialisation protocole de Ryujinx et de l'adapter à la couche socket C++ de ldn_mitm** tout en maintenant l'efficacité mémoire pour les contraintes hardware Switch.

---

*Document généré via workflow BMM `/document-project`*
