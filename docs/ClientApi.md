# API native cliente v1

En-tête : `sdk/Briefcase.ClientModApi/include/Briefcase/ClientModApi.h`.
Exemple complet : `samples/Briefcase.NativeOverlaySample/Overlay.cpp`.

L'ABI commune `BcApi` v1 est conservée : 80 octets, préfixe historique de 72 octets inchangé.
Demander les services avec `get_service(context, nom, 1, &service)` ; vérifier le résultat et `size`.

| Service | Capacité du manifeste | Table |
| --- | --- | --- |
| `briefcase.client.render` | `client.render` | `BcClientRenderApi` |
| `briefcase.client.input` | `client.input` | `BcClientInputApi` |

Le serveur renvoie `BC_DENIED` pour ces services, sans charger le module graphique.
Les environnements `client`, `server`, `both` sont filtrés avant de charger les DLL.
Les mods client de cette version utilisent la phase `ready` ; la phase `startup`
avant entrée EXE reste réservée au serveur.

## Rendu

- `subscribe` retourne un handle associé au mod : 16 callbacks par mod, 128 au total.
  Ils s'exécutent sur le thread qui présente l'image.
- `unsubscribe` refuse un handle d'un autre propriétaire. Un retrait pendant le
  dispatch supprime les appels futurs ; un callback retiré avant son tour n'est pas appelé.
- Le host invalide les callbacks avant le déchargement logique du mod. Dispatch et
  retraits sont sérialisés. Les DLL restent mappées jusqu'à la sortie du processus,
  conformément au chargeur existant ; pas de hot reload.
- `viewport` donne la taille physique du backbuffer, le DPI, le numéro et l'intervalle
  d'image. Résultat `BC_NOT_READY` avant la découverte.
- `to_pixels` convertit les coordonnées normalisées `[0,1]` en coordonnées de bord
  `[0,width] / [0,height]`, origine en haut à gauche. NaN, infinis et hors-domaine sont refusés.
- `text` et `rectangle` ne fonctionnent que dans le callback du propriétaire.
  Couleurs `0xAABBGGRR` ; texte UTF-8, longueur explicite, 4 096 octets maximum ;
  1 024 primitives maximum par callback et image.
- Les primitives sont derrière le menu. Aucun device, objet Unreal ou contexte
  ImGui n'est exposé.

Les callbacks commencent après la création paresseuse du contexte : ouvrir F1
une première fois après les shaders. Le sample continue ensuite à dessiner quand
le menu est fermé. Ne pas accéder à Unreal depuis un callback de rendu.

## Entrées et durée de vie

`key` accepte un virtual-key Windows entre 1 et 255. `down`, `pressed` et `released`
sont stables pour l'image `frame_number` : plusieurs lectures ne consomment pas
l'appui. La perte de focus efface les états publiés. Lire une touche ne la capture pas.

`capturing` indique si le menu est ouvert et possède le focus. Aucun service de
simulation d'entrée n'est exposé aux mods.

Les fonctions exportées et callbacks du mod doivent contenir leurs exceptions.
Le host et le renderer interceptent aussi les exceptions C++ aux points d'appel
des mods ; un callback de rendu fautif est désactivé. Le sample n'importe que
l'ABI Briefcase et ne possède ni ImGui ni hook graphique.
