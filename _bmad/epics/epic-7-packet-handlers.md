# Epic 7 : Packet Handlers & P2P Communication

**Priorité** : P0 (Critical - Bloque le jeu en ligne)
**Estimation** : 2 sprints
**Dépendances** : Epic 2, Epic 3

## Description

Implémenter la réception et le traitement des paquets serveur dans le sysmodule. Actuellement, le sysmodule peut envoyer des requêtes mais ne traite pas les réponses du serveur. Cette epic ajoute :
- Boucle de réception des paquets
- Handlers pour tous les types de paquets serveur
- Support complet du P2P proxy tunneling

## Valeur utilisateur

> En tant que **joueur Switch**, je veux pouvoir jouer en ligne avec d'autres joueurs via RyuLDN, avec une synchronisation correcte de l'état réseau et des données de jeu transmises en temps réel.

## Architecture cible

```
┌─────────────────────────────────────────────────────────────┐
│                    ICommunicationService                     │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────────┐  │
│  │ State       │  │ RyuLdnClient │  │ PacketDispatcher  │  │
│  │ Machine     │  │ + update()   │  │ + handlers        │  │
│  └─────────────┘  └──────┬───────┘  └─────────┬─────────┘  │
│                          │                     │            │
│                          ▼                     ▼            │
│                   ┌──────────────────────────────┐          │
│                   │     Packet Callback          │          │
│                   │  OnPacketReceived(id, data)  │          │
│                   └──────────────────────────────┘          │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │                  Packet Handlers                     │   │
│  ├─────────────────────────────────────────────────────┤   │
│  │ HandleSyncNetwork()    HandleConnected()            │   │
│  │ HandleScanReply()      HandleScanReplyEnd()         │   │
│  │ HandleReject()         HandleNetworkError()         │   │
│  │ HandleProxyConfig()    HandleProxyData()            │   │
│  │ HandleProxyConnect()   HandleProxyConnectReply()    │   │
│  │ HandleProxyDisconnect()                             │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │                   P2P Proxy Layer                    │   │
│  ├─────────────────────────────────────────────────────┤   │
│  │ LdnProxyBuffer (existing)                           │   │
│  │ LdnNodeMapper (existing)                            │   │
│  │ ProxyConnectionManager (new)                        │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## Objectifs

- [ ] Packet reception loop avec callback
- [ ] Handlers pour tous les paquets serveur (16 types)
- [ ] P2P proxy data forwarding fonctionnel
- [ ] Synchronisation NetworkInfo correcte
- [ ] Tests unitaires pour chaque handler
- [ ] Documentation complète

---

## Stories

### Story 7.1 : Packet Reception Infrastructure

**Priorité** : P0
**Estimation** : 3 points
**Statut** : `pending`
**Dépendances** : Epic 2

#### Description
Mettre en place l'infrastructure de réception des paquets : callback, dispatcher, et boucle de mise à jour.

#### Critères d'acceptation
- [ ] `set_packet_callback()` configuré dans ICommunicationService
- [ ] Thread/Timer pour appeler `m_server_client.update()` périodiquement
- [ ] PacketDispatcher qui route les paquets vers les handlers appropriés
- [ ] Logging des paquets reçus (debug mode)

#### Tâches techniques
1. ⬜ Créer `ldn_packet_dispatcher.hpp/cpp`
2. ⬜ Ajouter callback dans ICommunicationService::ConnectToServer()
3. ⬜ Implémenter boucle update() (thread ou timer)
4. ⬜ Ajouter enum pour tous les PacketId gérés
5. ⬜ Tests unitaires pour le dispatcher

#### Fichiers créés/modifiés
- `sysmodule/source/ldn/ldn_packet_dispatcher.hpp` (nouveau)
- `sysmodule/source/ldn/ldn_packet_dispatcher.cpp` (nouveau)
- `sysmodule/source/ldn/ldn_icommunication.cpp` (modifié)
- `tests/ldn_packet_dispatcher_tests.cpp` (nouveau)

---

### Story 7.2 : Session Handlers (Initialize, Connected, Error)

**Priorité** : P0
**Estimation** : 3 points
**Statut** : `pending`
**Dépendances** : Story 7.1

#### Description
Implémenter les handlers pour les paquets de session : confirmation de connexion, synchronisation réseau, et erreurs.

#### Critères d'acceptation
- [ ] HandleInitialize() - Stocker session ID et MAC assignés
- [ ] HandleConnected() - Confirmer connexion, stocker NetworkInfo
- [ ] HandleSyncNetwork() - Mettre à jour état réseau (joueurs join/leave)
- [ ] HandleNetworkError() - Traiter erreurs serveur, signaler au jeu
- [ ] HandlePing() - Echo ping serveur (requester == 0)

#### Handlers détaillés

```cpp
// HandleInitialize - Réponse du serveur après notre Initialize
void HandleInitialize(const InitializeMessage& msg) {
    // Stocker l'ID de session assigné par le serveur
    m_assigned_session_id = msg.session_id;
    m_assigned_mac = msg.mac_address;
    LOG_INFO("Server assigned session ID and MAC");
}

// HandleConnected - Confirmation de connexion réussie
void HandleConnected(const NetworkInfo& info) {
    // Stocker les infos réseau
    m_network_info = info;
    m_state_machine.OnConnected();
    // Signaler au jeu via event
    SignalStateEvent();
}

// HandleSyncNetwork - Mise à jour de l'état réseau
void HandleSyncNetwork(const NetworkInfo& info) {
    // Calculer les changements (nouveaux joueurs, départs)
    CalculateNodeUpdates(m_network_info, info);
    m_network_info = info;
    // Mettre à jour SharedState pour l'overlay
    UpdateSharedState();
}

// HandleNetworkError - Erreur du serveur
void HandleNetworkError(const NetworkErrorMessage& msg) {
    LOG_ERROR("Network error: %d", msg.error_code);
    m_error_state = msg.error_code;
    // Déclencher déconnexion si erreur fatale
    if (IsFatalError(msg.error_code)) {
        DisconnectFromServer();
    }
}

// HandlePing - Echo ping (keepalive)
void HandlePing(const PingMessage& msg) {
    if (msg.requester == 0) {
        // Serveur demande un echo
        SendPingReply(msg.id);
    }
    // Mettre à jour RTT si c'est notre réponse
    UpdateRtt(msg.id);
}
```

#### Tâches techniques
1. ⬜ Implémenter HandleInitialize()
2. ⬜ Implémenter HandleConnected()
3. ⬜ Implémenter HandleSyncNetwork()
4. ⬜ Implémenter HandleNetworkError()
5. ⬜ Implémenter HandlePing()
6. ⬜ Tests unitaires pour chaque handler

#### Fichiers modifiés
- `sysmodule/source/ldn/ldn_packet_dispatcher.cpp`
- `sysmodule/source/ldn/ldn_icommunication.cpp`
- `tests/ldn_session_handler_tests.cpp` (nouveau)

---

### Story 7.3 : Discovery Handlers (Scan, ScanReply)

**Priorité** : P0
**Estimation** : 2 points
**Statut** : `pending`
**Dépendances** : Story 7.1

#### Description
Implémenter les handlers pour la découverte de réseaux : résultats de scan.

#### Critères d'acceptation
- [ ] HandleScanReply() - Ajouter réseau à la liste des réseaux disponibles
- [ ] HandleScanReplyEnd() - Signaler fin de scan au jeu
- [ ] Buffer pour stocker les réseaux trouvés (max 24)
- [ ] Thread-safe access aux résultats de scan

#### Handlers détaillés

```cpp
// HandleScanReply - Un réseau trouvé
void HandleScanReply(const NetworkInfo& info) {
    std::lock_guard<std::mutex> lock(m_scan_mutex);
    if (m_scan_results.size() < MAX_SCAN_RESULTS) {
        m_scan_results.push_back(info);
    }
}

// HandleScanReplyEnd - Fin de la liste
void HandleScanReplyEnd() {
    std::lock_guard<std::mutex> lock(m_scan_mutex);
    m_scan_complete = true;
    m_scan_event.Signal();  // Réveiller le thread qui attend
}
```

#### Tâches techniques
1. ⬜ Implémenter HandleScanReply()
2. ⬜ Implémenter HandleScanReplyEnd()
3. ⬜ Ajouter m_scan_results vector avec mutex
4. ⬜ Modifier Scan() pour attendre les résultats
5. ⬜ Tests unitaires

#### Fichiers modifiés
- `sysmodule/source/ldn/ldn_packet_dispatcher.cpp`
- `sysmodule/source/ldn/ldn_icommunication.hpp` (ajouter scan_results)
- `sysmodule/source/ldn/ldn_icommunication.cpp`
- `tests/ldn_discovery_handler_tests.cpp` (nouveau)

---

### Story 7.4 : Control Handlers (Reject, SetAcceptPolicy)

**Priorité** : P1
**Estimation** : 2 points
**Statut** : `pending`
**Dépendances** : Story 7.2

#### Description
Implémenter les handlers pour le contrôle de session : rejet de joueurs, politique d'acceptation.

#### Critères d'acceptation
- [ ] HandleReject() - Traiter le rejet (host nous rejette ou on rejette)
- [ ] HandleRejectReply() - Confirmation de notre demande de rejet
- [ ] Stocker disconnect_reason pour le jeu
- [ ] Envoyer SetAcceptPolicy au serveur

#### Handlers détaillés

```cpp
// HandleReject - Nous sommes rejetés ou un joueur est rejeté
void HandleReject(const RejectRequest& msg) {
    if (msg.node_id == m_local_node_id) {
        // Nous sommes rejetés
        m_disconnect_reason = static_cast<DisconnectReason>(msg.disconnect_reason);
        m_state_machine.OnDisconnected();
        SignalStateEvent();
    } else {
        // Un autre joueur est rejeté - mise à jour via SyncNetwork
        LOG_INFO("Node %d rejected", msg.node_id);
    }
}

// HandleRejectReply - Notre demande de rejet a été traitée
void HandleRejectReply() {
    m_reject_event.Signal();
}
```

#### Tâches techniques
1. ⬜ Implémenter HandleReject()
2. ⬜ Implémenter HandleRejectReply()
3. ⬜ Ajouter send_set_accept_policy() dans TcpClient
4. ⬜ Tests unitaires

#### Fichiers modifiés
- `sysmodule/source/ldn/ldn_packet_dispatcher.cpp`
- `sysmodule/source/network/tcp_client.hpp/cpp`
- `tests/ldn_control_handler_tests.cpp` (nouveau)

---

### Story 7.5 : P2P Proxy Handlers (ProxyConfig, ProxyConnect)

**Priorité** : P0
**Estimation** : 4 points
**Statut** : `pending`
**Dépendances** : Story 7.2

#### Description
Implémenter les handlers pour la configuration et l'établissement des connexions P2P.

#### Critères d'acceptation
- [ ] HandleProxyConfig() - Stocker config du subnet virtuel
- [ ] HandleProxyConnect() - Demande de connexion P2P entrante
- [ ] HandleProxyConnectReply() - Réponse à notre demande P2P
- [ ] ProxyConnectionManager pour gérer les connexions actives

#### Handlers détaillés

```cpp
// HandleProxyConfig - Configuration du proxy P2P
void HandleProxyConfig(const ProxyConfig& config) {
    m_proxy_ip = config.proxy_ip;
    m_proxy_subnet_mask = config.proxy_subnet_mask;
    LOG_INFO("Proxy configured: %08X / %08X", m_proxy_ip, m_proxy_subnet_mask);
}

// HandleProxyConnect - Demande de connexion P2P entrante
void HandleProxyConnect(const ProxyConnectRequest& request) {
    // Enregistrer la connexion
    m_proxy_connections.AddConnection(request.info);

    // Répondre avec ProxyConnectReply
    ProxyConnectResponse response{};
    response.info = request.info;
    m_server_client.send_packet(PacketId::ProxyConnectReply, &response, sizeof(response));
}

// HandleProxyConnectReply - Réponse à notre demande
void HandleProxyConnectReply(const ProxyConnectResponse& response) {
    m_proxy_connections.ConfirmConnection(response.info);
}
```

#### Tâches techniques
1. ⬜ Créer `ldn_proxy_connection_manager.hpp/cpp`
2. ⬜ Implémenter HandleProxyConfig()
3. ⬜ Implémenter HandleProxyConnect()
4. ⬜ Implémenter HandleProxyConnectReply()
5. ⬜ Ajouter send_proxy_connect() dans TcpClient
6. ⬜ Tests unitaires

#### Fichiers créés/modifiés
- `sysmodule/source/ldn/ldn_proxy_connection_manager.hpp` (nouveau)
- `sysmodule/source/ldn/ldn_proxy_connection_manager.cpp` (nouveau)
- `sysmodule/source/ldn/ldn_packet_dispatcher.cpp`
- `tests/ldn_proxy_connect_tests.cpp` (nouveau)

---

### Story 7.6 : P2P Data Handlers (ProxyData, ProxyDisconnect)

**Priorité** : P0
**Estimation** : 5 points
**Statut** : `pending`
**Dépendances** : Story 7.5

#### Description
Implémenter le forwarding des données P2P - le coeur du jeu en ligne.

#### Critères d'acceptation
- [ ] HandleProxyData() - Recevoir données et les stocker dans LdnProxyBuffer
- [ ] HandleProxyDisconnect() - Fermer connexion P2P
- [ ] Interface pour que le jeu récupère les données (via IPC)
- [ ] Broadcast vs Unicast routing correct
- [ ] Performance < 5ms overhead

#### Handlers détaillés

```cpp
// HandleProxyData - Données de jeu reçues via tunnel P2P
void HandleProxyData(const ProxyDataHeader& header, const uint8_t* data, size_t size) {
    // Vérifier que c'est pour nous
    if (header.info.dest_ipv4 != m_ipv4_address &&
        header.info.dest_ipv4 != BROADCAST_IP) {
        return;  // Pas pour nous
    }

    // Stocker dans le buffer pour que le jeu le récupère
    if (!m_proxy_buffer.Write(header, data, size)) {
        LOG_WARN("Proxy buffer full, dropping packet");
    }

    // Signaler au jeu qu'il y a des données disponibles
    SignalDataAvailable();
}

// HandleProxyDisconnect - Connexion P2P fermée
void HandleProxyDisconnect(const ProxyDisconnectMessage& msg) {
    LOG_INFO("Proxy disconnect from %08X, reason: %d",
             msg.info.source_ipv4, msg.disconnect_reason);
    m_proxy_connections.RemoveConnection(msg.info);
}
```

#### Tâches techniques
1. ⬜ Implémenter HandleProxyData()
2. ⬜ Implémenter HandleProxyDisconnect()
3. ⬜ Intégrer avec LdnProxyBuffer existant
4. ⬜ Ajouter SignalDataAvailable() pour notifier le jeu
5. ⬜ Implémenter IPC pour récupérer les données (GetProxyData)
6. ⬜ Tests de performance (latence < 5ms)
7. ⬜ Tests unitaires

#### Fichiers modifiés
- `sysmodule/source/ldn/ldn_packet_dispatcher.cpp`
- `sysmodule/source/ldn/ldn_icommunication.cpp` (ajouter GetProxyData IPC)
- `tests/ldn_proxy_data_tests.cpp` (nouveau)

---

### Story 7.7 : Private Room Support

**Priorité** : P1
**Estimation** : 3 points
**Statut** : `pending`
**Dépendances** : Story 7.2, Story 7.3

#### Description
Ajouter le support des rooms privées avec mot de passe.

#### Critères d'acceptation
- [ ] CreateAccessPointPrivate() - Créer room avec passphrase
- [ ] ConnectPrivate() - Rejoindre room avec passphrase
- [ ] Passphrase envoyée après Initialize si nécessaire

#### Structures additionnelles

```cpp
// CreateAccessPointPrivateRequest - 0xC0 bytes
struct CreateAccessPointPrivateRequest {
    SecurityConfig security_config;      // 0x44
    UserConfig user_config;              // 0x30
    NetworkConfig network_config;        // 0x20
    RyuNetworkConfig ryu_network_config; // 0x28
    // + passphrase data in security_config
};

// ConnectPrivateRequest - similar to ConnectRequest + passphrase
```

#### Tâches techniques
1. ⬜ Ajouter send_create_access_point_private() dans TcpClient
2. ⬜ Ajouter send_connect_private() dans TcpClient
3. ⬜ Implémenter CreateAccessPointPrivate() IPC
4. ⬜ Implémenter ConnectPrivate() IPC
5. ⬜ Tests unitaires

#### Fichiers modifiés
- `sysmodule/source/network/tcp_client.hpp/cpp`
- `sysmodule/source/ldn/ldn_icommunication.hpp/cpp`
- `tests/ldn_private_room_tests.cpp` (nouveau)

---

### Story 7.8 : Integration & Testing

**Priorité** : P0
**Estimation** : 3 points
**Statut** : `pending`
**Dépendances** : Stories 7.1-7.6

#### Description
Tests d'intégration complets et validation end-to-end.

#### Critères d'acceptation
- [ ] Test complet du flux: Initialize → Scan → Connect → ProxyData
- [ ] Test host: CreateAccessPoint → Accept connections → ProxyData
- [ ] Test déconnexion/reconnexion
- [ ] Test erreurs réseau et recovery
- [ ] Validation avec serveur RyuLDN réel (optionnel)

#### Tâches techniques
1. ⬜ Écrire tests d'intégration complets
2. ⬜ Simuler flux complet client
3. ⬜ Simuler flux complet host
4. ⬜ Tests de stress (nombreuses connexions)
5. ⬜ Documentation des flux

#### Fichiers créés
- `tests/ldn_integration_full_tests.cpp`
- `docs/packet-flow.md`

---

## Definition of Done (Epic)

- [ ] Tous les handlers de paquets implémentés (16 types)
- [ ] P2P proxy data forwarding fonctionnel
- [ ] Tests unitaires pour chaque handler (80%+ coverage)
- [ ] Tests d'intégration passent
- [ ] Performance validée (< 5ms latence ajoutée)
- [ ] Documentation complète
- [ ] Code review passé

## Fichiers à créer

| Fichier | Description |
|---------|-------------|
| `ldn_packet_dispatcher.hpp` | Dispatcher de paquets |
| `ldn_packet_dispatcher.cpp` | Implémentation handlers |
| `ldn_proxy_connection_manager.hpp` | Gestionnaire connexions P2P |
| `ldn_proxy_connection_manager.cpp` | Implémentation |
| `tests/ldn_packet_dispatcher_tests.cpp` | Tests dispatcher |
| `tests/ldn_session_handler_tests.cpp` | Tests session |
| `tests/ldn_discovery_handler_tests.cpp` | Tests scan |
| `tests/ldn_proxy_connect_tests.cpp` | Tests P2P connect |
| `tests/ldn_proxy_data_tests.cpp` | Tests P2P data |
| `tests/ldn_integration_full_tests.cpp` | Tests intégration |

## Fichiers à modifier

| Fichier | Modifications |
|---------|---------------|
| `ldn_icommunication.hpp` | Ajouter scan_results, proxy config |
| `ldn_icommunication.cpp` | Setup callback, update loop |
| `tcp_client.hpp` | Nouvelles méthodes send |
| `tcp_client.cpp` | Implémentation |
| `ryu_protocol.hpp` | Nouveaux encoders si nécessaire |

## Référence: Correspondance avec Ryujinx

| Ryujinx File | Sysmodule Equivalent |
|--------------|---------------------|
| `RyuLdnProtocol.cs` | `ldn_packet_dispatcher.cpp` |
| `LdnMasterProxyClient.cs` | `ldn_icommunication.cpp` |
| `P2pProxyServer.cs` | `ldn_proxy_connection_manager.cpp` |
| `Station.cs` / `AccessPoint.cs` | Logique dans `ldn_icommunication.cpp` |

## Notes techniques

1. **Thread safety**: Tous les handlers doivent être thread-safe car appelés depuis le thread réseau
2. **Événements**: Utiliser des events/signals pour notifier le jeu (pas de polling)
3. **Buffer management**: LdnProxyBuffer existant gère le ring buffer
4. **Latence**: Minimiser les copies de données pour le P2P
