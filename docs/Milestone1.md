# Milestone 1 — prototype natif

## Résultat

Le prototype est construit en Release x64 et déployé dans le Win64 de l'unique
copie de développement autorisée. Le lanceur local est
StartBriefcaseNativeServer.ps1, à côté de DeceiveIncServer-Win64-Shipping.exe.
Le répertoire de travail transmis à Start-Process et celui constaté par le runtime
correspondent tous deux à ce Win64.

Le paquet contient trois DLL : version.dll, Briefcase.NativeHost.dll et
Briefcase.NativeHello.dll. Les autres fichiers déployés sont le lanceur, le
manifeste du sample et les notices/licences. La configuration locale du lanceur
est générée au déploiement. Aucun source C++, en-tête, PDB ou fichier du serveur
n'est intégré au dépôt. Les 163 fichiers du paquet ont été comparés par SHA-256
avec leur copie déployée : aucune différence.

Le dépôt Git est initialisé. Les fichiers du projet sont non commités ; aucun
commit, push ou publication n'a été effectué.

## Build identifié

- Exécutable : DeceiveIncServer-Win64-Shipping.exe, 92 886 528 octets.
- SHA-256 : 78afe1dbeecb09027c274def4f0ac855b447dc52ffe3cd9482c1be4341b0dae6.
- Timestamp PE : 0x6A966107 ; SizeOfImage : 0x05B60000.
- Unreal observé dans le journal du jeu : 4.27.2-72378+++deceive+dev+main.
- Version serveur annoncée : 1.12.02.72378.
- Profil activé dans cette milestone : serveur seulement. L'ABI et les paquets
  sont communs ; les profils clients restent à valider séparément.

Le runtime refuse un autre nom d'exécutable, une autre identité PE, un autre
SHA-256 ou un répertoire de travail différent du dossier de l'exécutable.

## Vérifications fonctionnelles

- 36 vérifications natives : versions, manifestes, dépendances, cycles,
  capacités, JSON ambigu, isolation des handles, destruction simulée,
  réutilisation d'adresse et libération.
- 15 vérifications du lanceur PowerShell : véritable capture des arguments de
  Start-Process, validation sans lancement, refus des mauvais dossiers et
  du double démarrage. Les fixtures n'exécutent aucun serveur.
- ABI compilée depuis une unité C indépendante, avec assertions des tailles
  de BcResult, BcHandle, BcApi et BcBuild et de l'offset du contexte.
- Les deux tests CTest passent ; les contrôles PowerShell passent.
- 17 exports du proxy vérifiés. Le runtime n'importe ni Lua, ni GLFW, ni D3D.
  Les cibles GUI, console et chargeurs de mods UE4SS ne sont pas compilées.
- NativeHello vérifie l'identité du framework/build, le rejet d'une structure
  de sortie trop petite, le refus d'une recherche depuis le worker, la recherche
  de /Script/DeceiveInc.Spy sur le game thread et l'invalidation d'un handle libéré.
- Le jeu charge Silverreef et annonce ServerStatus=Lobby.

## Initialisation mesurée

Mesures du dernier lancement, depuis l'entrée du runtime :

| Étape | Temps |
|---|---:|
| Identité du jeu et bootstrap armé | 77 ms |
| Début de l'initialisation sur le game thread | 6 553 ms |
| Durée propre à l'initialisation UE4SS | 158 ms |
| Backend prêt | 6 711 ms |
| NativeHello chargé, identité vérifiée | 6 811 ms |
| Callback Unreal et validation du handle | 6 812 ms |

Le délai initial de cinq secondes est volontaire pour ce prototype. Le point
d'amorçage est l'import Sleep du seul exécutable autorisé. Les temps de chargement
de map proviennent du journal du jeu et ne mesurent pas un surcoût du framework.

Les mesures de stabilité sont écrites par scripts/test/Measure-Server.ps1 dans
artifacts/stability-*.json. Les relevés portent sur le processus entier, pas
uniquement sur Briefcase. Une courte observation ne démontre pas l'absence de
fuite lors d'une session de plusieurs heures.

## Erreurs rencontrées et corrections

- CMake du PATH trop ancien pour Visual Studio installé : utilisation du CMake
  et de Ninja fournis par l'installation Visual Studio découverte par vswhere.
- Includes exportés par PolyHook incompatibles avec CMake récent : expressions
  BUILD_INTERFACE ajoutées dans l'intégration CMake.
- Trois incompatibilités des sources UE4SS avec les outils actuels : alias byte
  implicite, appel template find et enum non déclaré. Corrections ciblées et
  reproductibles dans Patch-UE4SS.ps1.
- Collision de macros AsmJit/Unreal : isolation des includes du backend.
- Premier bootstrap par detour de Sleep refusé : remplacé par une modification
  atomique d'un seul emplacement de la table d'import du serveur. Les DLL système
  ne sont pas patchées.
- Un lancement intermédiaire a disparu avant le relevé suivant, sans code de
  sortie capturé ni rapport de crash retrouvé. La cause n'est pas établie.
  Un relancement identique est resté actif jusqu'à son arrêt explicite, puis
  les observations suivantes n'ont pas reproduit cet arrêt. Un test prolongé
  reste nécessaire avant une distribution.

## Procédure de test manuelle

Le serveur final reste actif. Vérifier d'abord son lobby avec le client habituel,
sans modifier d'autre installation.

1. Lire Win64\Briefcase\Logs\BriefcaseNative.log : backend prêt, sample chargé,
   identité vérifiée, recherche Spy et handle libéré rejeté.
2. Vérifier Silverreef et Lobby dans DeceiveInc\Saved\Logs\DeceiveInc.log.
3. Laisser tourner plusieurs heures et utiliser
   ./scripts/test/Measure-Server.ps1 -DurationSeconds 3600 -SampleSeconds 60.
4. Pour reconstruire et redéployer depuis le projet :
   ./scripts/deploy/Stop-Server.ps1
   ./scripts/build/Build.ps1
   ./scripts/test/Test.ps1
   ./scripts/deploy/Deploy-Server.ps1 -PlanOnly
   ./scripts/deploy/Deploy-Server.ps1
   ./scripts/deploy/Start-Server.ps1 -ValidateOnly
   ./scripts/deploy/Start-Server.ps1
5. Le lanceur local Win64\StartBriefcaseNativeServer.ps1 fonctionne aussi lorsqu'il
   est appelé depuis un autre dossier.

La connexion d'un client n'a pas été réalisée automatiquement. Le jeu signale
un contrôle Windows Firewall avec erreur 0x80070005 ; aucune règle réseau n'a été
modifiée. Il faut donc valider la connectivité réelle lors du test manuel.

Le SDK complet, le portage des anciens mods, les profils clients, le hot reload
et les garanties de stabilité à long terme restent hors de cette milestone.

## Observation finale

Processus laissé actif : PID 23720. Observation de 180.01 s,
13 relevés après le démarrage. CPU moyen du processus :
15.03 % d'un cœur logique. Mémoire privée :
795.84 → 796.62 Mio ; handles Windows :
667 → 657 ; threads :
77 → 74. Aucun arrêt observé pendant cette mesure.
Le handle de fenêtre principale reste nul à tous les relevés.

Ces chiffres incluent le serveur, ses bots et ses services réseau. Ils ne permettent
pas d'isoler le surcoût de Briefcase ni de conclure à l'absence de fuite à long terme.
La légère variation de mémoire pendant cette fenêtre doit être contrôlée lors du
soak test manuel. Données détaillées : stability-20260913-203141.json, dans artifacts/.
