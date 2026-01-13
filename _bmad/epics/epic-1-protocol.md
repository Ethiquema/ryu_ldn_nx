# Epic 1 : Protocole RyuLdn

**Priorité** : P0 (Critique)
**Estimation** : 1 sprint
**Dépendances** : Epic 0

## Description

Implémenter le protocole binaire RyuLdn en C++ pour communiquer avec les serveurs Ryujinx. Ce composant est le cœur de la communication réseau.

## Valeur utilisateur

> En tant que **sysmodule**, je dois pouvoir encoder et décoder les paquets RyuLdn pour communiquer avec le serveur de jeu en ligne.

## Objectifs

- [x] Structures de données compatibles bit-à-bit avec le serveur C#
- [x] Encoder tous les types de paquets
- [x] Décoder tous les types de paquets
- [x] Validation des paquets reçus
- [ ] Tests unitaires complets

---

## Stories

### Story 1.1 : Structures de données de base

**Priorité** : P0
**Estimation** : 3 points
**Statut** : `completed`

#### Description
Définir toutes les structures de données du protocole RyuLdn en C++, avec alignement identique au serveur C#.

#### Critères d'acceptation
- [x] `LdnHeader` (10 bytes, packed)
- [x] `NetworkInfo` (0x480 bytes)
- [x] `NodeInfo` structure
- [x] `MacAddress` structure
- [x] `NetworkId`, `SessionId`, `IntentId`
- [x] Enum `PacketId` complet
- [x] `static_assert` pour vérifier tailles

#### Tâches techniques
1. Créer `sysmodule/source/protocol/types.hpp`
2. Définir `LdnHeader` avec `__attribute__((packed))`
3. Définir `MacAddress` (6 bytes)
4. Définir `NetworkId`, `SessionId`, `IntentId`
5. Définir `NodeInfo` avec tous les champs
6. Définir `NetworkInfo` (structure complète 0x480)
7. Définir enum `PacketId`
8. Ajouter `static_assert(sizeof(NetworkInfo) == 0x480)`

#### Tests
- [x] `sizeof(LdnHeader) == 10`
- [x] `sizeof(NetworkInfo) == 0x480`
- [x] Layout binaire identique au serveur (test avec données connues)

---

### Story 1.2 : Structures de messages

**Priorité** : P0
**Estimation** : 2 points
**Statut** : `completed`
**Dépendances** : Story 1.1

#### Description
Définir les structures pour chaque type de message du protocole.

#### Critères d'acceptation
- [x] `InitializeMessage`
- [x] `CreateAccessPointRequest`
- [x] `ScanFilter`
- [x] `ConnectRequest`
- [x] `ProxyDataHeader`
- [x] Toutes les structures de réponse

#### Tâches techniques
1. Analyser code C# du serveur pour chaque message
2. Créer structures C++ correspondantes
3. Vérifier alignement avec `static_assert`
4. Documenter chaque structure (Doxygen)

#### Tests
- [x] Tailles correspondent au serveur C#
- [x] Sérialisation/désérialisation round-trip

---

### Story 1.3 : Encodeur de paquets

**Priorité** : P0
**Estimation** : 3 points
**Statut** : `completed`
**Dépendances** : Story 1.1, Story 1.2

#### Description
Implémenter les fonctions d'encodage pour créer des paquets à envoyer au serveur.

#### Critères d'acceptation
- [x] `encode_packet<T>(PacketId, const T&)` template
- [x] Écriture header + payload dans buffer
- [x] Gestion magic number et version
- [x] Pas d'allocation dynamique (buffer pré-alloué)

#### Tâches techniques
1. Créer `sysmodule/source/protocol/ryu_protocol.hpp`
2. ~~Créer `sysmodule/source/protocol/ryu_protocol.cpp`~~ (header-only)
3. Implémenter `encode_header()`
4. Implémenter template `encode_packet<T>()`
5. Fonctions spécialisées pour chaque type de message

#### Tests
- [x] Encode Initialize produit bytes corrects
- [x] Encode CreateAccessPoint produit bytes corrects
- [x] Magic number correct (0x4E444C52)
- [x] Version correct (1)

---

### Story 1.4 : Décodeur de paquets

**Priorité** : P0
**Estimation** : 3 points
**Statut** : `completed`
**Dépendances** : Story 1.1, Story 1.2

#### Description
Implémenter les fonctions de décodage pour parser les paquets reçus du serveur.

#### Critères d'acceptation
- [x] `decode_header(buffer)` retourne LdnHeader
- [x] `decode_packet<T>(buffer)` template
- [x] Validation magic, version, taille
- [x] Gestion erreurs (paquet invalide)

#### Tâches techniques
1. Implémenter `decode_header()` avec validation
2. Implémenter `check_complete_packet()` (validation + fragmentation)
3. Implémenter template `decode<T>()`
4. Gérer paquets partiels (fragmentation TCP)
5. Retourner codes erreur appropriés

#### Tests
- [x] Decode header valide réussit
- [x] Decode header magic invalide échoue
- [x] Decode header version invalide échoue
- [x] Decode header taille excessive échoue

---

### Story 1.5 : Gestionnaire de buffer

**Priorité** : P1
**Estimation** : 2 points
**Statut** : `completed`
**Dépendances** : Story 1.3, Story 1.4

#### Description
Implémenter la gestion des buffers pour accumulation des données TCP et gestion des paquets fragmentés.

#### Critères d'acceptation
- [x] Buffer linéaire pré-alloué (template 64KB par défaut)
- [x] Accumulation données TCP
- [x] Détection paquet complet
- [x] Extraction paquet (avec copie ou peek sans copie)

#### Tâches techniques
1. Créer classe `PacketBuffer<BufferSize>`
2. Méthode `append(data, size)`
3. Méthode `has_complete_packet()`
4. Méthode `extract_packet()` et `peek_packet()`
5. Gestion débordement buffer et `discard_until_valid()`

#### Tests
- [x] Paquet complet en une fois
- [x] Paquet fragmenté en 2 parties
- [x] Paquet fragmenté en N parties
- [x] Multiple paquets dans un buffer

---

### Story 1.6 : Tests protocole

**Priorité** : P0
**Estimation** : 2 points
**Statut** : `pending`
**Dépendances** : Stories 1.1-1.5

#### Description
Écrire les tests unitaires complets pour le module protocole.

#### Critères d'acceptation
- [ ] Tests pour chaque structure (taille, alignement)
- [ ] Tests encode/decode round-trip
- [ ] Tests validation paquets invalides
- [ ] Tests buffer fragmenté
- [ ] Couverture > 90%

#### Tâches techniques
1. Créer `tests/protocol_tests.cpp`
2. Tests structures avec `static_assert`
3. Tests encode avec données connues
4. Tests decode avec données connues
5. Tests round-trip (encode puis decode)
6. Tests erreurs

#### Tests
- [ ] Tous les tests passent
- [ ] Couverture mesurée > 90%

---

## Definition of Done (Epic)

- [ ] Toutes les structures définies et vérifiées
- [ ] Encodage fonctionne pour tous les types de paquets
- [ ] Décodage fonctionne pour tous les types de paquets
- [ ] Validation rejette paquets malformés
- [ ] Tests unitaires passent (couverture > 90%)
- [ ] Documentation Doxygen complète
- [ ] Code review passé

## Notes

Ce module est critique car il doit être **binaire-compatible** avec le serveur C#. Toute erreur d'alignement causera des bugs difficiles à diagnostiquer.

Référence : Analyser `LdnServer/Network/RyuLdnProtocol.cs` et `Ryujinx.HLE/HOS/Services/Ldn/UserServiceCreator/LdnRyu/RyuLdnProtocol.cs`