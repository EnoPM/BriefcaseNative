# Mises à jour du serveur

Le lanceur local vérifie la dernière release stable GitHub avant de démarrer le Shipping,
toujours depuis Win64. Le redémarrage demandé dans l'administration emprunte le même
lanceur. Aucun téléchargement ni remplacement ne se produit pendant une partie.
Un lancement direct de l'EXE sans ce lanceur ne vérifie pas les mises à jour.

## Activer après la création du dépôt

Copier `Briefcase/Updater/updater.example.json` vers `Briefcase/updater.json`, puis renseigner
`repository` sous la forme `proprietaire/depot`. `enabled` permet de désactiver la vérification.
Sans fichier ou avec un dépôt vide, aucune requête réseau n'est envoyée.
Cette version utilise les releases **publiques**, sans jeton GitHub ni mot de passe supplémentaire.
Ne pas placer de jeton dans le nom du dépôt. Aucun dépôt n'est créé automatiquement.

`timeoutSeconds` (20 par défaut, 1 à 120) borne séparément la lecture des métadonnées et
le téléchargement de l'archive. Une panne réseau, une limite GitHub, un dépôt privé ou
une release absente laisse démarrer la version installée avec un avertissement.

## Préparer une release

1. Changer la version `project(BriefcaseNative VERSION ...)` dans CMakeLists.txt.
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

La première intégration concerne le **serveur Windows x64**, publié en premier. Le client n'installe
pas automatiquement ses mises à jour. Le protocole (HTTPS, JSON, ZIP, SHA256) est indépendant de l'OS ;
les scripts de lancement/processus restent Windows. Un serveur Linux devra disposer de son propre
installateur/lanceur et d'un asset Linux, ainsi que du portage du runtime. Aucun support Linux n'est annoncé ici.

Référence : [API officielle GitHub Releases](https://docs.github.com/en/rest/releases/releases).

## Workflow GitHub Actions manuel

Le fichier .github/workflows/release.yml fournit **Publish server release**.
Il n'est déclenché ni par un push ni par la création d'un tag.

Lorsque le dépôt sera créé et ces fichiers présents sur sa branche par défaut :

1. Mettre à jour la version dans CMakeLists.txt et enregistrer les sources dans Git.
2. Ouvrir **Actions → Publish server release → Run workflow**.
3. Choisir la branche contenant le code à publier.
4. Saisir sa version, par exemple 0.3.0 (sans v), exactement comme dans CMake.
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
personnel, mot de passe serveur, chemin de copie locale ou fichier local.settings.json n'est requis.
Les politiques du dépôt/de l'organisation doivent autoriser GitHub Actions et la création des releases/tags.
Les actions utilisées sont officielles et épinglées par leur SHA complet.

Le tag vMAJOR.MINOR.PATCH cible le commit compilé. Un tag existant associé à un autre commit
est refusé ; une release existante n'est pas remplacée. Les uploads sont assemblés dans un brouillon,
puis leurs digests GitHub sont comparés aux fichiers locaux avant publication comme release stable/latest.
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

Le premier build sur un runner GitHub reste à valider lorsque le dépôt existera.

Références : [déclenchement manuel](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow),
[création des releases avec GitHub CLI](https://cli.github.com/manual/gh_release_create).

Voir [Repositories.md](Repositories.md) pour la séparation et le SDK.
