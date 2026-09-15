# Dépendances épinglées

Source amont : https://github.com/UE4SS-RE/RE-UE4SS/releases/tag/v3.0.1
Release : v3.0.1, commit d935b5b23bac03b65c14ae38382b02007204cc2e.
Le clone expérimental local n'est ni copié ni modifié.

Sous-modules exacts :
- deps/first/Unreal (UEPseudo) : d09b7218bfe7392adeffb500fdeee0b42ca1cd27.
- deps/first/patternsleuth : 33e731e99f2a6bb7f65a8e95e89fd1c06ce9d1d2.

Dépendances CMake :
- nlohmann/json v3.11.3 : 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03, MIT.
- Zydis v4.1.0 : 569320ad3c4856da13b9dbf1f0d9e20bda63870e, MIT.
- PolyHook2 : fd2a88f09c8ae89440858fc52573656141013c7f, MIT.
- Les versions des sous-modules transitifs viennent de ces commits.
- patternsleuth : MIT OR Apache-2.0 ; crates verrouillées par le Cargo.lock amont.

Options : Release x64, C++23, CRT dynamique, UE_GAME, UE_BUILD_SHIPPING,
PLATFORM_WINDOWS, noms Unreal non case-preserving, bibliothèques UE4SS statiques,
profilage désactivé, BRIEFCASE_UE4SS_HEADLESS=ON. Aucune application UE4SS n'est liée.
Le cache de parcours UObject est désactivé. Les initialisations ne sont pas réessayées
dans une boucle de production après un échec.

Le fork local est matérialisé par un checkout détaché propre puis un script de patches
reproductible, sans commit ni publication. Le checkout est ignoré par le dépôt parent ;
le script de récupération et les pins sont versionnables. Aucun commit n'a été créé.

Adaptations à conserver dans scripts/build/Patch-UE4SS.ps1 :
- Une tentative de scan et une vérification des classes requises, sans attente longue
  sur le game thread.
- Désactivation des hooks console, struct-link et construction inutiles au prototype.
- Remplacement de l'alias byte implicite par uint8_t.
- Appel unordered_map::find standard sans argument template explicite.
- Déclaration anticipée de l'enum EAspectRatioAxisConstraint manquante en amont.
- Ajustements CMake externes pour les includes de PolyHook, la sélection des bibliothèques,
  les définitions de lien statique et l'exclusion du périphérique console.

Les autres sources du fork ne sont pas supprimées. Le build ne réutilise aucun binaire
de la référence locale. Les PDB et les sources restent dans le projet.

Sous-modules transitifs effectivement liés :
- AsmJit : 3577608cab0bc509f856ebf6e41b2f9d9f71acc4.
- AsmTK : 6e25b8983fbd8bf455c01ed7c5dd40c99b789565.
- Zycore externe v1.5.0 : 74620eefd233bec20daeb66e78e744ff06e273b7.
Le sous-module Zydis embarqué dans PolyHook est récupéré par Git mais non lié :
le build utilise exclusivement le Zydis externe épinglé ci-dessus.
