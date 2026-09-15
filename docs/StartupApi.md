# Services natifs de démarrage (0.2)
Le préfixe ABI v1 de 72 octets reste identique. Une fonction get_service est ajoutée en fin
de table ; vérifier size avant de la lire. Les services ont leur propre version (1).

## Configuration
Capacité config, service briefcase.config : load reçoit un schéma JSON borné à 64 Kio.
Types : boolean, integer signé 64 bits, number fini, string UTF-8, array de string.
Chaque propriété déclare default et description ; minimum, maximum et enum sont optionnels.
Valeurs hors bornes, types incorrects, clés inconnues et dupliquées sont refusés.
Les défauts sont appliqués aux clés absentes. Data/config.json est lu une fois lors de
BriefcaseModLoad. Le runtime écrit les défauts si absent et expose Data/config.schema.json.
Aucune lecture périodique. Une sonde de taille renvoie BC_LIMIT, required inclut le NUL.
La lecture suivante retourne les mêmes valeurs mises en cache.
L'appel est autorisé uniquement pendant le chargement du mod propriétaire.

## Préparation avant entrée EXE
loadPhase: startup dans le manifeste. Le proxy place un point d'arrêt unique à l'entrée PE,
restaure l'octet et retire son gestionnaire avant de charger le runtime et les mods.
Le chargement n'a pas lieu sous le loader lock. Le jeu et son tick n'ont pas encore commencé :
il n'y a aucun délai ni attente d'un worker dans le game thread en cours de partie.
Le test Windows EntryGateBeforeExe vérifie cet ordre et le retrait complet du point d'arrêt.
Le processus s'arrête avec code 119 si la préparation échoue. Aucun démarrage partiellement
configuré n'est autorisé. Les bibliothèques restent chargées jusqu'à la fin du processus.

La technique utilise les [exceptions vectorisées Windows](https://learn.microsoft.com/en-us/windows/win32/debug/vectored-exception-handling).
Le contexte x64 est redirigé vers une fonction normale, pas vers du travail dans le gestionnaire.
Les initialisateurs TLS de l'exécutable précèdent son point d'entrée ; un mod nécessitant
une interception avant ces initialisateurs ne peut pas utiliser ce service.

## Transactions
Service briefcase.startup :
- server-config.ini permet read_ini/stage_ini avec un alias déclaré dans iniBindings.
  Chaque alias désigne un simple nom .ini dans Saved/Config/WindowsServer ; aucun chemin
  arbitraire ni accès au dossier d'un autre mod. Fichiers UTF-8/ASCII, maximum 1 Mio,
  sections/clefs ambiguës et injections de nouvelles lignes refusées.
- startup.immediate permet stage_i32, un lot maximum de 64 opérandes MOV registre32,imm32.
  SHA256 exact, fenêtre .text attendue intégrale, RVA borné et décodage Zydis obligatoires.
  Aucun pointeur du jeu ni primitive libre de lecture/écriture mémoire n'est exposé.
  Les branches, adresses relatives, écritures mémoire et opérandes qui se chevauchent sont refusés.

Le runtime valide et prépare pendant BriefcaseModLoad, puis valide encore les valeurs originales
et les fichiers avant commit. Les fichiers sont sauvegardés et remplacés atomiquement ;
les quatre octets de chaque immédiat sont protégés temporairement et le cache CPU invalidé.
Tout échec déclenche la restauration des modifications déjà effectuées, puis l'arrêt du processus.
Les modifications des instructions existent uniquement en mémoire. L'exécutable sur disque reste intact.
Les valeurs INI persistent volontairement pour correspondre à la configuration du prochain démarrage.
Les API de préparation sont refusées après cette phase ; pas de rechargement à chaud.

## Sélection et dépendances
Briefcase/settings.json accepte enabledMods, liste explicite d'identifiants.
Un paquet demandé mais absent empêche le démarrage. Sans ce fichier, comportement M1 :
chargement de tous les paquets compatibles présents. Une dépendance startup ne peut pas
exiger un mod chargé après initialisation Unreal (phase ready).
