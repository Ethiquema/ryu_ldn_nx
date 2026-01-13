# Projet :
Update ryu_ldn pour fonctioner nativement sur switch

## c'est quoi ?
ryu_ldn est une adaptation du projet ldn_mitm de la switch
avec une incorporation des protocole natif de cette documentations : https://github.com/kinnay/NintendoClients/wiki
pour ne plus avoir besoin de pcap en capture de packet

a l'origine ldn_mitm est un sys module pour atmosphere (CFW nintendo switch)
qui a pour but de convertire les packet de multi joueur local en packet lan

### mais pourquoi ?
sur la nintendo switch il est impossible de jouer en ligne avec une console modifier
donc il a été créer lan play qui grace a une capture de packet créer un réseaux
virtuel recemblant a un vpn adpté au jeux switch
pour accédé au mode lan sur serveur en ligne.
c'est pour aller de pair que ldn_mitm a été créer

### et ryu_ldn dans tout ça ?
et bien il vise a rendre natif pour l'émulateur ryujinx un équivalent de 
lan play officiel sur leur propre serveur et ce sans config utilisateur
ni outils pcap d'intèrcéption de packet

parcontre il est important de noter que le système lan de ryu_ldn n'a rien a voir
avec un serveur lanplay car il implémenter a partire de la codebase de ldn_mitm
sont propre protocole totalement repensé qui capable de créer équivalent lanplay + ldn_mitm
sans pcap ver ses propre serveur

## Pourquoi ?
aujourd'hui il est beaucoup trop compliquer de jouer en lanplay sur switch a cause
des configuration réseaux et du pc nésésaire en partage de connexion avec pcap d'activé.
ce problème fait que prèsque personne n'utilise cette solutions.

## Objectif :
j'aimerais donc intégré ryu_ldn avec la même simplicité que ldn_mitm
en sys module sur atmosphere pour la nintendo switch

## défis technique :
contrairemement a ldn_mitm qui est fionctionel sur switch ryu_ldn lui
a été concu pour un émulateur et donc avec des capacité
bien superieur en therme d'ouverture système et de puissance brute.

ça veut dire qu'il faut optimiser sont code au maximum pour qu'il tourne
sur switch avec une consomation de resource casi null. sachant que la console a beaucoup
de male d'aurigine a tourner ses propre jeux. les performance doive être impécable

ça veut dire également que partout ou on peut supprimer des maloc ou quelquonque instruction
couteuse pour trouvé une alternative mieux optimiser il faut le faire
et ce sans changer le comportement du code d'origine pour ne pas cassé la communiquation avec les serveur
ni les apelle systèmes.

ça veut dire aussi qu'il faut minusieusement éplucher les documentations pour s'asurer
de la compatibilité du code avec le système de la switch et réadapté
ce qui n'est pas optimiser ou pas fonctionel.

Je t'ai donner la page des limitation hardware des différent model de switch.
tu doit obligatoirement de baser sur la limite basse de model le moins puissant

## Politique de code :
je veut obligatoirement des test unitaire de tout ce que tu ajouté
et tu doit les écrire avant le code
je veut également avant de commancer le projet que tu ajouté les test de tout ce qui est
déja implémanter actuèlement dans le code de ryu_ldn que nous allons reprendre

je te donne également une règle de documentations continue
car ce projet cera open source et repris pas une autre personne que mois
je veut donc que tout soit documenter au norme.
si il y a un équivalent docstring dans le langage utiliser documente ce qui ne l'est pas
dans le code actuèle avant de commencer a dev et documente ton dev au fure et a mesure

je veut également que tu intègre un starlight au projet qui récupère automatiquement
la documentation standard du code avec une github et en github page

## Sources :
pour toute les source don tu a besoin :
- la documentations complète du système réseaux lan, multi local et internet de la switch https://github.com/kinnay/NintendoClients/wiki
- ici tu trouvera la documentations intégrale de switchbrew. le framework de développement sur cfw nintendo switch https://switchbrew.org/wiki/Main_Page
- ici tu a la source de ruyldn également déja cloner dans le dossier courant https://git.ryujinx.app/ryubing/ryujinx
- ici tu a la source du serveur lanplay de ryuldn également précloner https://git.ryujinx.app/ryubing/ldn
- et ici la source de ldn_mitm d'aurigine fonctionel sur switch toujour cloner dans le dossier courant https://github.com/spacemeowx2/ldn_mitm
- Les limite hardware de la switch https://switchbrew.org/wiki/Hardware

## Environement de développement
Je veut travailler sur ce projet en totalité dans un container docker donc je t'invite a créer
un dev container adapté au projet.
