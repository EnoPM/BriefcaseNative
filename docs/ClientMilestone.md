# BriefcaseNative 0.3.0 — première variante cliente

Version Release x64 construite et déployée le 14 septembre 2026. Tests automatisés
réussis ; validation manuelle des contrôles en partie encore à compléter.
Aucun commit, push ou publication. Le serveur actif et les projets de référence
n'ont été ni modifiés ni redémarrés.

## Architecture et fichiers

| Composant | Rôle / nouveaux fichiers principaux |
| --- | --- |
| NativeHost | Socle commun : identité, manifestes, dépendances, capacités, chargement et arrêt. `GameProfile.hpp`, `ClientBuild.json`, `ClientStartup.hpp`, `ClientHost.inc`, `ClientServices.inc` |
| UnrealBackend | Intégration headless UE4SS liée dans NativeHost ; `ObjectDeleteListener.hpp` pour le cycle de vie Unreal |
| Client.Rendering | DLL client uniquement : `ClientBridge.h`, `Rendering.cpp` ; D3D11, contexte unique, ressources, callbacks |
| Client.Input | Bibliothèque interne : `Input.hpp`, `Input.cpp`, `InputPolicy.hpp` |
| Client.Menu | Bibliothèque interne : `Menu.hpp`, `Menu.cpp` ; Home/Mods, thème sombre violet |
| ModApi | ABI C commune existante conservée |
| ClientModApi | `sdk/Briefcase.ClientModApi/include/Briefcase/ClientModApi.h` ; voir `ClientApi.md` |
| NativeOverlaySample | `samples/Briefcase.NativeOverlaySample/Overlay.cpp` et manifeste client |

NativeHost est **le même binaire** dans les deux paquets. Il n'importe ni D3D, ni
DXGI, ni ImGui. Rendering est chargé dynamiquement seulement en mode client ;
Input/Menu/ImGui sont liés dans cette DLL. Le serveur ne charge jamais ces composants.

Dear ImGui **1.91.9b** est épinglé sous `third_party/imgui-1.91.9b`, avec licence MIT
et inventaire SHA256 dans `SOURCE.json`. Aucune copie par mod. Segoe UI est lue
depuis Windows, avec repli sur la police intégrée ; elle n'est pas redistribuée.
Les tiers liés statiquement restent séparés dans les sources, avec leurs licences
dans `Briefcase/Licenses`. Aucune DLL tierce graphique additionnelle à distribuer.

L'ancien `Briefcase.Native.Rendering` a été analysé en lecture seule : petite
swap chain WARP pour découvrir les méthodes DXGI, hooks MinHook, filtrage de la
fenêtre Unreal du processus et restauration de l'état graphique. Cette approche
est reprise, avec création différée et gestion d'entrée séparée.

Autres fichiers ajoutés : `scripts/client/` (build, tests, paquet/audit, déploiement,
lancement projet/local, arrêt, mesures et contrôles de chemins),
`tests/ClientContracts.cpp`, `tests/ClientRenderingFixture.cpp`,
`tests/ClientDeployment.Contracts.ps1`, `tests/ObjectListenerContracts.cpp` et
`docs/ClientApi.md`. CMake, proxy, Host/Manifest/Services/Shutdown, timings du backend,
contrat ABI C, paramètres locaux et README sont adaptés.

## Démarrage et ressources

Le jeu et les essais réels confirment **Direct3D 11 sur RTX 4070 Ti**.
Build Unreal du jeu : 4.27.2-72378 ; backend détecté : famille 4.27.

- EXE : `DeceiveInc-Win64-Shipping.exe`
- PE : `6A96564B` ; image : `06283000`
- SHA256 : `b753b51f4d51adc41f7577e7e01521a62f941711ec68330cf22546f81c788a87`

Le proxy prépare ses exports puis démarre le host sur un worker, sans bloquer
l'entrée EXE cliente. Le serveur conserve son mécanisme avant entrée EXE.
La découverte graphique utilise un worker de priorité réduite.

Aucun contexte, atlas de polices ou ressource ImGui au chargement. La création exige :
F1 ouvert, au moins 30 images/2 secondes de rendu, et le signal de fin des shaders.
Un lecteur borné du journal, sur le worker du host, reconnaît `PrecompileCompleted`
ou l'arrivée à l'écran de connexion. Les traces antérieures au processus sont
ignorées. Le lanceur fournit un journal propre à cette copie ; le lancement
manuel peut utiliser le journal AppData habituel. Sans signal, le menu reste différé.

La création à F1 déclenche ensuite la découverte Unreal sur le game thread.
Les mods client sans capacités Unreal peuvent se charger avant. Le sample attend
le contexte pour dessiner puis reste visible lorsque le menu est fermé.

Present, ResizeBuffers et destruction de swap chain sont traités. Le RTV est
libéré avant resize puis recréé ; une nouvelle swap chain/device reconstruit les
ressources. DEVICE_REMOVED/RESET invalide le rendu et attend la recréation par
le jeu. Tous les render targets et le depth target sont restaurés, en plus de
l'état sauvegardé par ImGui DX11. Après réduction de résolution, le menu reste
dans le viewport et son contenu défile si nécessaire.

## Entrées

F1 ouvre/ferme sans répétition ; un menu ouvert peut se fermer même avec un
modificateur tenu. Alt+Tab conserve son état ouvert.

Menu fermé, les messages vont au jeu et les imports utilisent leur fonction
originale. Une touche tenue lors de la fermeture peut rester neutralisée jusqu'à
son relâchement afin d'éviter un déplacement involontaire. À l'ouverture, les
touches de gameplay précédemment transmises reçoivent leur release.

Menu ouvert, les messages nécessaires sont interceptés. WM_INPUT est nettoyé par
DefWindowProc sans être transmis au gestionnaire de gameplay. L'enregistrement
Raw Input est conservé. Alt+Tab, Alt+F4, Shift+Tab et modificateurs système sont transmis.

Seuls les imports de l'EXE sont modifiés : clavier, ClipCursor, SetCursor,
GetCursorPos et SetCursorPos. Les demandes de clip, de position et de forme du
curseur du jeu sont mémorisées. Le jeu conserve une position virtuelle cohérente
pendant que le menu emploie le curseur physique. L'état demandé est restauré
au retour au gameplay. Aucun compteur ShowCursor artificiel ; aucun détournement
des fonctions des autres applications ou DLL d'overlay.

## Paquets, installation et scripts

```text
dist/Client/
  version.dll
  StartBriefcaseNativeClient.ps1
  Package.json
  Briefcase/
    loader.json
    Core/Briefcase.NativeHost.dll
    Core/Client/Briefcase.Client.Rendering.dll
    Mods/briefcase.native-overlay-sample/
    Licenses/
dist/Server/
  version.dll
  StartBriefcaseNativeServer.ps1
  Package.json
  Briefcase/Runtime/Briefcase.NativeHost.dll
  Briefcase/Mods/...
  Briefcase/Licenses/...
```

Seule destination de déploiement utilisée :

`D:\DeceiveIncBackups\DeceiveIncNativeClient\DeceiveInc\Binaries\Win64`

Les chemins viennent de `local.settings.json`, ignoré par Git, ou des paramètres.
Win64 doit être exactement le sous-dossier autorisé. Traversées, collisions de
préfixe et jonctions sont rejetées avant remplacement. Le client doit être arrêté.
Le déploiement conserve loader.json, ignore les fichiers identiques, sauvegarde
les fichiers remplacés sous `DeceiveIncNativeClient/BriefcaseDeploymentBackups`
et vérifie les empreintes copiées. Aucun nettoyage d'autres mods ou configurations.

Le lanceur installé dans Win64 démarre directement le Shipping avec **Win64
comme WorkingDirectory**, même appelé d'ailleurs. Aucun lancement de la racine.

- `Briefcase/Briefcase.log` : framework.
- `Briefcase/Logs/DeceiveInc-client.log` : jeu avec le lanceur.
- `Briefcase/Logs/client-metrics.json` : instantané demandé ou arrêt.
- `Briefcase/Logs/last-launch.json` : EXE, cwd, PID, arguments.

Les moyennes sont accumulées sans trace par image. Les événements de ressources,
ouvertures/fermetures et instantanés demandés sont journalisés.

```powershell
.\scripts\client\Build-Client.ps1
.\scripts\client\Test-Client.ps1
.\scripts\client\Package-Client.ps1
.\scripts\client\Stop-Client.ps1
.\scripts\client\Deploy-Client.ps1
.\scripts\client\Start-Client.ps1
.\scripts\client\Measure-Client.ps1 -Seconds 10
```

Package-Client exécute les tests avant de produire les deux paquets.
Stop-Client vérifie désormais aussi le code de sortie normal du jeu : un crash
ne doit plus être présenté comme une fermeture réussie.

## Mesures

Session réelle **PID 32548, binaire corrigé**, avec ouverture F1 par l'utilisateur,
puis fermeture normale vérifiée. Données : `artifacts/client-fixed-live-metrics.json`
et `client-fixed-framework.log`.

| Mesure | Valeur |
| --- | ---: |
| Bootstrap complet sur worker, hash EXE inclus | 86,048 ms |
| Installation des hooks sur worker | 92,723 ms |
| Initialisation Unreal, première ouverture | 176,503 ms |
| Contexte ImGui | 0,039 ms |
| Atlas des polices | 1,588 ms |
| Ressources GPU ImGui | 3,326 ms |
| Avant première ouverture, 6 393 images | 2,453 µs/image |
| Fermé avec sample, 772 images | 35,569 µs/image |
| Ouvert, 19 662 images | 57,736 µs/image |
| Callback du sample, moyenne à l'arrêt | 5,386 µs |

Le transfert des exports du proxy sous loader lock était de 0,222 ms lors de la
première session. Les deux sessions précédentes donnent respectivement
2,627 / 35,566 / 63,120 µs et 2,372 / 34,114 / 61,295 µs
(`artifacts/client-first-run-metrics.json` et `client-second-run-metrics.json`).
Elles précèdent la correction de fermeture ; la table ci-dessus mesure le binaire final.

Ce sont les coûts CPU du framework avant Present original, callbacks inclus,
attente Present et coût GPU exclus. La première création est mesurée séparément
et exclue de la moyenne ouverte.

Shaders : **6,917 s vanilla / 6,985 s modifié**, soit +68 ms (~1 %) sur cette paire
d'essais avec cache existant. Ce n'est pas une mesure à cache froid. Le délai
total avant connexion a varié ; les autres phases n'ont pas été isolées.
Voir `artifacts/client-startup-comparison.json` et les journaux associés.

Les mesures WARP du banc isolé sont dans `build/client-fixture/metrics.json` ;
elles vérifient les cycles de ressources et ne remplacent pas les mesures sur le GPU réel.

## Tests et correction du crash de fermeture

**12/12 suites CTest et 50 vérifications PowerShell réussies.** Tous les projets
Release x64 sont compilés. Le banc D3D11 teste : contexte absent avant F1/fin des
shaders, sample via ABI publique, propriétaires, retraits depuis le dispatch,
retrait de soi-même, exception contenue, nettoyage et déchargement du sample,
resize réel DXGI, restauration de plusieurs render targets, destruction/recréation
du device et de la swap chain, retour au Present original et imports virtuels
du curseur. Les images Home et résolution réduite ont été inspectées :
`build/client-fixture/home.png` et `resized.png`.

Le crash signalé à la fermeture était réel. Le rapport de PID 50772 indique :
`All UObject delete listeners should be unregistered when shutting down the UObject array`.
Notre listener oubliait de se retirer dans OnUObjectArrayShutdown.
`ObjectDeleteListener` le retire maintenant **avant** le nettoyage des propriétaires,
suivant le contrat et l'implémentation UE4SS. Le test ObjectListenerShutdown utilise
le même observateur de production et reproduit la liste qui doit être vide,
y compris lorsque les mods étaient déjà arrêtés.

Les premières traces « sample unloaded » prouvaient seulement le retrait des
callbacks du mod, pas une sortie saine du jeu. Le code de sortie est maintenant
contrôlé et la validation réelle du correctif est consignée ci-dessous.

**Fermeture réelle vérifiée sur PID 32548 après ouverture F1 et initialisation
Unreal.** Arrêt normal, sans forçage : l'observateur journalise sa désinscription,
le journal du jeu atteint `LogExit: Exiting.`, aucune erreur critique et aucun
rapport de crash correspondant à ce processus. Preuves :
`artifacts/client-shutdown-verification.json`, `client-fixed-framework.log` et
`client-fixed-game.log`.

Le code de sortie de cette session n'a pas été récupéré par PowerShell ; il n'est
donc pas présenté comme égal à zéro. Le script d'arrêt conserve maintenant le
handle du processus avant sa fermeture. Cette récupération a ensuite été vérifiée
sur un processus auxiliaire contrôlé retournant le code 3. Le client corrigé a
été relancé depuis Win64 pour les essais utilisateur.

## Validation utilisateur à compléter

Les vrais journaux confirment l'arrivée à la connexion, le contexte différé,
les ouvertures/fermetures F1, Unreal Ready, le sample chargé/exécuté/retiré.
Cela ne prouve pas à lui seul le comportement physique de la souris/caméra.

L'outil de contrôle Windows n'a pas pu s'initialiser (erreur sandbox malgré une
reprise). Les manipulations ci-dessous ne sont pas présentées comme automatisées.

1. Attendre les shaders, ouvrir F1 : Home, Unreal Ready, D3D11, timings ; puis Mods.
2. Vérifier souris, clics et défilement. Aucun clic ne doit agir dans le jeu.
3. Fermer F1 : texte du sample encore visible ; vérifier caméra/déplacements en partie.
4. Tenir une touche de déplacement, ouvrir, relâcher, fermer : aucun mouvement bloqué.
   Refaire en gardant la touche tenue jusqu'à la fermeture, puis relâcher/réappuyer.
5. Alt+Tab menu ouvert puis retour : menu toujours ouvert, souris et F1 fonctionnels.
   Shift+Tab volontaire doit fonctionner et ne jamais se déclencher seul.
6. Changer de résolution et passer fenêtré/plein écran, puis revérifier le rendu
   et les contrôles. La recréation du device est testée dans le banc ; un véritable
   retrait/rétablissement du pilote n'a pas été provoqué.
7. Alt+F4 : fermeture normale, sans rapport de crash ni console UE4SS.

