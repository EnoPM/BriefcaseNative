# Menu Serveurs et icônes — 14 septembre 2026

## Interface

Le menu principal est fixe : 5 % de marge sur chaque côté horizontal et 10 % en
haut et en bas, soit 90 % de la largeur et 80 % de la hauteur du viewport du jeu.
Sa position et sa taille suivent automatiquement la résolution. Le déplacement
et le redimensionnement à la souris sont désactivés.

- Navigation Home / Mods / Serveurs avec icônes et libellés centrés verticalement.
- Panneau de contenu commun aux trois onglets : marge intérieure de 22 px sur les côtés
  et 20 px en haut et en bas, adaptée au DPI, y compris sans bordure visible.
- Lignes de navigation de 44 px, icônes de 20 px, espacements ajustés au DPI.
- Boutons d'action de 36 px : ajouter (+), rejoindre (flèche), supprimer (corbeille).
- Bouton de fermeture avec croix et infobulle « Fermer le menu (F1) ».
- Infobulles sur les actions, marge des boutons texte de 14 × 9 px.
- Formulaire d'ajout assorti au thème violet, de largeur fixe (440 px adaptés au DPI).
  Sa hauteur seule suit le contenu ; un test sur 180 images vérifie que sa largeur ne dérive pas.
- Icônes vectorielles originales dessinées avec ImDrawList : aucune police,
  texture, dépendance tierce ou ressource supplémentaire au démarrage.

Les captures dans `build/menu-fixture/` proviennent d'un banc hors écran utilisant
le code du menu et le backend DX11 ImGui : `home.png`, `servers.png`, `add.png`,
`tooltip.png` et `resized.png`. Les adresses de démonstration appartiennent uniquement
au banc de test ; elles ne sont pas installées dans le client.

## Liste locale

Chaque entrée contient un identifiant stable, un nom et une adresse avec port.
IPv4, noms d'hôtes et IPv6 entre crochets sont acceptés. Les adresses sont normalisées,
les doublons rejetés. La limite est de 64 entrées.

La liste est enregistrée dans :

`D:\DeceiveIncBackups\DeceiveIncNativeClient\DeceiveInc\Binaries\Win64\Briefcase\servers.json`

Le fichier est créé au premier ajout. Il n'est pas fourni par le paquet et n'est
pas remplacé lors du déploiement. La lecture et la sauvegarde s'exécutent sur le
worker du host ; le rendu échange seulement des instantanés et des demandes.
La sauvegarde remplace le fichier après écriture temporaire complète. Une erreur
conserve l'ancien fichier et l'ancienne liste en mémoire. Un fichier invalide reste
intact et la liste affiche l'erreur sans l'écraser. Les liens et jonctions sont rejetés.

## Connexion

L'action reprend le chemin du mod C# de référence
`managed/builtins/CommunityServerConnection.cs`, consulté en lecture seule :
`/Script/DeceiveInc.EOSServerBrowserSubsystem:DirectConnect` sur l'instance vivante,
sur le game thread via la file existante du backend.

Avant l'appel, le code contrôle les métadonnées du build 6A96564B-06283000 :
deux paramètres FString d'entrée, IPPort à l'offset 0 et Password à l'offset 16,
tampon de 32 octets. Le mot de passe est vide pour cette version.
Aucun objet Unreal ou graphique n'est exposé dans l'ABI publique.

Le menu reste ouvert en cas d'erreur de préparation et affiche le message.
Après transmission à DirectConnect, il se ferme et rend les entrées au jeu.
Ce succès signifie que la demande a été transmise ; il ne garantit pas que le
serveur distant l'accepte. Le jeu conserve sa gestion des erreurs réseau.

## Fichiers principaux

- `runtime/Briefcase.Client.Menu/Icons.hpp`, `Servers.hpp` et `Menu.cpp`.
- `runtime/Briefcase.Client.Servers/ServerDirectory.hpp` et `ServerDirectory.cpp`.
- `runtime/Briefcase.NativeHost/ClientServers.inc`, `ClientHost.inc` et `Shutdown.inc`.
- `runtime/Briefcase.UnrealBackend/ClientConnection.hpp` et `ClientConnection.inc`.
- `runtime/Briefcase.Client.Rendering/ClientBridge.h` et validation dans `Rendering.cpp`.
- `tests/ServerDirectoryContracts.cpp` et `ClientMenuFixture.cpp`.
- `CMakeLists.txt` et adaptation du banc `ClientRenderingFixture.cpp`.

Seul le pont privé host/rendu passe en version 2 ; l'ABI publique des mods est inchangée.
Le socle commun reste identique entre les paquets. Le serveur ne dépend d'aucun
module graphique.

## Validation

Compilation Release x64 réussie. **14/14 suites CTest et 50 contrôles PowerShell**.
Les nouveaux tests couvrent la persistance, les échecs de remplacement, les doublons,
les adresses invalides, les limites, le contrat DirectConnect et les interactions
réelles du menu via l'IO de son contexte hors écran : onglet Serveurs, ajout clavier,
suppression, ID sélectionné pour rejoindre, fermeture, infobulle et résolution réduite.
Le test de connexion du banc emploie une destination simulée ; l'appel au serveur réel
est vérifié séparément avec l'utilisateur.

Paquets client/serveur audités, sans PDB et sans dépendance graphique côté serveur.
Déploiement limité à DeceiveIncNativeClient ; lancement direct du Shipping avec Win64
comme répertoire de travail. Journaux : `artifacts/package-client-servers.log`,
`deploy-client-servers.log`, `client-servers-launch.json`.

Validation de connexion en jeu confirmee par l'utilisateur : ajout du serveur, clic sur Rejoindre, entree sur le serveur reussie. Le journal du client PID 42664 confirme DirectConnect a 69,349 s, puis la fermeture du menu et la restauration des entrees a 69,355 s.

Pour tester : F1 → Serveurs → +, saisir le nom et l'adresse (par exemple
`127.0.0.1:7777` si le serveur local écoute sur ce port), puis Rejoindre.
Vérifier également la suppression et la conservation des entrées après redémarrage.

Aucun commit, push ou publication.
