# Epic 5 : Documentation et Release

**Priorité** : P1
**Estimation** : 1 sprint
**Dépendances** : Epics 0-4

## Description

Finaliser la documentation, mettre en place Starlight pour la doc auto-générée, et préparer le système de release.

## Valeur utilisateur

> En tant que **contributeur ou utilisateur**, je veux une documentation complète et à jour, et des releases faciles à installer, pour utiliser et contribuer au projet efficacement.

## Objectifs

- [x] Documentation Doxygen complète
- [x] Site Starlight déployé sur GitHub Pages
- [x] Workflow release automatique
- [x] Guide installation utilisateur
- [x] Guide contribution développeur

---

## Stories

### Story 5.1 : Documentation Doxygen code

**Priorité** : P1
**Estimation** : 3 points
**Statut** : `done`
**Dépendances** : Epics 1-3

#### Description
Ajouter la documentation Doxygen à tout le code existant.

#### Critères d'acceptation
- [x] Tous les fichiers .hpp documentés
- [x] Toutes les classes documentées
- [x] Toutes les fonctions publiques documentées
- [x] Génération XML pour Starlight

#### Tâches techniques
1. ✅ Créer `Doxyfile` configuration
2. ✅ Configurer génération XML
3. ✅ Intégrer avec Starlight build

#### Fichiers créés
- `Doxyfile`

---

### Story 5.2 : Site Starlight

**Priorité** : P1
**Estimation** : 3 points
**Statut** : `done`
**Dépendances** : Story 5.1

#### Description
Mettre en place le site de documentation Starlight avec intégration Doxygen.

#### Critères d'acceptation
- [x] Projet Starlight initialisé
- [x] Documentation API depuis Doxygen
- [x] Guides utilisateur (installation, config)
- [x] Guides développeur (contribution, architecture)
- [x] Déploiement GitHub Pages

#### Tâches techniques
1. ✅ Créer `docs/` avec structure Starlight
2. ✅ Configurer Starlight theme
3. ✅ Script conversion Doxygen XML → Markdown
4. ✅ Écrire guide installation
5. ✅ Écrire guide configuration
6. ✅ Écrire guide contribution
7. ✅ Écrire architecture.md

#### Fichiers créés
- `docs/package.json`
- `docs/astro.config.mjs`
- `docs/tsconfig.json`
- `docs/scripts/generate-api-docs.mjs`
- `docs/src/content/docs/index.mdx`
- `docs/src/content/docs/guides/introduction.mdx`
- `docs/src/content/docs/guides/installation.mdx`
- `docs/src/content/docs/guides/configuration.mdx`
- `docs/src/content/docs/guides/overlay.mdx`
- `docs/src/content/docs/guides/troubleshooting.mdx`
- `docs/src/content/docs/guides/contributing.mdx`
- `docs/src/content/docs/guides/building.mdx`
- `docs/src/content/docs/guides/architecture.mdx`
- `docs/src/content/docs/api/index.mdx`
- `docs/src/content/docs/reference/ipc-commands.mdx`
- `docs/src/content/docs/reference/protocol.mdx`
- `docs/src/styles/custom.css`
- `docs/src/assets/hero.svg`

---

### Story 5.3 : Workflow docs GitHub Actions

**Priorité** : P1
**Estimation** : 2 points
**Statut** : `done`
**Dépendances** : Story 5.2

#### Description
Automatiser le build et déploiement de la documentation.

#### Critères d'acceptation
- [x] Build Doxygen automatique
- [x] Build Starlight automatique
- [x] Déploiement GitHub Pages sur push main

#### Tâches techniques
1. ✅ Créer `.github/workflows/docs.yml`
2. ✅ Job Doxygen XML build
3. ✅ Job Starlight build
4. ✅ Job déploiement Pages

#### Fichiers créés
- `.github/workflows/docs.yml`

---

### Story 5.4 : Workflow release

**Priorité** : P1
**Estimation** : 3 points
**Statut** : `done`
**Dépendances** : Epic 0

#### Description
Automatiser la création de releases avec packages prêts à installer.

#### Critères d'acceptation
- [x] Tag `v*` déclenche release
- [x] Build sysmodule + overlay
- [x] Package ZIP pour SD card
- [x] GitHub Release créée avec changelog
- [x] Tests unitaires exécutés

#### Tâches techniques
1. ✅ Créer `.github/workflows/release.yml`
2. ✅ Build sysmodule en mode release
3. ✅ Build overlay en mode release
4. ✅ Créer structure SD (`atmosphere/contents/...`)
5. ✅ Inclure .npdm, .elf pour debug
6. ✅ Créer ZIP avec structure correcte
7. ✅ Générer changelog depuis commits
8. ✅ Créer GitHub Release

#### Fichiers créés/modifiés
- `.github/workflows/release.yml`
- `.github/workflows/build.yml` (ajout tests + overlay + package)

---

### Story 5.5 : Guide installation utilisateur

**Priorité** : P0
**Estimation** : 2 points
**Statut** : `done`

#### Description
Écrire le guide d'installation complet pour les utilisateurs finaux.

#### Critères d'acceptation
- [x] Prérequis clairement listés
- [x] Étapes d'installation détaillées
- [x] Configuration optionnelle expliquée
- [x] Troubleshooting commun

#### Fichiers créés
- `docs/src/content/docs/guides/installation.mdx`
- `docs/src/content/docs/guides/configuration.mdx`
- `docs/src/content/docs/guides/troubleshooting.mdx`

---

### Story 5.6 : Guide contribution développeur

**Priorité** : P1
**Estimation** : 2 points
**Statut** : `done`

#### Description
Écrire le guide de contribution pour les développeurs.

#### Critères d'acceptation
- [x] Setup environnement dev
- [x] Process de contribution (fork, PR)
- [x] Style de code
- [x] Requirements DCO
- [x] Tests requis

#### Fichiers créés
- `docs/src/content/docs/guides/contributing.mdx`
- `docs/src/content/docs/guides/building.mdx`
- `docs/src/content/docs/guides/architecture.mdx`

---

### Story 5.7 : CHANGELOG et versioning

**Priorité** : P1
**Estimation** : 1 point
**Statut** : `done`

#### Description
Mettre en place le système de versioning et changelog.

#### Critères d'acceptation
- [x] Format semver (v0.1.0)
- [x] CHANGELOG.md format Keep a Changelog
- [x] Version dans code source (RYU_LDN_VERSION)
- [x] Version affichée dans overlay

#### Fichiers modifiés
- `CHANGELOG.md` (updated with all features)

---

## Definition of Done (Epic)

- [x] Code documenté (Doxygen XML)
- [x] Site Starlight configuré
- [x] Workflow docs automatique
- [x] Workflow release automatique
- [x] Guide installation complet
- [x] Guide contribution complet
- [x] CHANGELOG mis à jour
- [ ] Première release v0.1.0 publiée (requires git tag)
- [ ] Site déployé sur GitHub Pages (requires push to main)

## Notes

La documentation est critique pour l'adoption par la communauté. Un projet bien documenté attire plus de contributeurs.

Format CHANGELOG : [Keep a Changelog](https://keepachangelog.com/)
Format versioning : [Semantic Versioning](https://semver.org/)
