# Epic 6 : Runtime LDN Info + Config Lock

**Priorité** : P1 (Post-MVP Enhancement)
**Estimation** : 1 sprint
**Dépendances** : Epic 3, Epic 4
**Statut** : `in_progress`

## Description

Réimplémenter les fonctionnalités runtime (LDN State, Session Info, Latency, Force Reconnect) qui étaient disponibles dans l'ancien design MITM-only, et les exposer via le service standalone `ryu:cfg`. Ces informations ne sont visibles que quand un jeu utilise activement le LDN. L'édition de la configuration est bloquée pendant qu'un jeu est actif pour éviter les conflits.

## Valeur utilisateur

> En tant que **joueur Switch**, je veux voir l'état de ma session LDN en temps réel pendant le jeu (latence, nombre de joueurs, état de connexion), et je comprends que la configuration ne peut pas être modifiée pendant une partie pour éviter les déconnexions.

## Objectifs

- [ ] SharedState singleton pour partager l'état runtime entre MITM et ryu:cfg
- [ ] Commandes IPC 23-28 pour les données runtime
- [ ] Overlay affiche runtime info uniquement quand un jeu est actif
- [ ] Configuration bloquée pendant le jeu
- [ ] Tests unitaires pour SharedState et IPC
- [ ] Documentation complète

## Architecture

```
┌─────────────────────┐     ┌─────────────────────┐
│   ldn:u MITM        │     │     ryu:cfg         │
│  (pendant jeu)      │     │   (toujours actif)  │
├─────────────────────┤     ├─────────────────────┤
│ ICommunicationSvc   │───▶│   ConfigService     │
│  - SetGameActive()  │     │  - IsGameActive()   │
│  - SetLdnState()    │     │  - GetLdnState()    │
│  - SetSessionInfo() │     │  - GetSessionInfo() │
│  - SetLastRtt()     │     │  - GetLastRtt()     │
└─────────────────────┘     └─────────────────────┘
           │                         │
           ▼                         ▼
    ┌─────────────────────────────────────┐
    │         SharedState (singleton)     │
    │  - m_game_active, m_process_id      │
    │  - m_ldn_state, m_session_info      │
    │  - m_last_rtt_ms, m_reconnect_req   │
    └─────────────────────────────────────┘
```

---

## Stories

### Story 6.1 : SharedState Singleton (Test-First)

**Priorité** : P1
**Estimation** : 3 points
**Statut** : `in_progress`
**Dépendances** : Epic 3

#### Description

Créer un singleton thread-safe pour partager l'état runtime entre le service MITM et le service de configuration.

#### Critères d'acceptation

- [ ] Singleton accessible globalement via `SharedState::GetInstance()`
- [ ] Thread-safe avec mutex pour toutes les opérations
- [ ] Gestion de l'état du jeu (actif/inactif, process ID)
- [ ] Gestion de l'état LDN (CommState)
- [ ] Gestion des infos session (node_count, max_nodes, local_id, is_host)
- [ ] Gestion du RTT (dernière valeur en ms)
- [ ] Mécanisme de demande de reconnexion (flag consommable)

#### Tests (Test-First)

```cpp
// tests/shared_state_tests.cpp - À ÉCRIRE EN PREMIER
TEST(singleton_returns_same_instance)
TEST(initially_game_not_active)
TEST(initially_process_id_zero)
TEST(set_game_active_true)
TEST(set_game_active_false_resets_pid)
TEST(set_game_active_false_resets_ldn_state)
TEST(set_game_active_false_resets_session_info)
TEST(initially_ldn_state_none)
TEST(set_ldn_state_initialized)
TEST(set_ldn_state_station_connected)
TEST(set_ldn_state_access_point_created)
TEST(set_ldn_state_error)
TEST(ldn_state_transitions)
TEST(initially_session_info_empty)
TEST(set_session_info_as_host)
TEST(set_session_info_as_client)
TEST(update_session_info_node_count)
TEST(initially_rtt_zero)
TEST(set_last_rtt)
TEST(update_last_rtt)
TEST(rtt_typical_values)
TEST(initially_no_reconnect_request)
TEST(request_reconnect_sets_flag)
TEST(consume_reconnect_clears_flag)
TEST(multiple_reconnect_requests)
TEST(reconnect_after_consume)
TEST(full_game_session_lifecycle)
TEST(host_session_lifecycle)
```

#### Tâches techniques

1. ⬜ Écrire tests unitaires (shared_state_tests.cpp)
2. ⬜ Créer `ldn_shared_state.hpp` avec interface
3. ⬜ Implémenter `ldn_shared_state.cpp`
4. ⬜ Ajouter méthode `Reset()` pour les tests
5. ⬜ Vérifier que tous les tests passent
6. ⬜ Documenter l'API avec Doxygen

#### Fichiers créés

- `sysmodule/source/ldn/ldn_shared_state.hpp`
- `sysmodule/source/ldn/ldn_shared_state.cpp`
- `tests/shared_state_tests.cpp`

---

### Story 6.2 : Commandes IPC Runtime (Test-First)

**Priorité** : P1
**Estimation** : 3 points
**Statut** : `pending`
**Dépendances** : Story 6.1

#### Description

Ajouter les commandes IPC 23-28 au service `ryu:cfg` pour exposer les données runtime.

#### Nouvelles commandes

| ID | Commande | In | Out | Description |
|----|----------|-----|-----|-------------|
| 23 | IsGameActive | - | u32 | 1 si un jeu utilise LDN |
| 24 | GetLdnState | - | u32 | CommState (0-6) |
| 25 | GetSessionInfo | - | struct(8B) | node_count, max, local_id, is_host |
| 26 | GetLastRtt | - | u32 | Latence en ms |
| 27 | ForceReconnect | - | - | Demande reconnexion |
| 28 | GetActiveProcessId | - | u64 | PID du jeu (debug) |

#### Critères d'acceptation

- [ ] Commandes 23-28 enregistrées dans ConfigService
- [ ] IsGameActive retourne état correct depuis SharedState
- [ ] GetLdnState retourne CommState depuis SharedState
- [ ] GetSessionInfo retourne struct avec infos session
- [ ] GetLastRtt retourne RTT depuis SharedState
- [ ] ForceReconnect définit le flag dans SharedState
- [ ] GetActiveProcessId retourne PID depuis SharedState

#### Tests (Test-First)

```cpp
// tests/config_ipc_service_tests.cpp - AJOUTER ces tests
TEST(config_ipc_is_game_active_returns_false_initially)
TEST(config_ipc_is_game_active_returns_true_when_set)
TEST(config_ipc_get_ldn_state_returns_none_initially)
TEST(config_ipc_get_ldn_state_returns_correct_state)
TEST(config_ipc_get_session_info_returns_empty_initially)
TEST(config_ipc_get_session_info_returns_correct_data)
TEST(config_ipc_get_last_rtt_returns_zero_initially)
TEST(config_ipc_get_last_rtt_returns_correct_value)
TEST(config_ipc_force_reconnect_sets_flag)
TEST(config_ipc_get_active_process_id_returns_zero_initially)
TEST(config_ipc_get_active_process_id_returns_correct_pid)
```

#### Tâches techniques

1. ⬜ Écrire tests unitaires IPC
2. ⬜ Ajouter enum RyuCfgCmd_* (23-28) dans header
3. ⬜ Implémenter IsGameActive dans ConfigService
4. ⬜ Implémenter GetLdnState
5. ⬜ Implémenter GetSessionInfo avec struct
6. ⬜ Implémenter GetLastRtt
7. ⬜ Implémenter ForceReconnect
8. ⬜ Implémenter GetActiveProcessId
9. ⬜ Vérifier tous les tests passent
10. ⬜ Documenter les commandes IPC

#### Fichiers modifiés

- `sysmodule/source/config/config_ipc_service.hpp`
- `sysmodule/source/config/config_ipc_service.cpp`
- `tests/config_ipc_service_tests.cpp`

---

### Story 6.3 : Intégration SharedState dans MITM

**Priorité** : P1
**Estimation** : 2 points
**Statut** : `pending`
**Dépendances** : Story 6.1

#### Description

Mettre à jour ICommunicationService pour alimenter SharedState avec les données runtime.

#### Critères d'acceptation

- [ ] Initialize() appelle SetGameActive(true, pid)
- [ ] Finalize() appelle SetGameActive(false, 0)
- [ ] Transitions d'état appellent SetLdnState()
- [ ] Réception NetworkInfo met à jour session info
- [ ] Callback RTT met à jour SharedState
- [ ] Vérification reconnect_requested dans boucle principale

#### Tâches techniques

1. ⬜ Include ldn_shared_state.hpp dans ldn_icommunication.cpp
2. ⬜ Appeler SetGameActive dans Initialize/Finalize
3. ⬜ Appeler SetLdnState dans OnStateChanged()
4. ⬜ Parser NetworkInfo pour SetSessionInfo
5. ⬜ Ajouter callback RTT dans RyuLdnClient
6. ⬜ Vérifier ConsumeReconnectRequest() et déclencher reconnexion

#### Fichiers modifiés

- `sysmodule/source/ldn/ldn_icommunication.cpp`
- `sysmodule/source/network/client.hpp`
- `sysmodule/source/network/client.cpp`

---

### Story 6.4 : Client IPC Overlay Runtime

**Priorité** : P1
**Estimation** : 2 points
**Statut** : `pending`
**Dépendances** : Story 6.2

#### Description

Ajouter les fonctions client IPC dans l'overlay pour les commandes runtime.

#### Critères d'acceptation

- [ ] ryuLdnIsGameActive() fonctionne
- [ ] ryuLdnGetLdnState() retourne l'état
- [ ] ryuLdnGetSessionInfo() retourne struct complète
- [ ] ryuLdnGetLastRtt() retourne RTT
- [ ] ryuLdnForceReconnect() déclenche reconnexion
- [ ] Types RyuLdnSessionInfo et RyuLdnState définis

#### Tâches techniques

1. ⬜ Ajouter enum RyuCfgCmd_* (23-28) dans ryu_ldn_ipc.c
2. ⬜ Ajouter typedef RyuLdnState (CommState equivalent)
3. ⬜ Ajouter typedef RyuLdnSessionInfo struct
4. ⬜ Implémenter ryuLdnIsGameActive()
5. ⬜ Implémenter ryuLdnGetLdnState()
6. ⬜ Implémenter ryuLdnGetSessionInfo()
7. ⬜ Implémenter ryuLdnGetLastRtt()
8. ⬜ Implémenter ryuLdnForceReconnect()
9. ⬜ Mettre à jour ryu_ldn_ipc.h avec déclarations

#### Fichiers modifiés

- `overlay/source/ryu_ldn_ipc.h`
- `overlay/source/ryu_ldn_ipc.c`

---

### Story 6.5 : UI Conditionnelle Overlay

**Priorité** : P1
**Estimation** : 3 points
**Statut** : `pending`
**Dépendances** : Story 6.4

#### Description

Modifier l'overlay pour afficher les sections runtime/config selon l'état du jeu.

#### Critères d'acceptation

- [ ] Vérifie IsGameActive() à chaque rafraîchissement
- [ ] **Mode Jeu Actif**:
  - [ ] Affiche section "Runtime" (LDN State, Session, Latency)
  - [ ] Bouton "Force Reconnect" disponible
  - [ ] Configuration en lecture seule avec message "Config locked"
  - [ ] Tous les toggles/edits désactivés
- [ ] **Mode Sans Jeu**:
  - [ ] Affiche section "Configuration" normale
  - [ ] Section "Runtime" masquée ou message "No game active"
  - [ ] Tous les toggles/edits actifs

#### Tâches techniques

1. ⬜ Créer classe LdnStateListItem (affiche état CommState)
2. ⬜ Créer classe SessionInfoListItem (node count, role, etc.)
3. ⬜ Créer classe LatencyListItem (affiche RTT)
4. ⬜ Créer classe ReconnectListItem (bouton Force Reconnect)
5. ⬜ Créer classe ConfigLockedListItem (message informatif)
6. ⬜ Modifier MainGui::createUI() pour vérifier game active
7. ⬜ Ajouter logique de rafraîchissement conditionnel
8. ⬜ Tester transitions jeu actif ↔ inactif

#### Fichiers modifiés

- `overlay/source/main.cpp`

---

### Story 6.6 : Tests et Documentation

**Priorité** : P1
**Estimation** : 2 points
**Statut** : `pending`
**Dépendances** : Stories 6.1-6.5

#### Description

Finaliser les tests, documenter le code et mettre à jour la documentation utilisateur.

#### Critères d'acceptation

- [ ] Tous les tests unitaires passent
- [ ] Documentation Doxygen complète pour SharedState
- [ ] Documentation Doxygen pour nouvelles commandes IPC
- [ ] Mise à jour du README avec la nouvelle fonctionnalité
- [ ] Mise à jour des guides utilisateur

#### Tâches techniques

1. ⬜ Vérifier couverture tests SharedState
2. ⬜ Vérifier couverture tests IPC
3. ⬜ Ajouter commentaires Doxygen manquants
4. ⬜ Mettre à jour docs/architecture.md
5. ⬜ Mettre à jour README section overlay
6. ⬜ Mettre à jour guide utilisateur Starlight
7. ⬜ Build et vérifier génération docs

#### Fichiers modifiés

- `docs/src/content/docs/**/*.mdx`
- `README.md`
- Headers avec commentaires Doxygen

---

## Definition of Done (Epic)

- [ ] SharedState singleton implémenté et testé
- [ ] Commandes IPC 23-28 fonctionnelles
- [ ] MITM alimente SharedState correctement
- [ ] Overlay affiche runtime info quand jeu actif
- [ ] Config bloquée pendant le jeu
- [ ] Tous les tests passent (sysmodule + overlay compile)
- [ ] Documentation complète
- [ ] Code review passé

## Vérification

1. **Sans jeu**: L'overlay affiche la config normale, pas de section runtime
2. **Avec jeu LDN actif**:
   - Section runtime visible (state, session, latency)
   - Config en lecture seule avec message "Config locked"
   - Bouton Force Reconnect disponible
3. **Tests unitaires**: `make test` passe avec nouveaux tests

## Fichiers à créer/modifier

### Nouveaux fichiers

- `sysmodule/source/ldn/ldn_shared_state.hpp`
- `sysmodule/source/ldn/ldn_shared_state.cpp`
- `tests/shared_state_tests.cpp`

### Fichiers modifiés

- `sysmodule/source/config/config_ipc_service.hpp`
- `sysmodule/source/config/config_ipc_service.cpp`
- `sysmodule/source/ldn/ldn_icommunication.cpp`
- `sysmodule/source/network/client.hpp`
- `sysmodule/source/network/client.cpp`
- `overlay/source/ryu_ldn_ipc.h`
- `overlay/source/ryu_ldn_ipc.c`
- `overlay/source/main.cpp`

## Notes

Cette fonctionnalité était présente dans l'ancien design où l'overlay communiquait directement avec le service MITM. Avec le nouveau design standalone `ryu:cfg`, il faut un mécanisme de partage d'état (SharedState singleton) pour exposer ces informations runtime.

Le blocage de la config pendant le jeu est une décision UX pour éviter que l'utilisateur ne change la config (serveur, TLS, etc.) pendant une partie active, ce qui causerait une déconnexion.
