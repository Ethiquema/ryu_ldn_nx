# Index des Epics - ryu_ldn_nx

**Date** : 2026-01-15
**Total Epics** : 8
**Estimation totale** : ~10-12 sprints

---

## Vue d'ensemble

```
┌─────────────────────────────────────────────────────────────────┐
│                        ROADMAP                                   │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Sprint 1    ┌──────────────────────┐                           │
│              │ Epic 0: Infrastructure│                          │
│              │ (Docker, CI/CD)       │                          │
│              └──────────┬───────────┘                           │
│                         │                                        │
│  Sprint 2    ┌──────────▼───────────┐                           │
│              │ Epic 1: Protocole     │                          │
│              │ (RyuLdn encoding)     │                          │
│              └──────────┬───────────┘                           │
│                         │                                        │
│  Sprint 3-4  ┌──────────▼───────────┐                           │
│              │ Epic 2: Network Client│                          │
│              │ (TCP, reconnection)   │                          │
│              └──────────┬───────────┘                           │
│                         │                                        │
│  Sprint 5-6  ┌──────────▼───────────┐                           │
│              │ Epic 3: LDN Service   │                          │
│              │ (MITM IPC, gameplay)  │                          │
│              └──────────┬───────────┘                           │
│                         │                                        │
│  Sprint 7    ┌──────────▼───────────┐  ┌────────────────────┐   │
│              │ Epic 4: Tesla Overlay │  │ Epic 5: Docs/Release│  │
│              │ (UI, config)          │  │ (Starlight, CI)     │  │
│              └──────────────────────┘  └────────────────────┘   │
│                                                                  │
│                         ▼                                        │
│              ┌──────────────────────┐                           │
│              │     v1.0.0 RELEASE   │                           │
│              └──────────────────────┘                           │
└─────────────────────────────────────────────────────────────────┘
```

---

## Epics par priorité

### P0 - Critiques (MVP)

| Epic | Nom | Stories | Estimation | Dépendances | Statut |
|------|-----|---------|------------|-------------|--------|
| [0](epic-0-infrastructure.md) | Infrastructure Projet | 6 | 1 sprint | - | ✅ Done |
| [1](epic-1-protocol.md) | Protocole RyuLdn | 6 | 1 sprint | Epic 0 | ✅ Done |
| [2](epic-2-network-client.md) | Client Réseau | 8 | 1.5 sprints | Epic 0, 1 | ✅ Done |
| [3](epic-3-ldn-service.md) | Service LDN (MITM) | 9 | 2 sprints | Epic 0, 1, 2 | ✅ Done |
| [7](epic-7-packet-handlers.md) | Packet Handlers | 6 | 1 sprint | Epic 3 | 🔄 In Progress |
| **[8](epic-8-bsd-mitm.md)** | **BSD Socket MITM** | **9** | **3-4 sprints** | **Epic 3** | **🆕 New** |

### P1 - Importants

| Epic | Nom | Stories | Estimation | Dépendances |
|------|-----|---------|------------|-------------|
| [5](epic-5-docs-release.md) | Documentation & Release | 7 | 1 sprint | Epic 0-4 |

### P2 - Nice to have

| Epic | Nom | Stories | Estimation | Dépendances |
|------|-----|---------|------------|-------------|
| [4](epic-4-tesla-overlay.md) | Tesla Overlay | 6 | 1 sprint | Epic 0, 3 |

---

## Statistiques

### Par statut

| Statut | Stories |
|--------|---------|
| Pending | 3 |
| In Progress | 0 |
| Completed | 39 |

### Par priorité

| Priorité | Stories |
|----------|---------|
| P0 | 28 |
| P1 | 8 |
| P2 | 5 |
| P3 | 1 |

### Estimation totale

| Métrique | Valeur |
|----------|--------|
| Total stories | 42 |
| Total points | ~100 |
| Sprints estimés | 7-8 |

---

## Chemin critique

Le chemin critique pour le MVP est :

```
Epic 0 → Epic 1 → Epic 2 → Epic 3 → (MVP Fonctionnel)
```

L'Epic 4 (Overlay) et Epic 5 (Docs) peuvent être parallélisés après Epic 3.

---

## Critères MVP

Le MVP est atteint quand :

- [x] Sysmodule s'installe sur Switch
- [ ] Connexion au serveur ryu_ldn réussit
- [ ] Mario Kart 8 Deluxe peut créer une partie
- [ ] Mario Kart 8 Deluxe peut rejoindre une partie
- [ ] Session stable pendant 1 heure

---

## Notes

- Chaque sprint = 2 semaines (estimation)
- Les estimations en points utilisent la séquence Fibonacci (1, 2, 3, 5, 8)
- Les dépendances sont strictes - un epic doit être complété avant de commencer le suivant
- Le développement TDD signifie que les tests sont écrits AVANT le code

---

*Document généré via workflow BMM `/create-epics-and-stories`*
