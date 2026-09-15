# Développement et indexation CMake

Ouvrir le dossier racine BriefcaseNative dans CLion comme projet CMake.
Le fichier CMakeLists.txt décrit les cibles, leurs sources, leurs dépendances
et les dossiers contenant leurs en-têtes. Son rôle est comparable à celui d'un
fichier .csproj et de ses références de projets en C#.

Le simple fait qu'un fichier soit présent dans le dossier ne lui donne pas
automatiquement un contexte de compilation. Les fichiers .cpp doivent appartenir
à une cible CMake ; les .hpp et .inc sont normalement analysés dans le contexte
des fichiers qui les incluent. Les DLL ne sont pas des fichiers source à indexer.

## CLion

Le profil local Debug utilise cmake-build-debug. Les scripts officiels utilisent
build en Release. Ce sont deux caches CMake distincts : compiler avec les scripts
ne recharge pas automatiquement le modèle déjà ouvert dans CLion.

Après ajout d'une cible ou modification des dépendances :

1. Dans CLion : Tools → CMake → Reload CMake Project
   (ou Ctrl+Shift+A, puis rechercher Reload CMake Project).
2. Attendre la fin de la configuration et de l'indexation.
3. Pour les modifications suivantes, activer le rechargement automatique dans
   Settings → Build, Execution, Deployment → CMake.

Il n'est normalement pas nécessaire d'invalider tous les caches de l'IDE.
Une erreur pendant la configuration doit être corrigée avant de recharger.

CMAKE_EXPORT_COMPILE_COMMANDS est activé : chaque dossier de build contient
compile_commands.json, qui expose les commandes exactes, définitions et chemins
d'inclusion utilisés pour chaque source. Ne pas modifier ce fichier généré.

## Mbed TLS et CMake

Mbed TLS 3.6.7 déclare encore un niveau de politiques CMake 3.5.1.
Avec CMake 4, cmake/AdminCrypto.cmake utilise CMAKE_POLICY_VERSION_MINIMUM=3.10
uniquement dans la portée qui ajoute cette dépendance. Le code tiers et les
avertissements globaux restent inchangés. Le minimum CMake du projet reste 3.28.

Références :
- https://www.jetbrains.com/help/clion/reloading-project.html
- https://cmake.org/cmake/help/latest/variable/CMAKE_POLICY_VERSION_MINIMUM.html

