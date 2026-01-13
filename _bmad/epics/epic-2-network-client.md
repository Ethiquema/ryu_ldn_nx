# Epic 2 : Client Réseau

**Priorité** : P0 (Critique)
**Estimation** : 1.5 sprints
**Dépendances** : Epic 0, Epic 1

## Description

Implémenter le client TCP qui maintient la connexion au serveur ryu_ldn, gère la reconnexion automatique, et fournit une interface pour envoyer/recevoir des paquets.

## Valeur utilisateur

> En tant que **joueur Switch**, je veux une connexion réseau fiable et automatique au serveur, pour ne pas avoir à gérer manuellement les problèmes de connexion.

## Objectifs

- [ ] Connexion TCP au serveur ryu_ldn
- [ ] Reconnexion automatique avec backoff exponentiel
- [ ] Envoi/réception de paquets
- [ ] Gestion timeout et keepalive
- [ ] Thread-safe

---

## Stories

### Story 2.1 : Configuration Manager

**Priorité** : P0
**Estimation** : 2 points
**Statut** : `pending`

#### Description
Implémenter le parsing du fichier de configuration INI pour récupérer l'adresse du serveur et les options.

#### Critères d'acceptation
- [ ] Parser fichier INI basique
- [ ] Lecture `[server]` host/port
- [ ] Lecture `[network]` timeouts
- [ ] Lecture `[debug]` options
- [ ] Valeurs par défaut si fichier absent
- [ ] Pas d'allocation dynamique

#### Tâches techniques
1. Créer `sysmodule/source/config/config.hpp`
2. Créer `sysmodule/source/config/config.cpp`
3. Structure `Config` avec tous les champs
4. Fonction `load_config(path)`
5. Parser INI simple (ligne par ligne)
6. Valeurs par défaut hardcodées

#### Tests
- [ ] Parse config valide
- [ ] Valeurs par défaut si fichier manquant
- [ ] Valeurs par défaut si section manquante
- [ ] Ignore lignes commentaires (;)

---

### Story 2.2 : Socket TCP de base

**Priorité** : P0
**Estimation** : 3 points
**Statut** : `pending`
**Dépendances** : Story 2.1

#### Description
Créer le wrapper socket TCP utilisant les APIs BSD de libnx.

#### Critères d'acceptation
- [ ] Création socket TCP
- [ ] Connect avec timeout
- [ ] Send bloquant
- [ ] Recv non-bloquant
- [ ] Close propre
- [ ] Gestion erreurs

#### Tâches techniques
1. Créer `sysmodule/source/network/socket.hpp`
2. Créer `sysmodule/source/network/socket.cpp`
3. Initialisation socket BSD
4. `connect()` avec timeout via `poll()`
5. `send()` avec gestion EAGAIN
6. `recv()` non-bloquant
7. `close()` avec cleanup

#### Tests
- [ ] Connect à serveur valide réussit
- [ ] Connect à serveur invalide timeout
- [ ] Send données réussit
- [ ] Recv données réussit

---

### Story 2.3 : Machine à états connexion

**Priorité** : P0
**Estimation** : 3 points
**Statut** : `pending`
**Dépendances** : Story 2.2

#### Description
Implémenter la machine à états pour gérer le cycle de vie de la connexion.

#### Critères d'acceptation
- [ ] États : Disconnected, Connecting, Connected, Backoff, Retrying
- [ ] Transitions correctes
- [ ] Callbacks sur changement d'état
- [ ] Thread-safe

#### Tâches techniques
1. Définir enum `ConnectionState`
2. Créer classe `ConnectionStateMachine`
3. Méthodes de transition
4. Mutex pour thread-safety
5. Event pour notifier changements

#### Tests
- [ ] Transition Disconnected → Connecting
- [ ] Transition Connecting → Connected (succès)
- [ ] Transition Connecting → Backoff (échec)
- [ ] Transition Backoff → Retrying

---

### Story 2.4 : Logique de reconnexion

**Priorité** : P0
**Estimation** : 3 points
**Statut** : `pending`
**Dépendances** : Story 2.3

#### Description
Implémenter le backoff exponentiel et la logique de reconnexion automatique.

#### Critères d'acceptation
- [ ] Backoff initial : 1 seconde
- [ ] Multiplicateur : 2x
- [ ] Backoff max : 30 secondes
- [ ] Retry infini tant que demandé
- [ ] Reset backoff sur connexion réussie

#### Tâches techniques
1. Créer `sysmodule/source/network/reconnect.hpp`
2. Créer `sysmodule/source/network/reconnect.cpp`
3. Classe `ReconnectManager`
4. Calcul délai avec jitter optionnel
5. Timer pour déclencher retry
6. Méthode `reset()` sur succès

#### Tests
- [ ] Premier retry après 1s
- [ ] Deuxième retry après 2s
- [ ] Troisième retry après 4s
- [ ] Plafonné à 30s
- [ ] Reset remet à 1s

---

### Story 2.5 : Client RyuLdn

**Priorité** : P0
**Estimation** : 5 points
**Statut** : `pending`
**Dépendances** : Stories 2.1-2.4, Epic 1

#### Description
Assembler tous les composants pour créer le client réseau complet.

#### Critères d'acceptation
- [ ] Méthode `connect()`
- [ ] Méthode `disconnect()`
- [ ] Méthode `send_packet<T>()`
- [ ] Callback `on_packet_received`
- [ ] Gestion thread réseau séparé
- [ ] Thread-safe

#### Tâches techniques
1. Créer `sysmodule/source/network/client.hpp`
2. Créer `sysmodule/source/network/client.cpp`
3. Classe `RyuLdnClient`
4. Thread réseau pour recv loop
5. Queue thread-safe pour envoi
6. Intégration avec ReconnectManager
7. Callbacks pour paquets reçus

#### Tests
- [ ] Connect au serveur réussit
- [ ] Handshake Initialize réussit
- [ ] Envoi paquet réussit
- [ ] Réception paquet déclenche callback

---

### Story 2.6 : Handshake protocole

**Priorité** : P0
**Estimation** : 2 points
**Statut** : `pending`
**Dépendances** : Story 2.5

#### Description
Implémenter le handshake initial avec le serveur (Initialize + version negotiation).

#### Critères d'acceptation
- [ ] Envoi Initialize après connexion TCP
- [ ] Attente réponse serveur
- [ ] Vérification version compatible
- [ ] Erreur si version incompatible

#### Tâches techniques
1. Générer MAC address unique
2. Créer message Initialize
3. Envoyer après connect TCP
4. Parser réponse
5. Valider version
6. Gérer erreur VersionMismatch

#### Tests
- [ ] Handshake avec serveur compatible réussit
- [ ] Handshake avec version incompatible échoue

---

### Story 2.7 : Ping/Keepalive

**Priorité** : P1
**Estimation** : 2 points
**Statut** : `pending`
**Dépendances** : Story 2.5

#### Description
Implémenter le mécanisme de ping pour maintenir la connexion active et détecter les déconnexions.

#### Critères d'acceptation
- [ ] Envoi ping périodique (config: ping_interval)
- [ ] Détection timeout si pas de réponse
- [ ] Déclenche reconnexion si timeout

#### Tâches techniques
1. Timer pour envoi ping
2. Envoi paquet Ping
3. Attente réponse (optionnel selon protocole)
4. Timeout détection
5. Trigger reconnexion

#### Tests
- [ ] Ping envoyé selon intervalle
- [ ] Timeout déclenche reconnexion

---

### Story 2.8 : Tests client réseau

**Priorité** : P0
**Estimation** : 2 points
**Statut** : `pending`
**Dépendances** : Stories 2.1-2.7

#### Description
Écrire les tests unitaires et d'intégration pour le client réseau.

#### Critères d'acceptation
- [ ] Tests unitaires pour chaque composant
- [ ] Tests intégration avec mock server
- [ ] Tests reconnexion
- [ ] Couverture > 80%

#### Tâches techniques
1. Créer `tests/network_tests.cpp`
2. Créer mock server simple
3. Tests ConfigManager
4. Tests socket de base
5. Tests state machine
6. Tests reconnect logic
7. Tests client complet

#### Tests
- [ ] Tous les tests passent
- [ ] Couverture > 80%

---

## Definition of Done (Epic)

- [ ] Client se connecte au serveur ryu_ldn
- [ ] Handshake Initialize réussit
- [ ] Reconnexion automatique fonctionne
- [ ] Ping keepalive fonctionne
- [ ] Thread-safe et stable
- [ ] Tests passent (couverture > 80%)
- [ ] Documentation Doxygen complète
- [ ] Code review passé

## Notes

Ce module utilise les APIs BSD de libnx. Attention aux particularités :
- `socketInitialize()` requis avant utilisation
- Transfer memory pour buffers socket
- Pas de DNS resolver standard (utiliser `nifm`)
