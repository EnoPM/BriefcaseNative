# Service public briefcase.unreal v1

ABI C uniquement, structures à taille/version fixes. Voir UnrealApi.h et les wrappers Unreal.hpp.

Les pointeurs Unreal restent privés au backend. Le mod déclare séparément unreal.reflection,

unreal.invoke, unreal.hooks et unreal.lifecycle. Une table accessible ne contourne pas les

contrôles de capacités de chaque appel. Toutes les opérations se font dans le game thread,

après initialisation ; utiliser post_game_thread depuis BriefcaseModLoad.



## Réflexion et invocation

resolve_function reçoit un chemin /Script complet et une signature JSON exacte :

parameterSize, parameters (name, type, offset, return optionnel), flags optionnel.

Chaque nom, type, offset et taille est comparé aux métadonnées réelles.

describe_function expose ces métadonnées sans renvoyer de pointeur.

La première version accepte les classes compilées /Script, dont la durée de vie couvre le serveur.

Les métadonnées de fonctions Blueprint dynamiques ne sont pas mises en cache.



Valeurs supportées : int32, float fini, bool, byte/enum de taille 1, objet et FVector.

Les noms d'arguments sont uniques, les types exacts et les valeurs hors représentation sont refusées.

Les retours utilisent BcValue ; les paramètres out/référence non pris en charge sont refusés.

Une invocation alloue un buffer local aligné et borné à 4096 octets ; aucun buffer brut n'est exposé.

Les propriétés peuvent être lues par nom avec validation des types et bornes du conteneur.

Pas d'écriture de propriété dans cette version.



## Handles

Les handles sont propres à un mod. retain prolonge leur durée de vie ; release_handle libère

une référence. Un objet détruit invalide tous ses handles, même si son adresse est réutilisée.

Les handles self/function fournis aux hooks sont empruntés. Retenir self avant de le conserver.

Les valeurs objet rendues par les lectures et l'invocation sont possédées : les libérer.

on_deleted signale dans le game thread les handles devenus invalides ; aucune lecture de l'objet

détruit n'est permise. ObjectInfo.flags expose seulement ClassDefaultObject (16) et ArchetypeObject (32).



## Hooks

hook observe préfixe ou postfixe, sans suppression de l'appel vanilla.

ProcessEvent et les Func thunks réfléchis sont couverts avec déduplication.

Les appels directs au corps C++ d'une fonction ne passent pas par ce transport.

Le callback reçoit un contexte éphémère call pour read_argument.

Les paramètres sont disponibles pour ProcessEvent et les frames native dont Code est nul.

Une frame bytecode non matérialisée reste observable via self ; read_argument refuse sa mémoire.

Aucune interprétation spéculative de la pile VM.

Chaque enregistrement supprime sa propre réentrance et peut se retirer pendant un callback.

Les exceptions désactivent le callback fautif et laissent l'appel vanilla s'exécuter.



Bornes : 4096 handles, 256 métadonnées, 128 cibles de thunk, 512 hooks au total / 64 par mod,

32 callbacks imbriqués avec contexte de paramètres. Dispatch indexé par UFunction,

sans parcours de GUObjectArray ni recherche globale périodique.



## Arrêt

BriefcaseModUnload est appelé dans le game thread lors de la demande de nettoyage.

Le runtime retire ensuite tous les hooks, abonnements et tâches du propriétaire et invalide ses handles.

Les thunks originaux sont restaurés quand leur dernière inscription est retirée.

Les DLL restent mappées ; pas de déchargement à chaud.

Stop-Server demande le nettoyage via un événement local identifié par PID, attend l'accusé de réception,

puis termine uniquement l'exécutable de l'installation configurée.

Une terminaison forcée externe ne peut pas garantir l'exécution de callbacks.



## Vérification

ReflectionContracts vérifie signatures incompatibles, propriétaires, réutilisation d'adresse,

rétention, retrait durant dispatch, suppression de réentrance et exceptions.

Le paquet de test RuntimeProbe utilise uniquement le SDK public : Abs_Int(-7) sur le CDO

KismetMathLibrary, observations pré/post uniques, retrait effectif et refus de paramètres invalides.

Il ne simule ni Déployer ni transition de phase de jeu.



La libération/validation d'un handle appartient à son contexte ; elle ne requiert pas unreal.find.

La lecture typée accepte maintenant les chemins de structures bornés, par exemple HeatState.HeatCount.

Les appels C++ directs peuvent être observés séparément : [contrat des hooks natifs](NativeHooks.md).


## Écritures typées — extension v1 du 14 septembre 2026

La table BcUnrealApi conserve ses 96 premiers octets et ajoute write_property puis
write_argument (taille totale 112 octets). Les anciens paquets restent compatibles.
Un nouveau paquet doit vérifier la taille du service ; Services::service le fait déjà.

La capacité unreal.write est requise, avec unreal.reflection pour une propriété ou
unreal.hooks pour un argument. Les contrôles de propriétaire et de game thread restent actifs.

write_property accepte les scalaires et vecteurs pris en charge par read_property, sur
une instance vivante, y compris les chemins de structures bornés. Elle refuse les CDO,
archétypes, pointeurs objets, collections et types non pris en charge. Les types, tailles,
bornes numériques et valeurs finies sont vérifiés avant écriture. Elle ne déclenche ni
réplication ni OnRep automatiquement : le mod reste responsable de choisir le bon événement.

write_argument agit seulement pendant PRE sur un paramètre d'entrée matérialisé.
POST, retours, références/out et frames VM sans buffer sont refusés. L'appel est transitoire
et appartient au mod. Pour un hook natif, le trampoline reçoit le paramètre modifié une
seule fois ; le mod ne peut pas supprimer l'original.

RuntimeProbe vérifie réellement Abs_Int(-3) → Abs_Int(-21) par écriture PRE, puis 3 après
retrait ; mauvais type, écriture du retour, POST, CDO, appel périmé et faux propriétaire
sont refusés. Le test ne modifie aucune phase ou instance de gameplay.
