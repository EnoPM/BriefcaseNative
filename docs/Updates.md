# Mises à jour du serveur

Le lanceur local vérifie la dernière release stable GitHub avant de démarrer le Shipping,
depuis `Binaries/Win64` sur Windows ou `Binaries/Linux` sur Linux. Le redémarrage demandé dans l'administration emprunte le même
lanceur. Aucun téléchargement ni remplacement ne se produit pendant une partie.
Un lancement direct de l'exécutable du jeu sans ce lanceur ne vérifie pas les mises à jour.

## Activation automatique

À partir de la version 0.5.1, une installation depuis le ZIP officiel ne demande aucune
configuration manuelle : au premier lancement, `Briefcase.ServerLauncher.exe` (Windows)
ou `Briefcase.ServerLauncher` (Linux) crée `Briefcase/updater.json` s'il est absent,
avec `enabled: true`, `repository: "EnoPM/BriefcaseNative"` et `timeoutSeconds: 20`.
Il vérifie immédiatement les mises à jour, puis à chaque démarrage ou redémarrage via le lanceur.
Une mise à jour compatible est installée avant de lancer le serveur.

Un fichier existant est conservé tel quel, y compris `enabled: false`, un dépôt personnalisé
ou une configuration invalide (signalée comme erreur, jamais remplacée silencieusement).
`enabled` permet de désactiver la vérification ; un dépôt vide la désactive également.
`Briefcase/Updater/updater.example.json` reste fourni comme référence. La configuration
active appartient à l'utilisateur et n'est jamais incluse parmi les fichiers remplacés par une mise à jour.
Les installations 0.5.0 sans configuration doivent installer le nouveau paquet une fois,
ou copier l'exemple vers `Briefcase/updater.json` pour activer leur updater existant.
Cette version utilise les releases **publiques**, sans jeton GitHub ni mot de passe supplémentaire.
Ne pas placer de jeton dans le nom du dépôt. Aucun dépôt n'est créé automatiquement.

`timeoutSeconds` (20 par défaut, 1 à 120) borne séparément la lecture des métadonnées et
le téléchargement de l'archive. Une panne réseau, une limite GitHub, un dépôt privé ou
une release absente laisse démarrer la version installée avec un avertissement.

## Préparer une release

1. Changer uniquement le fichier `VERSION` à la racine (par exemple `0.5.1`).
2. Exécuter `scripts/build/Build.ps1`, puis `scripts/client/Package-Client.ps1`.
   Ce dernier teste et vérifie les deux paquets, puis prépare l'archive serveur dans `dist/Releases`.
3. Après validation et approbation, créer une release stable portant le tag `vMAJOR.MINOR.PATCH`.
4. Y joindre `BriefcaseNative-Server-windows-x64-MAJOR.MINOR.PATCH.zip` et son fichier `.sha256`.
   Les archives de sources générées automatiquement par GitHub ne sont pas des paquets installables.

L'updater lit `GET /repos/{owner}/{repo}/releases/latest`. Il refuse les préversions,
les retours automatiques à une version antérieure, les assets ambigus, les redirections hors GitHub,
les archives sans digest SHA256 fourni par GitHub et les builds de jeu incompatibles.
Le hash de jeu pris en charge est actuellement celui du serveur déjà validé (dans le packaging).
Un futur changement de build doit être validé avec les contrats natifs avant de changer ce hash.

## Installation et récupération

L'archive est téléchargée puis extraite dans `Briefcase/Updates/<id>/stage` avec des limites
de taille et un contrôle des chemins. `Package.json` décrit exactement les fichiers et leurs hashes.
Seuls les fichiers du framework sont gérés ; aucun mod ne figure dans le nouveau paquet. Les données `Mods/*/Data`,
la sélection des mods, la configuration d'administration, les mots de passe, les logs, le fichier
de configuration de l'updater et les réglages du jeu restent en place. Les mods recensés par les anciens paquets monolithiques restent installés. Les mods personnels absents de l'ancien inventaire restent en place.
Les anciens fichiers gérés absents du nouveau paquet sont retirés, après sauvegarde.

Un verrou empêche deux lanceurs de modifier simultanément l'installation. Le journal de transaction
est écrit avant toute mutation. Les anciens fichiers sont sauvegardés sous `Briefcase/Updates/<id>/backup`.
Une erreur entraîne leur restauration. Après interruption au milieu d'une installation, le prochain
lancement restaure d'abord les sauvegardes. Si cette restauration échoue, le serveur ne démarre pas
avec un mélange de DLL. Les sauvegardes sont conservées : leur nettoyage reste manuel pour cette version.
Une installation techniquement réussie n'est pas un test du comportement en jeu : le retour automatique
sur crash de gameplay ne fait pas partie de cette version.

`Briefcase/Updates/last-result.json` et les traces `Briefcase/Logs/launch-*.json` donnent le résultat.
Le digest protège l'intégrité de l'archive téléchargée ; il ne remplace pas une signature indépendante
si le compte GitHub qui publie les releases est compromis. Sécuriser les accès de publication du futur dépôt.

## Portabilité et périmètre

Les paquets **serveur Windows x64 et Linux x64** utilisent le même protocole
(HTTPS, JSON, ZIP, SHA256), avec un asset propre à chaque plateforme. Le lanceur Linux
et son updater sont natifs C++ ; le coordinateur Windows utilise PowerShell.
Le client n'installe pas automatiquement ses mises à jour. Voir [LinuxServer.md](LinuxServer.md)
pour les dépendances et les limites de validation du serveur Linux.

Référence : [API officielle GitHub Releases](https://docs.github.com/en/rest/releases/releases).

## Publication automatique et manuelle

Le fichier `.github/workflows/release.yml` fournit **Publish server release**.
Un push sur `main` qui modifie `VERSION` déclenche automatiquement la compilation,
les tests et la publication de `v<VERSION>`. Un push sans changement de ce fichier
ne publie rien. Le fichier accepte uniquement `MAJOR.MINOR.PATCH`, sans préfixe `v`.
La version est lue dans le commit déclencheur, jamais dans une autre révision de `main`.
CMake, le SDK et les noms d'archives utilisent tous cette même source.
L'inventaire des sources valide le format de `VERSION` sans lui imposer une empreinte
fixe ; changer uniquement ce fichier suffit pour une nouvelle release.

Pour déclencher manuellement :

1. Mettre à jour `VERSION` et enregistrer les sources dans Git.
2. Ouvrir **Actions → Publish server release → Run workflow**.
3. Choisir la branche contenant le code à publier.
4. La version est lue automatiquement dans `VERSION` ; aucune saisie supplémentaire.
5. Laisser **Keep the release as a draft** décoché pour publier automatiquement, ou le cocher
   pour préparer un brouillon à relire avant publication.
6. Cliquer sur **Run workflow**.

GitHub doit connaître le workflow sur la branche par défaut pour proposer ce déclenchement manuel.
Le commit sélectionné au lancement est fixé pour les deux jobs, même si la branche évolue ensuite.
Le workflow ne modifie pas la version du code à votre place.

Le runner Windows 2025 récupère UE4SS et les dépendances épinglées, installe Rust 1.97.1,
compile tous les projets en Release x64, exécute les tests puis vérifie les paquets client/serveur.
Les tests graphiques utilisent WARP : aucune copie du jeu n'est nécessaire. Le ZIP **serveur**, le ZIP **SDK** et leurs .zip.sha256 sont transférés au job de publication et joints à la release.
Le client est compilé et testé mais n'est pas publié.

Le job de compilation dispose d'un accès en lecture au dépôt. Le job de publication utilise
le GITHUB_TOKEN fourni automatiquement par GitHub, avec contents: write. Aucun secret
personnel n'est utilisé pour publier. La compilation utilise le secret `UPSTREAM_READ_TOKEN`
pour lire le sous-module Unreal privé. Aucun mot de passe serveur, chemin de copie locale
ou fichier local.settings.json n'est requis.
Les politiques du dépôt/de l'organisation doivent autoriser GitHub Actions et la création des releases/tags.
Les actions utilisées sont officielles et épinglées par leur SHA complet.

Le tag vMAJOR.MINOR.PATCH cible le commit compilé. Un tag existant associé à un autre commit
est refusé ; une release existante n'est pas remplacée. Les uploads sont assemblés dans un brouillon.
La release reste en brouillon pendant que le workflow réutilisable `linux-server-release.yml`
compile, teste et ajoute les assets Linux du même commit. Les digests GitHub sont comparés
aux fichiers locaux avant publication comme release stable/latest, avec les six assets
(Windows, Linux et SDK, chacun avec son SHA-256). Le mode brouillon manuel conserve la
release en brouillon après validation des deux plateformes.
Les notes de release sont générées par GitHub. Un brouillon ne déclenche pas les mises à jour serveur.

En cas d'échec après création du brouillon, celui-ci est laissé en place pour inspection.
Une nouvelle exécution ne remplace pas son contenu : supprimer manuellement le brouillon incomplet
avant de recommencer, ou reprendre sa gestion dans GitHub. Ne pas remplacer une release déjà distribuée ;
publier une nouvelle version. Les fichiers de release restent accessibles dans l'artifact
server-release du run pendant 14 jours et les rapports CTest d'un échec pendant 7 jours.

Validation locale du workflow :
- actionlint .github/workflows/release.yml vérifie la syntaxe GitHub Actions ;
- après préparation de l'archive, tests/ReleaseWorkflow.Contracts.ps1 teste les contrôles de version/tag,
  le brouillon, la publication et les erreurs d'intégrité avec des commandes GitHub simulées.
  Ce test ne contacte pas GitHub et ne crée aucune release.

Références : [déclenchement manuel](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow),
[création des releases avec GitHub CLI](https://cli.github.com/manual/gh_release_create).

Voir [Repositories.md](Repositories.md) pour la séparation et le SDK.
