# Epic 0 : Infrastructure Projet

**Priorité** : P0 (Bloquant)
**Estimation** : 1 sprint
**Dépendances** : Aucune

## Description

Mettre en place l'infrastructure de développement et la structure open source du projet avant de commencer le développement.

## Valeur utilisateur

> En tant que **développeur contributeur**, je veux un environnement de dev reproductible et une structure de projet claire, pour pouvoir contribuer efficacement.

## Objectifs

- [ ] Environnement de développement Docker fonctionnel
- [ ] Structure projet complète selon architecture.md
- [ ] Fichiers open source (LICENSE, CONTRIBUTING, etc.)
- [ ] CI/CD GitHub Actions de base
- [ ] Build qui compile (même si vide)

---

## Stories

### Story 0.1 : Dev Container Docker

**Priorité** : P0
**Estimation** : 3 points
**Statut** : `completed`

#### Description
Créer un dev container Docker avec devkitPro et toutes les dépendances pour compiler des sysmodules Switch.

#### Critères d'acceptation
- [x] Dockerfile avec devkitPro ARM toolchain
- [x] libnx et Atmosphere-libs installés
- [x] devcontainer.json pour VSCode
- [x] `make` compile sans erreur (projet vide)
- [x] Documentation dans README

#### Tâches techniques
1. Créer `.devcontainer/Dockerfile` basé sur devkitpro/devkita64
2. Installer libnx, libstratosphere
3. Créer `.devcontainer/devcontainer.json`
4. Tester compilation projet vide
5. Documenter setup dans README

#### Tests
- [ ] `docker build` réussit
- [ ] `make` dans container réussit
- [ ] VSCode Remote Containers fonctionne

---

### Story 0.2 : Structure projet sysmodule

**Priorité** : P0
**Estimation** : 2 points
**Statut** : `completed`
**Dépendances** : Story 0.1

#### Description
Créer la structure de base du sysmodule basée sur ldn_mitm, avec Makefile fonctionnel.

#### Critères d'acceptation
- [x] Structure `sysmodule/source/` créée
- [x] `main.cpp` minimal qui compile
- [x] `app.json` avec title_id configuré
- [x] Makefile qui produit un .nso

#### Tâches techniques
1. Copier structure de base depuis ldn_mitm
2. Adapter Makefile pour ryu_ldn_nx
3. Créer `main.cpp` minimal (juste entry point)
4. Configurer `app.json` (title_id: 0x4200000000000010)
5. Vérifier compilation produit `ryu_ldn_nx.nso`

#### Tests
- [ ] `make` produit `ryu_ldn_nx.nso`
- [ ] Taille NSO < 100 KB (projet vide)

---

### Story 0.3 : Fichiers open source

**Priorité** : P1
**Estimation** : 2 points
**Statut** : `completed`

#### Description
Créer tous les fichiers standard pour un projet open source GPLv3 avec DCO.

#### Critères d'acceptation
- [x] LICENSE (GPLv3 complet)
- [x] README.md avec badges et sections
- [x] CONTRIBUTING.md avec DCO instructions
- [x] CODE_OF_CONDUCT.md (Contributor Covenant 2.1)
- [x] SECURITY.md
- [x] CHANGELOG.md

#### Tâches techniques
1. Copier texte GPLv3 dans LICENSE
2. Créer README.md avec structure définie
3. Créer CONTRIBUTING.md avec process PR et DCO
4. Ajouter Contributor Covenant
5. Créer SECURITY.md
6. Initialiser CHANGELOG.md

#### Tests
- [x] Tous les fichiers présents à la racine
- [x] Liens dans README fonctionnent
- [x] DCO clairement expliqué dans CONTRIBUTING

---

### Story 0.4 : Templates GitHub

**Priorité** : P2
**Estimation** : 1 point
**Statut** : `completed`

#### Description
Créer les templates GitHub pour issues et PRs.

#### Critères d'acceptation
- [x] `.github/ISSUE_TEMPLATE/bug_report.md`
- [x] `.github/ISSUE_TEMPLATE/feature_request.md`
- [x] `.github/ISSUE_TEMPLATE/config.yml`
- [x] `.github/PULL_REQUEST_TEMPLATE.md`
- [x] `.github/CODEOWNERS`

#### Tâches techniques
1. Créer template bug report
2. Créer template feature request
3. Configurer issue template chooser
4. Créer PR template avec checklist DCO
5. Définir CODEOWNERS

#### Tests
- [x] Créer issue utilise le template
- [x] Créer PR affiche le template

---

### Story 0.5 : CI/CD Build de base

**Priorité** : P1
**Estimation** : 2 points
**Statut** : `completed`
**Dépendances** : Story 0.1, Story 0.2

#### Description
Mettre en place GitHub Actions pour build automatique sur push/PR.

#### Critères d'acceptation
- [x] `.github/workflows/build.yml` fonctionnel
- [x] Build sur push et PR
- [x] Artifact .nso disponible au téléchargement
- [x] Badge build status dans README

#### Tâches techniques
1. Créer workflow build.yml
2. Utiliser container devkitpro
3. Configurer cache pour accélérer builds
4. Upload artifact .nso
5. Ajouter badge dans README

#### Tests
- [x] Push déclenche build
- [x] PR déclenche build
- [x] Artifact téléchargeable

---

### Story 0.6 : Configuration exemple

**Priorité** : P2
**Estimation** : 1 point
**Statut** : `completed`

#### Description
Créer le fichier de configuration exemple pour les utilisateurs.

#### Critères d'acceptation
- [x] `config/ryu_ldn_nx/config.ini.example` créé
- [x] Toutes les options documentées avec commentaires
- [x] Valeurs par défaut raisonnables

#### Tâches techniques
1. Créer structure `config/ryu_ldn_nx/`
2. Créer `config.ini.example` selon architecture
3. Ajouter commentaires explicatifs
4. Documenter dans README

#### Tests
- [x] Fichier parseable comme INI valide
- [x] Commentaires clairs

---

## Definition of Done (Epic)

- [x] Dev container fonctionnel
- [x] `make` compile le sysmodule (vide mais valide)
- [x] Tous les fichiers open source présents
- [x] CI/CD build fonctionnel
- [x] README complet avec instructions

## Notes

Cet epic doit être complété EN PREMIER avant tout développement de fonctionnalités. Il établit les fondations du projet.
