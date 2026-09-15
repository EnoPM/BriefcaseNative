# Première milestone — décisions

Le projet conserve l'idée du proxy version.dll et du bootstrap hors du verrou du chargeur,
les profils PE, les manifestes inventoriés avant chargement, le tri topologique et
les diagnostics horodatés de Briefcase. Il remplace le host managé par un host C++.
Les références ont été consultées en lecture seule.

Le proxy transfère les 17 exports de version.dll vers la DLL de System32, capture
le thread initial et charge Briefcase.NativeHost.dll sur un thread de travail.
Le host identifie le build avant d'installer le backend.

L'ABI des mods est un en-tête C : tailles, version, codes d'erreur, table de
fonctions, capacités et handles opaques. Le wrapper C++ est header-only. Ni STL,
ni types UE4SS, ni propriété mémoire entre CRT ne traversent cette frontière.
Les mods natifs restent du code de confiance : les capacités ne constituent pas
un bac à sable contre une DLL hostile.

Le backend compile les bibliothèques Unreal de RE-UE4SS v3.0.1, pas son application.
Les sources amont de GUI, Lua, consoles, dumpers et chargeurs de mods sont conservées
dans le checkout mais ne font pas partie de la cible distribuée. Le scanner Rust
patternsleuth reste une dépendance interne d'UE4SS ; les mods et l'API sont en C++.

## Thread et durée de vie

Le prototype amorce l'initialisation depuis Sleep sur le thread initial du jeu,
après un délai de cinq secondes. Cela permet de laisser le jeu construire ses
classes avant l'initialisation synchrone d'UE4SS. C'est un pont de bootstrap
spécifique au prototype, à remplacer par un point de cycle Unreal éprouvé dans
une milestone ultérieure. L'initialisation ne lit pas les objets depuis un worker.

Après initialisation, ProcessEvent et ce pont pompent une file bornée : maximum
1024 callbacks en attente, 32 traités par passage. À vide, seulement des contrôles
de thread et de drapeaux atomiques. Aucun parcours global d'objets par frame.
Un callback de mod doit rester court et ne pas lever d'exception.

Les handles appartiennent à un mod, ne sont jamais réutilisés dans le processus,
sont invalidés par le listener de destruction UObject et sont validés avec UE4SS
avant utilisation. Le plafond est de 4096 handles. Ils ne retiennent pas l'objet.
Cette milestone ne fournit ni hot reload ni déchargement des mods ; les DLL et
les contextes restent chargés jusqu'à la fin du processus.

## Paquets et dépendances

Un dossier par identifiant, un manifeste et une DLL d'entrée locale. Les versions
sont des triplets numériques stricts, sans plages ni suffixes pour cette milestone.
Une dépendance déclare un minimum inclusif. Doublons, cycles, dépendances absentes
ou trop anciennes invalident l'inventaire avant tout chargement. L'échec de chargement
d'une DLL empêche ensuite ses dépendants de démarrer.

## Ce qui est différé

Les anciens mods montrent les besoins futurs : BeginPlay, Spy, propriétés réfléchies,
ProcessEvent et états de match, avec beaucoup de code UE4SS et des pointeurs conservés.
Ils ne sont pas portés ici. Le futur SDK doit masquer ce code derrière des wrappers
typés et des handles. La production de snapshots et le SDK complet sont différés,
comme demandé. Aucun SDK dérivé d'un autre build n'est présenté comme celui du serveur.

CMake remplace le fichier de solution initialement suggéré : il orchestre C++, MASM,
les bibliothèques UE4SS et le scanner interne. Visual Studio peut ouvrir CMakeLists.txt.
