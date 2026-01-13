# Product Brief : ryu_ldn_nx

**Version** : 1.0
**Date** : 2026-01-12
**Statut** : Draft

---

## 1. Vision Produit

### Énoncé de Vision
Permettre aux joueurs Switch CFW de jouer en ligne via les serveurs Ryujinx avec la même simplicité qu'un jeu local - sans configuration réseau, sans PC en partage de connexion, sans outil pcap.

### Problème à Résoudre
Aujourd'hui, jouer en "LAN play" sur Switch modifiée est trop complexe :
- Nécessite un PC avec pcap en partage de connexion
- Configuration réseau avancée requise
- Peu d'utilisateurs y arrivent → communauté fragmentée

### Solution Proposée
Un sysmodule Atmosphere natif qui :
- S'installe comme ldn_mitm (copier dans `/atmosphere/contents/`)
- Se connecte automatiquement aux serveurs Ryujinx
- Convertit le trafic LDN en protocole réseau sans interception pcap
- Fonctionne de manière transparente pour l'utilisateur

---

## 2. Utilisateurs Cibles

### Persona Principal : Joueur Switch CFW
- Possède une Nintendo Switch avec Atmosphere CFW
- Veut jouer en ligne malgré le ban Nintendo
- N'a pas de compétences techniques avancées
- Attend une expérience "plug and play"

### Besoins Utilisateur
| Besoin | Priorité | Description |
|--------|----------|-------------|
| Installation simple | Critique | Copier des fichiers, redémarrer, jouer |
| Aucune configuration | Critique | Pas de config réseau/serveur manuelle |
| Performance invisible | Haute | Pas d'impact sur les performances de jeu |
| Compatibilité large | Haute | Tous les jeux supportant LDN |

---

## 3. Portée et Fonctionnalités

### Dans le Scope (MVP)
- [x] Sysmodule Atmosphere fonctionnel
- [x] Interception des appels système LDN
- [x] Conversion LDN → protocole réseau Ryujinx
- [x] Connexion automatique aux serveurs Ryujinx
- [x] Support de tous les jeux utilisant LDN
- [x] Consommation ressources minimale

### Hors Scope (v1.0)
- [ ] Compatibilité serveurs lanplay legacy
- [ ] Interface de configuration utilisateur
- [ ] Support émulateur (Ryujinx garde sa version actuelle)
- [ ] Serveur self-hosted

### Fonctionnalités Futures (Post-MVP)
- Configuration avancée optionnelle
- Statistiques de connexion
- Sélection de région/serveur

---

## 4. Exigences Techniques

### Contraintes Hardware
| Contrainte | Spécification |
|------------|---------------|
| Cible | Switch originale (modèle le moins puissant) |
| CPU | ARM Cortex-A57 (4 cœurs) partagé avec le jeu |
| RAM | ~4GB total, sysmodule doit utiliser le minimum |
| Réseau | WiFi 802.11ac |

### Exigences de Performance
- **Latence** : Impact < 5ms sur le temps de réponse réseau
- **Mémoire** : < 2MB d'empreinte mémoire
- **CPU** : < 1% d'utilisation CPU en idle, < 5% en activité
- **Allocations** : Minimiser les malloc, préférer les buffers statiques

### Exigences de Compatibilité
- Atmosphere CFW (dernière version stable)
- Tous les jeux utilisant le service LDN de Nintendo
- Protocole serveur Ryujinx existant (pas de modification serveur)

---

## 5. Exigences de Développement

### Approche TDD
```
1. Écrire les tests AVANT le code
2. Tests du code existant (ryu_ldn) d'abord
3. Couverture de test obligatoire pour tout nouveau code
```

### Documentation Continue
| Langage | Format Documentation |
|---------|---------------------|
| C | Doxygen (`/** ... */`) |
| C++ | Doxygen (`/** ... */` ou `///`) |
| C# | XML comments (`/// <summary>`) |

### Livrables Documentation
- Code documenté selon les standards ci-dessus
- Site Starlight auto-généré
- Déploiement GitHub Pages

### Environnement
- Dev container Docker obligatoire
- Build reproductible

---

## 6. Architecture Haut Niveau

```
┌─────────────────────────────────────────────────────────┐
│                    Nintendo Switch                       │
│  ┌─────────────┐    ┌──────────────────────────────┐   │
│  │    Jeu      │───▶│     ryu_ldn_nx (sysmodule)   │   │
│  │  (LDN API)  │    │  ┌─────────────────────────┐ │   │
│  └─────────────┘    │  │ Interception LDN calls  │ │   │
│                     │  └───────────┬─────────────┘ │   │
│                     │              │               │   │
│                     │  ┌───────────▼─────────────┐ │   │
│                     │  │ Protocol Translation    │ │   │
│                     │  │ (LDN → Ryujinx proto)   │ │   │
│                     │  └───────────┬─────────────┘ │   │
│                     │              │               │   │
│                     │  ┌───────────▼─────────────┐ │   │
│                     │  │ Network Client          │ │   │
│                     │  │ (TCP/UDP to server)     │ │   │
│                     │  └───────────┬─────────────┘ │   │
│                     └──────────────┼──────────────┘   │
└────────────────────────────────────┼──────────────────┘
                                     │ Internet
                          ┌──────────▼──────────┐
                          │  Serveur Ryujinx    │
                          │  (ldn server)       │
                          └─────────────────────┘
```

---

## 7. Risques et Mitigations

| Risque | Impact | Probabilité | Mitigation |
|--------|--------|-------------|------------|
| Performance insuffisante sur Switch | Élevé | Moyenne | Profiling continu, optimisation agressive |
| Incompatibilité certains jeux | Moyen | Moyenne | Tests extensifs, communauté beta |
| Protocole serveur change | Moyen | Faible | Versioning protocole, communication avec équipe Ryujinx |
| Complexité du port | Élevé | Haute | Analyse approfondie codebase avant dev |

---

## 8. Critères de Succès

### MVP Ready When
- [ ] Installation en < 5 minutes sans documentation
- [ ] Mario Kart 8 Deluxe fonctionne en ligne
- [ ] Pas de crash pendant 1h de jeu continu
- [ ] Latence imperceptible par rapport à ldn_mitm+lanplay

### Métriques de Succès
- Taux d'installation réussie > 95%
- Temps de connexion au serveur < 3 secondes
- Aucune régression de performance de jeu mesurable

---

## 9. Sources de Référence

| Source | Usage |
|--------|-------|
| [NintendoClients Wiki](https://github.com/kinnay/NintendoClients/wiki) | Protocoles Nintendo LDN/LAN |
| [Switchbrew](https://switchbrew.org/wiki/Main_Page) | APIs système, développement sysmodule |
| [Switchbrew Hardware](https://switchbrew.org/wiki/Hardware) | Contraintes hardware |
| [ldn_mitm source](https://github.com/spacemeowx2/ldn_mitm) | Implémentation de référence Switch |
| [Ryujinx source](https://git.ryujinx.app/ryubing/ryujinx) | Code ryu_ldn à porter |
| [ldn server](https://git.ryujinx.app/ryubing/ldn) | Protocole serveur |

---

## 10. Approbations

| Rôle | Nom | Date | Statut |
|------|-----|------|--------|
| Product Owner | Gabriel | 2026-01-12 | En attente |

---

*Document généré via workflow BMM `/create-product-brief`*
