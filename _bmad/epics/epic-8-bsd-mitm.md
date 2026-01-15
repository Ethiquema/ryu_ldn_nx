# Epic 8 : BSD Socket MITM (Proxy Data Routing)

**Priorité** : P0 (Critique - Bloquant pour MVP)
**Estimation** : 3-4 sprints
**Dépendances** : Epic 3 (LDN Service)

## Description

Implémenter un service MITM pour `bsd:u`/`bsd:s` qui intercepte les appels socket des jeux et route le trafic via le protocole RyuLdn `ProxyData` au lieu de vrais sockets réseau. C'est nécessaire car les jeux Nintendo utilisent des sockets BSD après la création du réseau LDN, et ces sockets doivent être redirigés vers le serveur Ryujinx.

## Problème actuel

Après analyse des logs, le flux actuel est :

1. ✅ `CreateNetwork` réussit - room créée sur le serveur
2. ✅ `GetIpv4Address` retourne l'IP proxy (10.114.0.1)
3. ❌ Le jeu essaie de créer des sockets avec l'IP 10.114.x.x
4. ❌ Les sockets échouent car le réseau 10.114.x.x n'existe pas
5. ❌ Le jeu appelle `CloseAccessPoint()` car la communication échoue

## Valeur utilisateur

> En tant que **joueur Switch**, je veux que les données de jeu (gameplay, synchronisation) transitent correctement entre ma console et les autres joueurs via le serveur Ryujinx, pour que je puisse jouer en multijoueur.

## Objectifs

- [ ] Service MITM `bsd:u` fonctionnel
- [ ] Détection des sockets destinés au réseau LDN (10.114.x.x)
- [ ] Sockets proxy qui routent via `ProxyData`
- [ ] Support UDP et TCP
- [ ] Tests avec Mario Kart 8 Deluxe

---

## Recherche - Architecture Ryujinx LdnProxy

### Fichiers analysés

- `/tmp/ryujinx-client/src/Ryujinx.HLE/HOS/Services/Ldn/UserServiceCreator/LdnRyu/Proxy/LdnProxy.cs`
- `/tmp/ryujinx-client/src/Ryujinx.HLE/HOS/Services/Ldn/UserServiceCreator/LdnRyu/Proxy/LdnProxySocket.cs`

### Architecture Ryujinx

```
┌─────────────────────────────────────────────────────────────────┐
│ Game Code                                                       │
├─────────────────────────────────────────────────────────────────┤
│ BSD Service (ISocketImpl interface)                             │
├─────────────────────────────────────────────────────────────────┤
│ ┌─────────────────────┐    ┌──────────────────────────────────┐│
│ │ Real Socket         │ OR │ LdnProxySocket                   ││
│ │ (normal traffic)    │    │ (LDN proxy traffic → 10.114.x.x) ││
│ └─────────────────────┘    └──────────────────────────────────┘│
├─────────────────────────────────────────────────────────────────┤
│ LdnProxy (manages proxy sockets, routes ProxyData)             │
├─────────────────────────────────────────────────────────────────┤
│ RyuLdnProtocol (encode/decode ProxyData packets)               │
├─────────────────────────────────────────────────────────────────┤
│ TCP Connection to Ryujinx LDN Server                           │
└─────────────────────────────────────────────────────────────────┘
```

### Classes clés Ryujinx

#### LdnProxy.cs

```csharp
class LdnProxy : IDisposable
{
    // Configuration
    private readonly uint _subnetMask;     // 0xFFFF0000
    private readonly uint _localIp;        // 0x0A72xxxx (10.114.x.x)
    private readonly uint _broadcast;      // _localIp | (~_subnetMask)

    // Socket management
    private readonly List<LdnProxySocket> _sockets = [];
    private readonly Dictionary<ProtocolType, EphemeralPortPool> _ephemeralPorts = new();

    // Packet handlers
    void HandleData(LdnHeader header, ProxyDataHeader proxyHeader, byte[] data)
    {
        // Route incoming data to matching socket based on dest port/protocol
        ForRoutedSockets(proxyHeader.Info, (socket) => {
            socket.IncomingData(packet);
        });
    }

    // Send data via ProxyData packet
    int SendTo(ReadOnlySpan<byte> buffer, SocketFlags flags,
               IPEndPoint localEp, IPEndPoint remoteEp, ProtocolType type)
    {
        ProxyDataHeader request = new() {
            Info = MakeInfo(localEp, remoteEp, type),
            DataLength = (uint)buffer.Length
        };
        _parent.SendAsync(_protocol.Encode(PacketId.ProxyData, request, buffer.ToArray()));
        return buffer.Length;
    }
}
```

#### LdnProxySocket.cs

```csharp
class LdnProxySocket : ISocketImpl
{
    // Implements full socket interface
    public int Send(ReadOnlySpan<byte> buffer) => SendTo(buffer, SocketFlags.None, RemoteEndPoint);
    public int Receive(Span<byte> buffer) => ReceiveFrom(buffer, SocketFlags.None, ref dummy);

    // Data is queued from incoming ProxyData packets
    private readonly Queue<ProxyDataPacket> _receiveQueue = new();

    public void IncomingData(ProxyDataPacket packet)
    {
        lock (_receiveQueue) {
            _receiveQueue.Enqueue(packet);
        }
    }

    // Send routes through LdnProxy
    public int SendTo(ReadOnlySpan<byte> buffer, SocketFlags flags, EndPoint remoteEP)
    {
        return _proxy.SendTo(buffer, flags, localEp, (IPEndPoint)remoteEP, ProtocolType);
    }
}
```

### Protocole ProxyData

```
┌─────────────────────────────────────────────────────────────────┐
│ LdnHeader (12 bytes)                                            │
├─────────────────────────────────────────────────────────────────┤
│ magic: 0x4E444C52 ("RLDN")                                      │
│ type: PacketId::ProxyData (20)                                  │
│ version: 5                                                      │
│ reserved: 0                                                     │
│ data_size: sizeof(ProxyDataHeader) + payload_length             │
├─────────────────────────────────────────────────────────────────┤
│ ProxyDataHeader (20 bytes)                                      │
├─────────────────────────────────────────────────────────────────┤
│ ProxyInfo (16 bytes):                                           │
│   source_ipv4: uint32   (e.g., 0x0A720001 = 10.114.0.1)        │
│   source_port: uint16   (e.g., 49152)                          │
│   dest_ipv4: uint32     (e.g., 0x0A720002 = 10.114.0.2)        │
│   dest_port: uint16     (e.g., 30000)                          │
│   protocol: uint32      (UDP=17, TCP=6)                        │
│ data_length: uint32     (payload size)                         │
├─────────────────────────────────────────────────────────────────┤
│ Payload (variable)                                              │
│ [game data bytes...]                                            │
└─────────────────────────────────────────────────────────────────┘
```

---

## Recherche - BSD Service Nintendo Switch

### Services à intercepter

| Service | Description | Utilisé par |
|---------|-------------|-------------|
| `bsd:u` | BSD sockets (user mode) | Jeux |
| `bsd:s` | BSD sockets (system mode) | Services système |

### Interface IPC BSD

Source: [switchbrew.org/wiki/Sockets_services](https://switchbrew.org/wiki/Sockets_services)

| Cmd | Name | Description |
|-----|------|-------------|
| 0 | RegisterClient | Initialize client |
| 1 | StartMonitoring | Start socket monitoring |
| 2 | Socket | Create socket |
| 3 | SocketExempt | Create exempt socket |
| 4 | Open | Open socket |
| 5 | Select | Select on sockets |
| 6 | Poll | Poll sockets |
| 7 | Sysctl | System control |
| 8 | Recv | Receive data |
| 9 | RecvFrom | Receive with address |
| 10 | Send | Send data |
| 11 | SendTo | Send to address |
| 12 | Accept | Accept connection |
| 13 | Bind | Bind to address |
| 14 | Connect | Connect to address |
| 15 | GetPeerName | Get peer address |
| 16 | GetSockName | Get socket address |
| 17 | GetSockOpt | Get socket option |
| 18 | Listen | Listen for connections |
| 19 | Ioctl | I/O control |
| 20 | Fcntl | File control |
| 21 | SetSockOpt | Set socket option |
| 22 | Shutdown | Shutdown socket |
| 23 | ShutdownAllSockets | Shutdown all |
| 24 | Write | Write data |
| 25 | Read | Read data |
| 26 | Close | Close socket |
| 27 | DuplicateSocket | Duplicate socket |

### Stratégie d'interception

Pour intercepter les sockets LDN :

1. **Créer MITM pour `bsd:u`**
2. **Intercepter `Socket()`** : Créer un fd réel OU un fd proxy selon le contexte
3. **Intercepter `Bind()`** : Si l'adresse est 10.114.x.x, marquer comme socket proxy
4. **Intercepter `Connect()`** : Si destination est 10.114.x.x, envoyer `ProxyConnect`
5. **Intercepter `Send/SendTo()`** : Router via `ProxyData`
6. **Intercepter `Recv/RecvFrom()`** : Lire depuis le buffer proxy
7. **Intercepter `Close()`** : Cleanup du socket proxy

---

## Recherche - ldn_mitm de spacemeowx2

### Différence d'architecture

| Aspect | spacemeowx2/ldn_mitm | ryu_ldn_nx (notre projet) |
|--------|---------------------|---------------------------|
| Réseau cible | LAN local | Serveur Ryujinx distant |
| Protocole | UDP broadcast + TCP direct | TCP via serveur + ProxyData |
| Sockets | Vrais sockets vers LAN | Sockets proxy vers serveur |
| Complexité | Moyenne | Élevée |

### Leur approche (pour référence)

- N'interceptent PAS le service BSD
- Utilisent des vrais sockets qui communiquent sur le LAN local
- `lanDiscovery` gère la découverte via UDP broadcast
- Fonctionne uniquement si les consoles sont sur le même réseau

### Notre approche (nécessaire)

- DEVONS intercepter le service BSD
- Créer des sockets virtuels qui routent via le serveur
- Le réseau 10.114.x.x est virtuel, pas réel
- Permet le jeu via Internet, pas seulement LAN

---

## Architecture proposée

```
┌─────────────────────────────────────────────────────────────────┐
│ Game                                                            │
├─────────────────────────────────────────────────────────────────┤
│ BSD Service MITM (bsd:u)                                        │
│ ┌─────────────────────────────────────────────────────────────┐ │
│ │ BsdMitmService                                              │ │
│ │ - Intercepts all BSD IPC calls                              │ │
│ │ - Routes to real socket OR proxy socket based on IP         │ │
│ └─────────────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│ ┌──────────────────────┐    ┌────────────────────────────────┐ │
│ │ RealSocketHandler    │    │ ProxySocketHandler             │ │
│ │ - Normal internet    │    │ - 10.114.x.x addresses         │ │
│ │ - Forwards to real   │    │ - Routes via ProxyData         │ │
│ │   bsd:u service      │    │ - Manages proxy sockets        │ │
│ └──────────────────────┘    └────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│ LdnProxy (shared with LDN MITM)                                 │
│ - Encodes/decodes ProxyData packets                             │
│ - Manages ephemeral port allocation                             │
│ - Routes incoming ProxyData to correct socket                   │
├─────────────────────────────────────────────────────────────────┤
│ RyuLdnClient (existing)                                         │
│ - TCP connection to Ryujinx server                              │
│ - Send/receive ProxyData packets                                │
└─────────────────────────────────────────────────────────────────┘
```

### Fichiers à créer

```
sysmodule/source/bsd/
├── bsd_mitm_service.hpp      # MITM service pour bsd:u
├── bsd_mitm_service.cpp
├── bsd_socket_handler.hpp    # Gestion des sockets (réels vs proxy)
├── bsd_socket_handler.cpp
├── proxy_socket.hpp          # Socket proxy pour LDN
├── proxy_socket.cpp
├── ephemeral_port_pool.hpp   # Allocation de ports éphémères
├── ephemeral_port_pool.cpp
└── interfaces/
    └── ibsd_service.hpp      # Interface IPC BSD
```

---

## Stories

### Story 8.1 : Recherche et Documentation BSD IPC

**Priorité** : P0
**Estimation** : 2 points
**Statut** : `done`
**Dépendances** : -

#### Description
Documenter complètement l'interface IPC BSD et l'architecture Ryujinx LdnProxy.

#### Critères d'acceptation
- [x] Interface IPC BSD documentée (toutes les commandes)
- [x] Architecture Ryujinx LdnProxy documentée
- [x] Protocole ProxyData documenté
- [x] Stratégie d'interception définie
- [x] Architecture proposée validée

#### Tests
- [x] Documentation relue et validée

---

### Story 8.2 : Interface IPC BSD

**Priorité** : P0
**Estimation** : 3 points
**Statut** : `todo`
**Dépendances** : Story 8.1

#### Description
Créer l'interface IPC pour le service BSD avec toutes les structures et commandes.

#### Critères d'acceptation
- [ ] Structures BSD (sockaddr_in, sockaddr_in6, etc.)
- [ ] Interface CMIF avec toutes les commandes (0-27)
- [ ] Documentation des paramètres IPC
- [ ] Tests de compilation

#### Tâches techniques
1. [ ] Créer `sysmodule/source/bsd/bsd_types.hpp` avec structures
2. [ ] Créer `sysmodule/source/bsd/interfaces/ibsd_service.hpp`
3. [ ] Définir les commandes IPC avec AMS_SF_METHOD

#### Tests (TDD)
- [ ] Test: structures ont la bonne taille (static_assert)
- [ ] Test: structures sont packed correctement

---

### Story 8.3 : Service MITM BSD de base

**Priorité** : P0
**Estimation** : 4 points
**Statut** : `todo`
**Dépendances** : Story 8.2

#### Description
Créer le service MITM bsd:u avec forwarding vers le service réel.

#### Critères d'acceptation
- [ ] Service MITM bsd:u enregistré
- [ ] ShouldMitm() basé sur program_id (jeux LDN uniquement)
- [ ] Forwarding transparent vers service réel
- [ ] Pas de régression (jeux fonctionnent normalement)

#### Tâches techniques
1. [ ] Créer `sysmodule/source/bsd/bsd_mitm_service.hpp`
2. [ ] Créer `sysmodule/source/bsd/bsd_mitm_service.cpp`
3. [ ] Implémenter forwarding pour toutes les commandes
4. [ ] Enregistrer dans main.cpp

#### Tests (TDD)
- [ ] Test: service s'enregistre sans crash
- [ ] Test: forwarding fonctionne (socket normal)
- [ ] Test: jeu peut créer socket internet normal

---

### Story 8.4 : Proxy Socket Manager

**Priorité** : P0
**Estimation** : 5 points
**Statut** : `todo`
**Dépendances** : Story 8.3

#### Description
Implémenter le gestionnaire de sockets proxy qui détecte et gère les sockets LDN.

#### Critères d'acceptation
- [ ] Détection d'adresse LDN (10.114.x.x)
- [ ] Allocation de file descriptors proxy
- [ ] Map fd → ProxySocket
- [ ] Pool de ports éphémères

#### Tâches techniques
1. [ ] Créer `sysmodule/source/bsd/proxy_socket.hpp`
2. [ ] Créer `sysmodule/source/bsd/proxy_socket.cpp`
3. [ ] Créer `sysmodule/source/bsd/ephemeral_port_pool.hpp`
4. [ ] Créer `sysmodule/source/bsd/bsd_socket_handler.hpp`
5. [ ] Implémenter IsLdnAddress(addr) → bool

#### Tests (TDD)
- [ ] Test: IsLdnAddress("10.114.0.1") == true
- [ ] Test: IsLdnAddress("192.168.1.1") == false
- [ ] Test: allocation de port éphémère
- [ ] Test: création ProxySocket

---

### Story 8.5 : Interception Bind/Connect

**Priorité** : P0
**Estimation** : 4 points
**Statut** : `todo`
**Dépendances** : Story 8.4

#### Description
Intercepter Bind et Connect pour router vers proxy ou socket réel.

#### Critères d'acceptation
- [ ] Bind sur 10.114.x.x → crée ProxySocket
- [ ] Connect vers 10.114.x.x → envoie ProxyConnect
- [ ] Bind/Connect normal → forward au service réel
- [ ] Gestion d'erreurs correcte

#### Tâches techniques
1. [ ] Implémenter `Bind()` avec détection LDN
2. [ ] Implémenter `Connect()` avec envoi ProxyConnect
3. [ ] Recevoir et traiter `ProxyConnectReply`
4. [ ] Mapper fd système ↔ ProxySocket

#### Tests (TDD)
- [ ] Test: Bind normal forward au service
- [ ] Test: Bind LDN crée ProxySocket
- [ ] Test: Connect LDN envoie ProxyConnect
- [ ] Test: ProxyConnectReply reçu et traité

---

### Story 8.6 : Interception Send/Recv

**Priorité** : P0
**Estimation** : 5 points
**Statut** : `todo`
**Dépendances** : Story 8.5

#### Description
Intercepter Send/SendTo et Recv/RecvFrom pour les sockets proxy.

#### Critères d'acceptation
- [ ] Send sur ProxySocket → encode et envoie ProxyData
- [ ] Recv sur ProxySocket → lit depuis buffer proxy
- [ ] Send/Recv normal → forward au service réel
- [ ] Support UDP et TCP

#### Tâches techniques
1. [ ] Implémenter `Send()` → `ProxyData` encoding
2. [ ] Implémenter `SendTo()` avec adresse destination
3. [ ] Implémenter buffer de réception dans ProxySocket
4. [ ] Implémenter `Recv()` depuis buffer
5. [ ] Implémenter `RecvFrom()` avec adresse source

#### Tests (TDD)
- [ ] Test: Send encode ProxyData correctement
- [ ] Test: données arrivent au serveur
- [ ] Test: ProxyData entrant → buffer
- [ ] Test: Recv lit depuis buffer

---

### Story 8.7 : Réception ProxyData dans LDN MITM

**Priorité** : P0
**Estimation** : 3 points
**Statut** : `in_progress`
**Dépendances** : Story 8.6

#### Description
Intégrer la réception des ProxyData dans le service LDN existant et router vers les ProxySockets.

#### Critères d'acceptation
- [ ] HandleServerPacket traite ProxyData
- [ ] ProxyData routé vers bon ProxySocket
- [ ] Signal pour réveiller Recv bloquant
- [ ] Pas de perte de données

#### Tâches techniques
1. [x] Ajouter case ProxyData dans HandleServerPacket (FAIT)
2. [ ] Créer registre global ProxySockets
3. [ ] Router ProxyData → ProxySocket::IncomingData()
4. [ ] Signaler event pour débloquer Recv

#### Notes
Le case ProxyData a été ajouté mais stocke dans m_proxy_buffer local.
Il faut maintenant router vers les ProxySockets du service BSD.

#### Tests (TDD)
- [ ] Test: ProxyData reçu et parsé
- [ ] Test: données arrivent au bon socket
- [ ] Test: Recv débloqué après réception

---

### Story 8.8 : Commandes BSD restantes

**Priorité** : P1
**Estimation** : 4 points
**Statut** : `todo`
**Dépendances** : Story 8.6

#### Description
Implémenter les commandes BSD restantes pour les sockets proxy.

#### Critères d'acceptation
- [ ] Accept pour TCP listen sockets
- [ ] Listen pour TCP sockets
- [ ] GetSockName/GetPeerName
- [ ] SetSockOpt/GetSockOpt
- [ ] Shutdown/Close

#### Tâches techniques
1. [ ] Implémenter Accept (pour TCP server)
2. [ ] Implémenter Listen
3. [ ] Implémenter Get*Name
4. [ ] Implémenter *SockOpt
5. [ ] Implémenter Shutdown/Close avec cleanup

#### Tests (TDD)
- [ ] Test: Close libère ProxySocket
- [ ] Test: GetSockName retourne adresse proxy
- [ ] Test: SetSockOpt stocké localement

---

### Story 8.9 : Tests d'intégration

**Priorité** : P0
**Estimation** : 4 points
**Statut** : `todo`
**Dépendances** : Story 8.7, Story 8.8

#### Description
Tests d'intégration complets avec Mario Kart 8 Deluxe.

#### Critères d'acceptation
- [ ] CreateNetwork → sockets proxy créés
- [ ] Jeu ne ferme plus l'access point
- [ ] Données échangées entre Switch et émulateur
- [ ] Session stable 30 minutes

#### Scénarios de test
1. Switch crée room, Ryujinx rejoint
2. Ryujinx crée room, Switch rejoint
3. Course complète sans déconnexion
4. Reconnexion après perte réseau

---

## Risques et mitigations

| Risque | Impact | Probabilité | Mitigation |
|--------|--------|-------------|------------|
| Performance BSD MITM | Élevé | Moyen | Optimiser le forwarding, minimiser copies |
| Compatibilité jeux | Élevé | Faible | Tests avec plusieurs jeux |
| Stabilité sockets | Élevé | Moyen | Tests de stress, gestion mémoire |
| Conflits fd | Moyen | Faible | Namespace séparé pour proxy fds |

## Critères de succès

- [ ] Mario Kart 8 Deluxe peut créer une room sans CloseAccessPoint immédiat
- [ ] Switch peut rejoindre room créée par émulateur Ryujinx
- [ ] Émulateur peut rejoindre room créée par Switch
- [ ] Session stable pendant 1 heure de jeu
- [ ] Pas de régression sur les jeux non-LDN
