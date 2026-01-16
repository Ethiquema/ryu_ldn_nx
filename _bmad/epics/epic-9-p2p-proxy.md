# Epic 9 : P2P Proxy & BSD Avancé

**Priorité** : P1 (Optimisation - Post-MVP)
**Estimation** : 3-4 sprints
**Dépendances** : Epic 8 (BSD MITM)

## Description

Implémenter le système P2P (External Proxy) de Ryujinx pour permettre des connexions directes entre clients, réduisant la latence. Le système P2P doit être **100% compatible** avec Ryujinx pour permettre l'interopérabilité.

## Valeur utilisateur

> En tant que **joueur Switch**, je veux que ma console puisse héberger un serveur P2P et permettre des connexions directes avec d'autres joueurs, pour réduire la latence et améliorer l'expérience multijoueur.

## Contexte technique

### Architecture P2P Ryujinx (référence)

```
                           ┌─────────────────────────────────┐
                           │       RyuLDN Server             │
                           │   (Master Server / Relay)       │
                           └───────────────┬─────────────────┘
                                           │
                    ┌──────────────────────┼──────────────────────┐
                    │                      │                      │
            ┌───────▼───────┐      ┌───────▼───────┐      ┌───────▼───────┐
            │   Host (P2P)   │      │    Client 1    │      │    Client 2    │
            │  P2pProxyServer│◄─────│ P2pProxyClient │      │ P2pProxyClient │
            │   (TCP:39990)  │      │  P2pProxyClient│      │                │
            └───────────────┘      └────────────────┘      └────────────────┘
                    │                      │
                    │    Direct TCP        │
                    └──────────────────────┘
```

### Flux P2P détaillé (depuis analyse Ryujinx)

1. **Host crée réseau (CreateNetwork)**
   - `P2pProxyServer` démarre sur port 39990-39999
   - `NatPunch()` via UPnP ouvre port public (lease 60s, renew 50s)
   - `RyuNetworkConfig.ExternalProxyPort` = port public UPnP
   - `RyuNetworkConfig.PrivateIp` = IP locale du host
   - `RyuNetworkConfig.InternalProxyPort` = port privé

2. **Master server notifie host des joiners**
   - Envoie `ExternalProxyToken` avec VirtualIP + Token + PhysicalIP
   - Host stocke dans `_waitingTokens`

3. **Client rejoint (Connect)**
   - Reçoit `ExternalProxyConfig` du master server
   - Crée `P2pProxyClient` et se connecte au host
   - Envoie `ExternalProxyConfig` pour authentification

4. **Host valide client**
   - `TryRegisterUser()` match token dans `_waitingTokens`
   - Assigne `VirtualIP` à la session
   - Envoie `ProxyConfig` au client

5. **Communication directe**
   - `ProxyData/ProxyConnect/ProxyConnectReply/ProxyDisconnect`
   - Routé via `RouteMessage()` - broadcast ou unicast

### Analyse de compatibilité

| Composant | Ryujinx | Notre impl. | Status |
|-----------|---------|-------------|--------|
| Types P2P | ✅ | ✅ | OK |
| ExternalProxyConfig | 0x26 bytes | 0x26 bytes | ✅ |
| ExternalProxyToken | 0x28 bytes | 0x28 bytes | ✅ |
| ExternalProxyConnectionState | 0x08 bytes | 0x08 bytes | ✅ |
| TCP Accept queue (BSD) | ✅ | ✅ | **Implémenté** |
| TCP Connect handshake (BSD) | ✅ | ✅ | **Implémenté** |
| Broadcast filtering (BSD) | ✅ | ✅ | **Implémenté** |
| UPnP NAT Punch | ✅ (Open.NAT) | ✅ | **Implémenté** (switch-miniupnpc) |
| P2pProxyServer | ✅ | ✅ | **Implémenté** |
| P2pProxyClient | ✅ | ✅ | **Implémenté** |
| HandleExternalProxy | ✅ | ✅ | **Implémenté** |
| ConfigureAccessPoint P2P | ✅ | ❌ | À impl. |

### Composants Ryujinx analysés

```
Ryujinx/src/Ryujinx.HLE/HOS/Services/Ldn/UserServiceCreator/LdnRyu/
├── LdnMasterProxyClient.cs     # Client principal (INetworkClient + IProxyClient)
│   ├── CreateNetwork()          # Démarre P2pProxyServer + UPnP
│   ├── HandleExternalProxy()    # Crée P2pProxyClient pour joiner
│   └── ConfigureAccessPoint()   # Configure P2P dans RyuNetworkConfig
├── IProxyClient.cs             # Interface SendAsync()
├── Proxy/
│   ├── P2pProxyServer.cs       # Serveur TCP pour host P2P
│   │   ├── NatPunch()          # UPnP port mapping
│   │   ├── TryRegisterUser()   # Authentification token
│   │   ├── RouteMessage()      # Routing ProxyData
│   │   └── Handle*()           # Handlers ProxyConnect/Data/Disconnect
│   ├── P2pProxyClient.cs       # Client TCP pour joiner
│   │   ├── PerformAuth()       # Envoi ExternalProxyConfig
│   │   └── EnsureProxyReady()  # Wait ProxyConfig
│   ├── P2pProxySession.cs      # Session TCP par joueur
│   │   └── Handle*()           # Délègue au parent server
│   ├── LdnProxy.cs             # Gestionnaire de proxy sockets
│   │   ├── RegisterSocket()    # Enregistre LdnProxySocket
│   │   ├── HandleData()        # Route vers socket approprié
│   │   └── SendTo()            # Encode et envoie ProxyData
│   └── LdnProxySocket.cs       # Socket virtuel (ISocketImpl)
│       ├── Accept()            # Queue de ProxyConnectRequest
│       ├── Connect()           # Handshake ProxyConnect
│       └── IncomingData()      # Filtrage broadcast
└── Types/
    ├── ExternalProxyConfig.cs  # IP + Port + Token (0x26 bytes)
    ├── ExternalProxyToken.cs   # VirtualIP + Token + PhysicalIP (0x28 bytes)
    └── ExternalProxyConnectionState.cs  # IP + Connected (0x08 bytes)
```

---

## UPnP pour Switch

### Solution : switch-miniupnpc (devkitPro officiel)

**Bonne nouvelle** : La librairie miniupnpc est déjà portée officiellement pour Switch !

- **Package** : `switch-miniupnpc`
- **Installation** : `dkp-pacman -S switch-miniupnpc`
- **Source** : https://github.com/devkitPro/pacman-packages/tree/master/switch/miniupnpc
- **Licence** : BSD-3-Clause

Le Dockerfile a été mis à jour pour inclure ce package :
```dockerfile
RUN dkp-pacman -S --noconfirm \
    switch-miniupnpc \
    ...
```

### API UPnP (identique à miniupnpc standard)

```c
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>

// Découverte (timeout 2500ms comme Ryujinx)
struct UPNPDev* devlist = upnpDiscover(2500, NULL, NULL, 0, 0, 2, &error);
int r = UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr));

// Port mapping (lease 60s comme Ryujinx)
int UPNP_AddPortMapping(
    urls.controlURL,
    data.first.servicetype,
    "39990",            // Port externe (public)
    "39990",            // Port interne (privé)
    lanaddr,            // IP locale
    "ryu_ldn_nx P2P",   // Description
    "TCP",              // Protocole
    NULL,               // remoteHost (wildcard)
    "60"                // Durée en secondes
);

// Suppression port mapping
int UPNP_DeletePortMapping(
    urls.controlURL,
    data.first.servicetype,
    "39990",
    "TCP",
    NULL
);

// Get external IP
char externalIP[16];
UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, externalIP);

// Cleanup
freeUPNPDevlist(devlist);
FreeUPNPUrls(&urls);
```

### Constantes (alignées sur Ryujinx)

```cpp
constexpr uint16_t PRIVATE_PORT_BASE = 39990;
constexpr int PRIVATE_PORT_RANGE = 10;
constexpr uint16_t PUBLIC_PORT_BASE = 39990;
constexpr int PUBLIC_PORT_RANGE = 10;
constexpr int PORT_LEASE_LENGTH = 60;   // seconds
constexpr int PORT_LEASE_RENEW = 50;    // seconds
constexpr int UPNP_DISCOVERY_TIMEOUT = 2500;  // ms
```

---

## Stories

### Story 9.1 : Corrections BSD - TCP Accept Queue ✅

**Priorité** : P1
**Estimation** : 3 points
**Statut** : `done`
**Dépendances** : Epic 8

#### Description
Implémenter une queue d'accept pour les sockets TCP listen, comme Ryujinx.

#### Implémentation réalisée (2026-01-15)

Fichiers modifiés :
- [proxy_socket.hpp](../../sysmodule/source/bsd/proxy_socket.hpp) - Ajout queue et état Listen
- [proxy_socket.cpp](../../sysmodule/source/bsd/proxy_socket.cpp) - Implémentation Accept()
- [proxy_socket_manager.hpp](../../sysmodule/source/bsd/proxy_socket_manager.hpp) - RouteConnectRequest()
- [proxy_socket_manager.cpp](../../sysmodule/source/bsd/proxy_socket_manager.cpp) - Routing

```cpp
// ProxySocket - Nouveaux membres
std::queue<ryu_ldn::protocol::ProxyConnectRequest> m_accept_queue;
os::ConditionVariable m_accept_cv;

// Accept() - Bloque jusqu'à connexion entrante
ProxySocket* ProxySocket::Accept(s32 new_fd) {
    std::scoped_lock lock(m_mutex);
    while (m_accept_queue.empty()) {
        m_accept_cv.Wait(m_mutex);
    }
    auto request = m_accept_queue.front();
    m_accept_queue.pop();
    // Crée nouveau socket avec état Connected
    // Envoie ProxyConnectReply
}
```

#### Critères d'acceptation ✅
- [x] Queue de ProxyConnectRequest dans ProxySocket
- [x] Accept() bloque jusqu'à connexion entrante
- [x] ProxyConnectReply envoyé automatiquement
- [x] Nouveau ProxySocket créé pour connexion acceptée

---

### Story 9.2 : Corrections BSD - TCP Connect Handshake ✅

**Priorité** : P1
**Estimation** : 3 points
**Statut** : `done`
**Dépendances** : Story 9.1

#### Description
Implémenter le handshake TCP complet avec ProxyConnect/ProxyConnectReply.

#### Implémentation réalisée (2026-01-15)

```cpp
// ProxySocket - Nouveaux membres
bool m_connecting = false;
ryu_ldn::protocol::ProxyConnectResponse m_connect_response;
os::ConditionVariable m_connect_cv;

// Connect() - Envoie ProxyConnect et attend réponse
Result ProxySocket::Connect(const ryu_ldn::bsd::SockAddrIn& addr) {
    // Envoie ProxyConnect via callback
    m_connecting = true;
    ProxySocketManager::GetInstance().SendProxyConnect(...);

    // Wait avec timeout
    auto deadline = os::GetSystemTick() + CONNECT_TIMEOUT_TICKS;
    while (m_connecting && os::GetSystemTick() < deadline) {
        m_connect_cv.TimedWait(m_mutex, deadline);
    }

    // Vérifie réponse
    if (m_connect_response.info.protocol != ProtocolType::Unspecified) {
        R_THROW(Errno::Connrefused);
    }
    m_state = ProxySocketState::Connected;
}
```

#### Critères d'acceptation ✅
- [x] Connect envoie ProxyConnect
- [x] Connect bloque jusqu'à ProxyConnectReply
- [x] Gestion du timeout
- [x] Gestion du refus de connexion

---

### Story 9.3 : Corrections BSD - Broadcast Filtering ✅

**Priorité** : P2
**Estimation** : 2 points
**Statut** : `done`
**Dépendances** : Epic 8

#### Description
Ajouter le flag broadcast et filtrage comme Ryujinx.

#### Implémentation réalisée (2026-01-15)

```cpp
// ProxySocket - Nouveau membre
bool m_broadcast = false;

// SetSockOpt - SO_BROADCAST
case ryu_ldn::bsd::SocketOption::Broadcast:
    m_broadcast = (*reinterpret_cast<const s32*>(optval) != 0);
    R_SUCCEED();

// IncomingData - Filtrage
void ProxySocket::IncomingData(const ryu_ldn::protocol::ProxyDataHeader& header,
                                const void* data, size_t data_len) {
    bool is_broadcast = (header.dest_ipv4 == GetBroadcastAddress());
    if (is_broadcast && !m_broadcast) {
        return;  // Ignore broadcast si flag désactivé
    }
    // Queue le paquet...
}
```

#### Critères d'acceptation ✅
- [x] Flag `m_broadcast` dans ProxySocket
- [x] SetSockOpt SO_BROADCAST met à jour le flag
- [x] GetSockOpt SO_BROADCAST retourne le flag
- [x] IncomingData filtre broadcast si flag désactivé

---

### Story 9.4 : Wrapper UPnP avec switch-miniupnpc ✅

**Priorité** : P1
**Estimation** : 2 points (réduit car lib dispo)
**Statut** : `done`
**Dépendances** : -

#### Description
Créer un wrapper C++ pour switch-miniupnpc (déjà disponible via devkitPro).

#### Implémentation réalisée (2026-01-15)

Fichiers créés :
- [upnp_port_mapper.hpp](../../sysmodule/source/p2p/upnp_port_mapper.hpp) - Interface singleton
- [upnp_port_mapper.cpp](../../sysmodule/source/p2p/upnp_port_mapper.cpp) - Implémentation complète
- [tests/upnp_tests.cpp](../../tests/upnp_tests.cpp) - Tests unitaires (25 tests)

Makefile mis à jour :
- Ajout `source/p2p` aux SOURCES
- Ajout `-lminiupnpc` aux LIBS

```cpp
class UpnpPortMapper {
public:
    static UpnpPortMapper& GetInstance();

    // Découverte UPnP (timeout 2500ms)
    bool Discover();
    bool IsAvailable() const;

    // Port mapping (TCP)
    bool AddPortMapping(uint16_t internal_port, uint16_t external_port,
                        const char* description, int lease_duration = 60);
    bool DeletePortMapping(uint16_t external_port);
    bool RefreshPortMapping(uint16_t internal_port, uint16_t external_port,
                            const char* description);

    // Info
    bool GetExternalIPAddress(char* ip_out, size_t ip_len);
    bool GetLocalIPAddress(char* ip_out, size_t ip_len);
    uint32_t GetLocalIPv4() const;

    void Cleanup();

private:
    mutable os::Mutex m_mutex{false};
    UPNPUrls* m_urls;
    IGDdatas* m_data;
    char m_lan_addr[64];
    bool m_available;
};
```

#### Critères d'acceptation ✅
- [x] Wrapper compile avec switch-miniupnpc
- [x] Discover() implémenté avec SSDP
- [x] AddPortMapping() ouvre un port TCP
- [x] DeletePortMapping() ferme un port
- [x] RefreshPortMapping() renouvelle le lease
- [x] GetExternalIPAddress() retourne IP publique
- [x] GetLocalIPAddress() retourne IP locale
- [x] Thread-safe avec mutex

#### Tests ✅ (25/25 passent)
- [x] Constantes alignées sur Ryujinx
- [x] Parsing IPv4 (valide, localhost, broadcast, zero, classe A)
- [x] Parsing IPv4 (vide, null, format invalide, overflow, garbage)
- [x] Formatage des ports
- [x] Codes d'erreur UPnP
- [x] Vérification lease renew < lease duration

---

### Story 9.5 : P2pProxyServer (Host) ✅

**Priorité** : P1
**Estimation** : 5 points
**Statut** : `done`
**Dépendances** : Story 9.4

#### Description
Implémenter le serveur P2P qui permet d'héberger des connexions directes.

#### Implémentation réalisée (2026-01-15)

Fichiers créés :
- [p2p_proxy_server.hpp](../../sysmodule/source/p2p/p2p_proxy_server.hpp) - Classes P2pProxyServer et P2pProxySession
- [p2p_proxy_server.cpp](../../sysmodule/source/p2p/p2p_proxy_server.cpp) - Implémentation complète (~1600 lignes)
- [tests/p2p_proxy_tests.cpp](../../tests/p2p_proxy_tests.cpp) - Tests unitaires (27 tests)

Architecture implémentée :
```
                    ┌─────────────────────┐
                    │  RyuLDN Server      │
                    │  (Master Server)    │
                    └──────────┬──────────┘
                               │ ExternalProxyToken
                    ┌──────────▼──────────┐
                    │  P2pProxyServer     │◄────── Joiner P2pProxyClient
                    │  (Switch Host)      │
                    │  TCP:39990-39999    │◄────── Joiner P2pProxyClient
                    └─────────────────────┘
```

```cpp
class P2pProxyServer {
public:
    static constexpr uint16_t PRIVATE_PORT_BASE = 39990;
    static constexpr int PRIVATE_PORT_RANGE = 10;
    static constexpr int PORT_LEASE_LENGTH = 60;   // seconds
    static constexpr int PORT_LEASE_RENEW = 50;    // seconds
    static constexpr int AUTH_WAIT_SECONDS = 1;
    static constexpr int MAX_PLAYERS = 8;

    bool Start(uint16_t port = 0);
    void Stop();
    uint16_t NatPunch();
    void ReleaseNatPunch();

    void AddWaitingToken(const ExternalProxyToken& token);
    bool TryRegisterUser(P2pProxySession* session,
                         const ExternalProxyConfig& config,
                         uint32_t remote_ip);

    void HandleProxyData(...);
    void HandleProxyConnect(...);
    void HandleProxyConnectReply(...);
    void HandleProxyDisconnect(...);
    void OnSessionDisconnected(P2pProxySession* session);

private:
    os::Mutex m_mutex{false};
    int m_listen_fd;
    os::ThreadType m_accept_thread;     // Accept loop
    os::ThreadType m_lease_thread;      // UPnP lease renewal
    P2pProxySession* m_sessions[MAX_PLAYERS];
    ExternalProxyToken m_waiting_tokens[MAX_WAITING_TOKENS];
};

class P2pProxySession {
public:
    uint32_t GetVirtualIpAddress() const;
    void SetVirtualIp(uint32_t ip);
    bool IsAuthenticated() const;
    void Start();  // Start receive thread
    bool Send(const void* data, size_t size);
    void Disconnect(bool from_master = false);

private:
    os::ThreadType m_recv_thread;  // Per-session receive loop
    // Protocol handlers for ProxyData, ProxyConnect, etc.
};
```

#### Critères d'acceptation ✅
- [x] Serveur TCP écoute sur port 39990+
- [x] UPnP ouvre port public via UpnpPortMapper
- [x] Authentification par token (TryRegisterUser)
- [x] Routing ProxyData entre sessions (broadcast et unicast)
- [x] Cleanup propre à la fermeture

#### Tâches techniques ✅
1. [x] Créer classe P2pProxyServer
2. [x] Implémenter Start/Stop avec socket listen et accept thread
3. [x] Intégrer UpnpPortMapper pour UPnP NAT punch
4. [x] Implémenter TryRegisterUser avec validation token
5. [x] Implémenter HandleProxyData routing (template RouteMessage)
6. [x] Créer P2pProxySession avec receive thread par client
7. [x] Lease renewal thread (50s interval)
8. [x] MasterSendCallback pour notifier déconnexions

#### Tests ✅ (27/27 passent)
- [x] Test: Constantes Ryujinx (ports, lease, etc.)
- [x] Test: Calcul broadcast address (classe A, B, C)
- [x] Test: Détection IP privée (RFC 1918)
- [x] Test: Extraction IPv4 depuis sockaddr
- [x] Test: Validation token (token match, IP mismatch, token invalide)

---

### Story 9.6 : P2pProxyClient (Joiner) ✅

**Priorité** : P1
**Estimation** : 4 points
**Statut** : `done`
**Dépendances** : Story 9.5

#### Description
Implémenter le client P2P qui se connecte à un host externe.

#### Implémentation réalisée (2026-01-16)

Fichiers créés :
- [p2p_proxy_client.hpp](../../sysmodule/source/p2p/p2p_proxy_client.hpp) - Classe P2pProxyClient
- [p2p_proxy_client.cpp](../../sysmodule/source/p2p/p2p_proxy_client.cpp) - Implémentation complète (~600 lignes)
- [tests/p2p_proxy_client_tests.cpp](../../tests/p2p_proxy_client_tests.cpp) - Tests unitaires (31 tests)

Architecture implémentée :
```
                    ┌─────────────────────┐
                    │  P2pProxyServer     │
                    │  (Switch Host)      │
                    │  TCP:39990-39999    │
                    └──────────┬──────────┘
                               │ ProxyConfig
                    ┌──────────▼──────────┐
                    │  P2pProxyClient     │◄────── ExternalProxyConfig
                    │  (Switch Joiner)    │
                    │  Connect + Auth     │
                    └─────────────────────┘
```

```cpp
class P2pProxyClient {
public:
    static constexpr int FAILURE_TIMEOUT_MS = 4000;  // Ryujinx compatible
    static constexpr int CONNECT_TIMEOUT_MS = 5000;
    static constexpr size_t RECV_BUFFER_SIZE = 0x10000;  // 64KB

    bool Connect(const char* address, uint16_t port);
    bool Connect(const uint8_t* ip_bytes, size_t ip_len, uint16_t port);
    void Disconnect();

    bool PerformAuth(const ExternalProxyConfig& config);
    bool EnsureProxyReady(int timeout_ms = FAILURE_TIMEOUT_MS);

    const ProxyConfig& GetProxyConfig() const;
    bool SendProxyData(const ProxyDataHeader& header,
                       const uint8_t* data, size_t data_len);

    void SetPacketCallback(ProxyPacketCallback callback);

private:
    friend void ClientRecvThreadEntry(void* arg);

    os::Mutex m_mutex{false};
    int m_socket_fd = -1;
    bool m_connected = false;
    bool m_ready = false;

    os::ConditionVariable m_ready_cv;
    ProxyConfig m_proxy_config;

    // Receive thread
    os::ThreadType m_recv_thread;
    alignas(0x1000) uint8_t m_recv_thread_stack[0x4000];
    bool m_recv_thread_running = false;

    ProxyPacketCallback m_packet_callback;
};
```

#### Critères d'acceptation ✅
- [x] Connexion TCP au host P2P
- [x] Envoi ExternalProxyConfig pour auth (0x26 bytes)
- [x] Réception ProxyConfig du host
- [x] Attente ready avec timeout (EnsureProxyReady)
- [x] Receive thread pour paquets entrants

#### Tâches techniques ✅
1. [x] Créer classe P2pProxyClient
2. [x] Implémenter Connect/Disconnect avec timeout
3. [x] Implémenter PerformAuth (envoi ExternalProxyConfig)
4. [x] Handler pour ProxyConfig (signal ready)
5. [x] Receive thread avec ProcessData
6. [x] ProxyPacketCallback pour routing BSD MITM

#### Tests ✅ (31/31 passent)
- [x] Test: Constantes Ryujinx (FAILURE_TIMEOUT_MS = 4000)
- [x] Test: Parsing IPv4 (valide, localhost, broadcast, zero)
- [x] Test: Parsing IPv4 invalide (vide, null, format, overflow)
- [x] Test: Détection IP privée (RFC 1918)
- [x] Test: Calcul broadcast (classe A, B, C)
- [x] Test: ExternalProxyConfig size (0x26 bytes)
- [x] Test: États connexion et timeout

---

### Story 9.7 : Intégration HandleExternalProxy ✅

**Priorité** : P1
**Estimation** : 3 points
**Statut** : `done`
**Dépendances** : Story 9.5, Story 9.6

#### Description
Compléter le handler ExternalProxy dans LDN service pour utiliser P2P.

#### Implémentation réalisée (2026-01-16)

Fichiers modifiés :
- [ldn_icommunication.hpp](../../sysmodule/source/ldn/ldn_icommunication.hpp) - Ajout P2pProxyClient membre
- [ldn_icommunication.cpp](../../sysmodule/source/ldn/ldn_icommunication.cpp) - HandleExternalProxyConnect + SendProxyDataToServer P2P routing
- [tests/p2p_integration_tests.cpp](../../tests/p2p_integration_tests.cpp) - Tests unitaires (35 tests)

```cpp
// ldn_icommunication.hpp - Nouveaux membres
p2p::P2pProxyClient* m_p2p_client;  ///< Connected P2P proxy client (joiner side)

void HandleExternalProxyConnect(const ryu_ldn::protocol::ExternalProxyConfig& config);
void DisconnectP2pProxy();

// ldn_icommunication.cpp - HandleExternalProxyConnect
void ICommunicationService::HandleExternalProxyConnect(
    const ryu_ldn::protocol::ExternalProxyConfig& config)
{
    // Disconnect existing P2P if any
    DisconnectP2pProxy();

    // Create callback for routing P2P packets to BSD MITM
    auto packet_callback = [](ryu_ldn::protocol::PacketId type,
                               const void* data, size_t size) {
        if (type == ryu_ldn::protocol::PacketId::ProxyData) {
            // Route to BSD MITM ProxySocketManager
            ryu_ldn::bsd::ProxySocketManager::GetInstance().HandleProxyData(header, payload, payload_len);
        }
    };

    m_p2p_client = new p2p::P2pProxyClient(packet_callback);

    // Connect using IPv4 (address_family == 2)
    bool connected = m_p2p_client->Connect(config.proxy_ip, 4, config.proxy_port);
    if (!connected) { DisconnectP2pProxy(); return; }

    // Authenticate with token
    if (!m_p2p_client->PerformAuth(config)) { DisconnectP2pProxy(); return; }

    // Wait for ProxyConfig from host
    if (!m_p2p_client->EnsureProxyReady()) { DisconnectP2pProxy(); return; }

    m_proxy_config = m_p2p_client->GetProxyConfig();
}

// ldn_icommunication.cpp - SendProxyDataToServer P2P routing
ryu_ldn::network::ClientOpResult ICommunicationService::SendProxyDataToServer(...) {
    // If P2P client is connected, route through P2P
    if (m_p2p_client != nullptr && m_p2p_client->IsReady()) {
        if (m_p2p_client->SendProxyData(header, data, data_len)) {
            return ryu_ldn::network::ClientOpResult::Success;
        }
        // Fall through to master server if P2P fails
    }
    return m_server_client.send_proxy_data(header, data, data_len);
}
```

#### Critères d'acceptation ✅
- [x] HandleExternalProxy crée P2pProxyClient
- [x] Connexion au host P2P
- [x] Auth avec token
- [x] Fallback sur serveur master si échec
- [x] ProxyData routé via P2P si connecté

#### Tâches techniques ✅
1. [x] Compléter HandleExternalProxy → HandleExternalProxyConnect()
2. [x] Créer P2pProxyClient si config valide
3. [x] Gérer échec connexion → cleanup et return
4. [x] Switcher routage ProxyData vers P2P → SendProxyDataToServer modifié
5. [x] Cleanup à la déconnexion → DisconnectP2pProxy() + destructor + DisconnectFromServer()

#### Tests ✅ (35/35 passent)
- [x] Test: ExternalProxyConfig structure (size 0x26, offsets)
- [x] Test: Address family (IPv4 = 2, IPv6 = 10)
- [x] Test: ProxyDataHeader structure (size 12, offsets)
- [x] Test: Routing logic (null client, disconnected, not ready, ready)
- [x] Test: Token handling (size, copy, zero check)
- [x] Test: HandleExternalProxy flow (disabled, connect fail, auth fail, ready fail, success)
- [x] Test: Cleanup on failure
- [x] Test: DisconnectP2pProxy safety
- [x] Test: Callback routing (ProxyData, ProxyConfig)
- [x] Test: IP extraction from config

---

### Story 9.8 : Intégration CreateNetwork avec P2P ✅

**Priorité** : P1
**Estimation** : 4 points
**Statut** : `done`
**Dépendances** : Story 9.5, Story 9.7

#### Description
Quand le Switch crée un réseau (host), démarrer P2pProxyServer si possible.

#### Implémentation réalisée (2026-01-16)

Fichiers modifiés :
- [ldn_icommunication.hpp](../../sysmodule/source/ldn/ldn_icommunication.hpp) - Ajout P2pProxyServer membre + méthodes
- [ldn_icommunication.cpp](../../sysmodule/source/ldn/ldn_icommunication.cpp) - CreateNetwork P2P + handlers
- [tcp_client.hpp](../../sysmodule/source/network/tcp_client.hpp) - Ajout send_raw()
- [tcp_client.cpp](../../sysmodule/source/network/tcp_client.cpp) - Implémentation send_raw()
- [client.hpp](../../sysmodule/source/network/client.hpp) - Ajout send_raw_packet()
- [client.cpp](../../sysmodule/source/network/client.cpp) - Implémentation send_raw_packet()
- [p2p_proxy_server.hpp](../../sysmodule/source/p2p/p2p_proxy_server.hpp) - MasterSendCallback avec user_data
- [p2p_proxy_server.cpp](../../sysmodule/source/p2p/p2p_proxy_server.cpp) - Constructor avec user_data
- [tests/p2p_create_network_tests.cpp](../../tests/p2p_create_network_tests.cpp) - 40 tests unitaires

```cpp
// ldn_icommunication.hpp - Nouveaux membres
p2p::P2pProxyServer* m_p2p_server;  ///< Hosted P2P proxy server (host side)

bool StartP2pProxyServer();
void StopP2pProxyServer();
void HandleExternalProxyToken(const ryu_ldn::protocol::ExternalProxyToken& token);

// ldn_icommunication.cpp - CreateNetwork avec P2P
if (m_use_p2p_proxy && StartP2pProxyServer()) {
    uint16_t public_port = m_p2p_server->NatPunch();
    uint32_t local_ip = p2p::UpnpPortMapper::GetInstance().GetLocalIPv4();
    std::memcpy(request.ryu_network_config.private_ip, &local_ip, sizeof(local_ip));
    request.ryu_network_config.address_family = 2;  // AF_INET
    request.ryu_network_config.external_proxy_port = public_port;
    request.ryu_network_config.internal_proxy_port = m_p2p_server->GetPrivatePort();
}

// StartP2pProxyServer avec callback user_data pattern
auto master_send_callback = [](const void* data, size_t size, void* user_data) {
    auto* self = static_cast<ICommunicationService*>(user_data);
    if (self->IsServerConnected()) {
        self->m_server_client.send_raw_packet(data, size);
    }
};
m_p2p_server = new p2p::P2pProxyServer(master_send_callback, this);
```

#### Critères d'acceptation ✅
- [x] P2pProxyServer démarré à CreateNetwork
- [x] UPnP tente d'ouvrir port
- [x] RyuNetworkConfig rempli avec ports
- [x] Fallback si UPnP échoue (ports = 0)
- [x] Cleanup à CloseAccessPoint/DestroyNetwork

#### Tâches techniques ✅
1. [x] Créer P2pProxyServer dans CreateNetwork
2. [x] Appeler NatPunch()
3. [x] Remplir RyuNetworkConfig si succès
4. [x] Log warning si UPnP échoue
5. [x] Cleanup dans CloseAccessPoint et DestroyNetwork
6. [x] Handler ExternalProxyToken pour tokens entrants

#### Tests ✅ (40/40 passent)
- [x] Test: P2P server constants (port base, range, lease, etc.)
- [x] Test: RyuNetworkConfig structure (size 40, offsets)
- [x] Test: ExternalProxyToken structure (size 40, offsets)
- [x] Test: CreateNetwork P2P config initialization
- [x] Test: CreateNetwork P2P disabled config
- [x] Test: UPnP failed config (external_port = 0)
- [x] Test: Token handling flow
- [x] Test: MasterSendCallback signature et user_data pattern
- [x] Test: Port selection logic
- [x] Test: Full CreateNetwork flow (success and disabled)

---

### Story 9.9 : Tests d'intégration P2P

**Priorité** : P1
**Estimation** : 3 points
**Statut** : `todo`
**Dépendances** : Story 9.8

#### Description
Tests complets du système P2P avec différentes configurations.

#### Scénarios de test

1. **Switch host, Ryujinx join via P2P**
   - Switch crée room avec UPnP
   - Ryujinx reçoit ExternalProxy
   - Connexion P2P directe
   - Gameplay via P2P

2. **UPnP indisponible - fallback**
   - Switch crée room sans UPnP
   - Ryujinx utilise serveur master
   - Gameplay via relay

3. **Déconnexion P2P - recovery**
   - Connexion P2P établie
   - Perte connexion P2P
   - Fallback vers master
   - Reprise gameplay

#### Critères d'acceptation
- [ ] P2P fonctionne quand UPnP disponible
- [ ] Fallback fonctionne quand UPnP indisponible
- [ ] Recovery après perte P2P
- [ ] Pas de régression mode relay

#### Tests
- [ ] Test intégration: Switch host + Ryujinx P2P
- [ ] Test intégration: Fallback sans UPnP
- [ ] Test intégration: Recovery après perte
- [ ] Test stress: Session longue P2P

---

## Risques et mitigations

| Risque | Impact | Probabilité | Mitigation |
|--------|--------|-------------|------------|
| UPnP non supporté par routeur | Moyen | Élevé | Fallback vers relay |
| NAT symétrique bloque P2P | Moyen | Moyen | Fallback vers relay |
| Port déjà utilisé | Faible | Moyen | Range de ports 39990-39999 |
| Latence UPnP discovery | Faible | Faible | Timeout raisonnable (2.5s) |
| miniupnpc incompatible Switch | Élevé | Faible | Tests extensifs, fallback |

## Critères de succès

- [ ] UPnP fonctionne sur routeurs compatibles
- [ ] P2P réduit latence mesurable
- [ ] Fallback transparent si P2P échoue
- [ ] Pas de régression sur fonctionnalités existantes
- [ ] Session stable 1h en mode P2P

## Dépendances externes

- **miniupnpc** : https://github.com/miniupnp/miniupnp (BSD-3-Clause)
- **UPnP IGD** : Standard UPnP, supporté par la plupart des routeurs modernes

## Notes techniques

### Constantes P2P (Ryujinx)
```
PrivatePortBase = 39990
PrivatePortRange = 10
PublicPortBase = 39990
PublicPortRange = 10
PortLeaseLength = 60 seconds
PortLeaseRenew = 50 seconds
AuthWaitSeconds = 1
FailureTimeout = 4000 ms
```

### Subnet LDN
```
NetworkBase = 0x0A720000 (10.114.0.0)
SubnetMask = 0xFFFF0000 (/16)
Broadcast = IP | ~Mask
```
