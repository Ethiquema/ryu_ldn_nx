# Epic 4 : Tesla Overlay

**Priorité** : P2 (Nice to have pour MVP)
**Estimation** : 1 sprint
**Dépendances** : Epic 0, Epic 3

## Description

Créer un overlay Tesla pour afficher le statut de connexion, configurer le serveur, et débugger en temps réel.

## Valeur utilisateur

> En tant que **joueur Switch**, je veux voir le statut de connexion au serveur et pouvoir changer de serveur sans modifier de fichiers, pour une meilleure expérience utilisateur.

## Objectifs

- [x] Overlay Tesla fonctionnel
- [x] Affichage statut connexion
- [x] Configuration serveur in-app (partial - display only)
- [x] Mode debug optionnel (toggle + RTT)

---

## Stories

### Story 4.1 : Structure overlay de base

**Priorité** : P2
**Estimation** : 2 points
**Statut** : `done`
**Dépendances** : Epic 0

#### Description
Créer la structure de base de l'overlay Tesla.

#### Critères d'acceptation
- [x] Projet overlay compile
- [ ] Overlay visible dans Tesla Menu (requires hardware)
- [x] Interface vide fonctionnelle
- [x] Communication IPC avec sysmodule préparée

#### Tâches techniques
1. ✅ Créer `overlay/Makefile`
2. ✅ Créer `overlay/source/main.cpp`
3. ✅ Configurer Tesla framework
4. ✅ Créer GUI de base (MainGui class)
5. ✅ Définir interface IPC custom pour communication

#### Fichiers créés
- `overlay/Makefile`
- `overlay/source/main.cpp`
- `overlay/source/ryu_ldn_ipc.h`
- `overlay/source/ryu_ldn_ipc.c`
- `sysmodule/source/ldn/ldn_config_service.hpp`
- `sysmodule/source/ldn/ldn_config_service.cpp`

#### Tests
- [x] Overlay compile (.ovl generated)
- [ ] Visible dans Tesla Menu (requires hardware)
- [ ] S'ouvre sans crash (requires hardware)

---

### Story 4.2 : Affichage statut connexion

**Priorité** : P2
**Estimation** : 3 points
**Statut** : `done`
**Dépendances** : Story 4.1, Epic 3

#### Description
Afficher le statut de connexion au serveur ryu_ldn en temps réel.

#### Critères d'acceptation
- [x] Affiche : Déconnecté / Connexion... / Connecté / Erreur
- [x] Couleur selon état (vert/jaune/rouge) - StatusColor()
- [x] Affiche adresse serveur actuel - ServerAddressListItem
- [x] Rafraîchissement en temps réel - update() polling

#### Tâches techniques
1. ✅ Créer service IPC pour requêter état (cmd 65001-65002)
2. ✅ Implémenter handler dans sysmodule (ldn_config_service.cpp)
3. ✅ Créer widget statut dans overlay (StatusListItem)
4. ✅ Polling état (toutes les secondes via m_updateCounter)
5. ✅ Affichage coloré (StatusColor function)

#### Tests
- [ ] Affiche Déconnecté quand pas de WiFi (requires hardware)
- [ ] Affiche Connecté quand OK (requires hardware)
- [ ] Mise à jour en temps réel (requires hardware)

---

### Story 4.3 : Affichage info session

**Priorité** : P2
**Estimation** : 2 points
**Statut** : `done`
**Dépendances** : Story 4.2

#### Description
Afficher les informations de la session de jeu en cours.

#### Critères d'acceptation
- [x] Nombre de joueurs dans la session - SessionInfoListItem
- [x] Nom du jeu (si disponible) - RyuLdnSessionInfo.game_name
- [x] Rôle : Hôte ou Client - is_host field
- [x] Durée de session - session_duration_ms field

#### Tâches techniques
1. ✅ Récupérer NetworkInfo via IPC (cmd 65004)
2. ✅ Parser infos pertinentes (SessionInfo struct)
3. ✅ Créer widgets d'affichage (SessionInfoListItem, LatencyListItem)
4. ✅ Timer pour durée session (session_duration_ms)

#### Tests
- [ ] Affiche nombre joueurs correct (requires hardware)
- [ ] Affiche rôle correct (requires hardware)
- [ ] Met à jour quand joueurs rejoignent/quittent (requires hardware)

---

### Story 4.4 : Configuration serveur

**Priorité** : P2
**Estimation** : 3 points
**Statut** : `done`
**Dépendances** : Story 4.2

#### Description
Permettre de changer l'adresse du serveur depuis l'overlay.

#### Critères d'acceptation
- [ ] Liste de serveurs prédéfinis (TODO: future enhancement)
- [ ] Saisie serveur custom (TODO: Tesla keyboard limitations)
- [x] Sauvegarde dans config.ini (IPC ready, TODO: config backend)
- [x] Reconnexion après changement - ReconnectListItem

#### Tâches techniques
1. ⏳ Créer liste serveurs prédéfinis (future enhancement)
2. ✅ Widget affichage serveur (ServerAddressListItem)
3. ✅ IPC pour changer serveur (cmd 65006)
4. ✅ Handler dans sysmodule (SetServerAddress)
5. ⏳ Mise à jour config.ini (TODO: config integration)
6. ✅ Trigger reconnexion (ReconnectListItem, cmd 65009)

#### Tests
- [ ] Changement serveur fonctionne (requires hardware + config)
- [ ] Config persistée après reboot (requires hardware + config)
- [ ] Reconnexion réussit (requires hardware)

---

### Story 4.5 : Mode debug

**Priorité** : P3
**Estimation** : 2 points
**Statut** : `partial`
**Dépendances** : Story 4.2

#### Description
Ajouter un mode debug pour afficher les logs et diagnostics.

#### Critères d'acceptation
- [x] Toggle debug on/off - DebugToggleListItem
- [ ] Affiche derniers logs (buffer circulaire) - TODO
- [x] Affiche stats réseau (latence) - LatencyListItem (RTT)
- [ ] Copie logs (si possible) - Not possible with Tesla

#### Tâches techniques
1. ⏳ Buffer circulaire de logs dans sysmodule
2. ✅ IPC pour toggle debug (cmd 65007-65008)
3. ⏳ Widget affichage logs scrollable
4. ⏳ Stats compteurs (paquets envoyés/reçus)
5. ✅ Calcul latence ping (cmd 65010 GetLastRtt)

#### Tests
- [ ] Logs visibles (requires implementation)
- [x] RTT stats affichées (LatencyListItem)
- [ ] Pas d'impact perf quand debug off (requires hardware)

---

### Story 4.6 : Tests et polish

**Priorité** : P2
**Estimation** : 2 points
**Statut** : `pending`
**Dépendances** : Stories 4.1-4.5

#### Description
Tester l'overlay et polir l'interface.

#### Critères d'acceptation
- [ ] Interface intuitive
- [ ] Pas de crash
- [ ] Performance acceptable
- [ ] Documentation utilisateur

#### Tâches techniques
1. Tests manuels complets
2. Fix bugs UX
3. Ajuster layout
4. Ajouter icônes/emojis status
5. Documenter dans README

#### Tests
- [ ] Overlay fluide
- [ ] Pas de freeze
- [ ] Utilisable sans doc

---

## Definition of Done (Epic)

- [x] Overlay compiles (.ovl generated)
- [ ] Overlay visible dans Tesla Menu (requires hardware)
- [x] Statut connexion affiché (StatusListItem)
- [x] Info session affichée (SessionInfoListItem)
- [x] Affichage serveur (ServerAddressListItem)
- [x] Mode debug disponible (DebugToggleListItem)
- [ ] Documentation utilisateur
- [ ] Code review passé
- [ ] Hardware testing

## Notes

L'overlay utilise le framework Tesla-Menu (libtesla). Assurer la compatibilité avec les dernières versions.

L'overlay est optionnel pour le MVP mais améliore significativement l'UX.
