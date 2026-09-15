# Hooks natifs validés
Le service public briefcase.native-hooks, version 1, complète les hooks réfléchis.
Il exige native.hooks, unreal.hooks et unreal.reflection. Le mod résout d'abord
une UFunction avec sa signature exacte, puis fournit un BcNativeSite propre au build.

Le runtime contrôle le SHA256 du serveur, l'adresse de la Func UHT, la totalité de
la fenêtre exec (5–512 octets), la frontière d'instruction Zydis du CALL/JMP relatif,
sa destination et les octets du prologue cible (16–128 octets), dans des sections
exécutables. Une erreur interdit l'installation. Aucun choix heuristique d'adresse.

Les formes initiales sont des méthodes x64 retournant void, sans paramètre ou avec
un float, bool ou UObject*. Aucune référence C++, valeur de retour ou structure.
Le backend privé utilise MinHook 1.3.4 ; la relocalisation et la suspension brève des
autres threads ont lieu uniquement à l'installation/retrait. Aucune DLL du jeu
n'est modifiée sur disque. Les licences et empreintes de la dépendance sont livrées.

Les callbacks BcHookEvent PRE/POST s'exécutent sur le game thread, transport NATIVE.
Ils peuvent lire les paramètres via read_argument et l'objet via les handles publics.
Un appel hors game thread exécute uniquement vanilla. L'original est toujours appelé
une fois ; PRE peut modifier un argument avec write_argument et la capacité unreal.write.
La suppression de l'original n'est pas exposée.
Les appels réfléchis peuvent aussi traverser ce corps natif : utiliser un seul
transport pour une modification. Les observateurs distincts peuvent documenter les deux.

unhook retire une inscription propriétaire. Le dernier retrait restaure le code ;
s'il survient dans un callback, la restauration attend la fin de l'appel courant.
Les trampolines et les DLL restent mappées jusqu'à la sortie du processus. L'arrêt
via Stop-Server.ps1 retire aussi les inscriptions oubliées. Limites : 32 cibles,
64 inscriptions par propriétaire, 512 inscriptions au total, 32 callbacks imbriqués.

La lecture de propriété accepte aussi un chemin de structures, par exemple
HeatState.HeatCount : 8 segments maximum, aucune traversée de pointeur implicite,
aucune collection, chaque offset vérifié dans son conteneur. Les résultats primitifs
sont copiés et les résultats objet sont des handles possédés. Tout propriétaire peut
valider/libérer ses handles, quelle que soit l'API qui les a produits.

Tests : NativeHookContracts vérifie les fenêtres, limites, frontières et chemins,
puis un vrai détour float dans un processus de test : original une fois, valeur intacte,
retrait et restauration exacte des octets. Le service doit ensuite être vérifié sur
les signatures du serveur avant qu'un mod soit considéré chargé.
