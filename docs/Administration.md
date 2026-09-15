# Administration native

## Interface

Dans **F1 → Serveurs**, le bouton bouclier ouvre l'administration du serveur sélectionné :

- adresse du service, empreinte du certificat et mot de passe ;
- état du runtime, build du jeu, Unreal, durée de fonctionnement et nombre de mods ;
- mods, versions, auteurs, dépendances et erreurs ;
- valeurs enregistrées et valeurs actives des mods enregistrés ;
- actualisation et déconnexion.

**Enregistrer ne change pas une partie en cours.** Les réglages serveur sont appliqués au chargement. L'interface distingue donc les changements qui attendent le prochain redémarrage du serveur. Le bouton de redémarrage applique la sélection et les réglages enregistrés. Les DLL restent chargées jusqu’à l’arrêt du processus.

La durée et l'état affichés correspondent à la dernière actualisation. La disponibilité du backend Unreal n'est pas une mesure du nombre de joueurs ou de l'état du lobby.

## Architecture

- `Briefcase.Admin.Crypto` : primitives Mbed TLS et génération des identités en mémoire, compilables indépendamment du runtime Windows.
- `runtime/Briefcase.Admin/WindowsIdentity.cpp` : protection DPAPI des clés persistées existantes.
- `runtime/Briefcase.Admin` : TLS, identité, protocole JSON version 1, authentification, service serveur et persistance validée.
- `runtime/Briefcase.Client.Admin` : tâches réseau clientes différées, état d'interface et identités mémorisées.
- `runtime/Briefcase.NativeHost/AdminHost.inc` : adaptation du runtime existant, inventaire et schémas enregistrés par les mods.
- `runtime/Briefcase.Client.Menu/Administration.hpp` : interface ImGui.
- `tools/AdminSetup.cpp` : configuration locale et saisie masquée du mot de passe.

Les composants réseau sont liés statiquement au runtime commun. Le serveur n'initialise aucun service client et n'a aucune dépendance graphique. L'ABI publique des mods reste inchangée ; seul le pont privé du framework passe en version 5.

Les échanges utilisent Winsock et **Mbed TLS 3.6.7**, avec sa licence conservée. Schannel a été écarté après un test montrant son incompatibilité avec la clé éphémère utilisée : cela évite d'installer une clé privée dans le stockage Windows hors du dossier Briefcase.

## Identité et authentification

- TLS 1.3 ou TLS 1.2, AES-GCM et échange éphémère ; versions antérieures refusées.
- Vérification obligatoire de l'empreinte SHA-256 du certificat exact, de sa validité et de ses usages avant tout envoi du mot de passe.
- L'empreinte provient du fichier public communiqué par l'administrateur via un canal de confiance. Aucun certificat découvert sur le réseau n'est approuvé automatiquement.
- Mot de passe administrateur distinct d'un éventuel mot de passe de partie. Le client peut le mémoriser sur demande ; il n’est jamais journalisé.
- Mot de passe administrateur enregistré en clair dans le champ `password` de `Briefcase/Admin/server.json` (configuration version 2), conformément au choix de l’administrateur. Le service calcule son vérificateur PBKDF2 en mémoire au démarrage ; le protocole TLS reste inchangé.
- Anciennes configurations version 1 contenant un vérificateur toujours acceptées.
- Clé RSA 3072 protégée par DPAPI pour le compte Windows ayant configuré le serveur. Fichier privé réservé au propriétaire et à SYSTEM.
- Aucun certificat installé dans un magasin Windows, aucune règle de pare-feu créée et aucun port ouvert dans un routeur.
- Quatre connexions traitées au maximum ; quatre authentifications par adresse et douze au total par minute ; trente commandes par seconde et par connexion.
- Authentification sous dix secondes après le handshake. Une session authentifiée reste ouverte jusqu’à la déconnexion explicite, la fermeture du client, un changement de serveur, un redémarrage serveur ou une perte réseau. Le client envoie un message léger toutes les 30 secondes sur son worker réseau ; le serveur libère une connexion sans trafic après deux minutes. Aucune limite de durée de session n’est appliquée.

Changer le certificat exige d'approuver la nouvelle empreinte dans le client. Cette version charge les paramètres d'administration au démarrage : une modification locale du mot de passe ou de l'identité exige un redémarrage pour révoquer les anciennes sessions. La rotation automatisée des secrets n'est pas incluse.

## Configurer le serveur

L'administration est **désactivée** en l'absence de `Win64/Briefcase/Admin/server.json`.

Le script de déploiement existant accepte maintenant l’option **-RuntimeOnly** : elle met à jour le runtime et l’outil de configuration en conservant les DLL, réglages et sélection des mods. **-PlanOnly** permet de consulter les fichiers concernés. Le serveur doit être arrêté avant toute copie.

Après installation du paquet serveur, lancer dans un terminal interactif depuis son Win64 :

```powershell
& ".\Briefcase\Tools\Briefcase.AdminSetup.exe" `
    --root "$PWD\Briefcase" `
    --listen "127.0.0.1" `
    --port 50002 `
    --endpoint "127.0.0.1:50002"
```

Le mot de passe est demandé deux fois, sans écho, puis enregistré en clair dans `Briefcase/Admin/server.json`. Il ne passe jamais dans la ligne de commande. L'outil refuse de remplacer une identité déjà configurée.

Depuis les sources, le script équivalent accepte les chemins en paramètres :

```powershell
.\scripts\admin\Configure-Administration.ps1 `
    -ServerRoot "D:\DeceiveIncBackups\DeceiveIncNativeServer" `
    -ListenAddress "127.0.0.1" `
    -Port 50002 `
    -PublicEndpoint "127.0.0.1:50002"
```

Pour générer directement un mot de passe de 256 bits dans cette configuration, ajouter `--generate-password` à l’exécutable, ou `-GeneratePassword` au script. Aucun fichier de récupération supplémentaire n’est nécessaire et le mot de passe n’est pas affiché dans le terminal.

Extrait du fichier de configuration (les autres champs existants doivent être conservés) :

```json
{
  "version": 2,
  "password": "Votre-mot-de-passe-administrateur"
}
```

Le mot de passe accepte de 12 à 256 octets UTF-8. Pour le changer, modifier uniquement `password` puis redémarrer le serveur. Cela ne change ni son certificat ni son empreinte. Le fichier privé conserve ses permissions restrictives et ne doit pas être inclus dans les paquets.

Pour copier le mot de passe à partir de la configuration :

```powershell
.\scripts\admin\Copy-AdministrationPassword.ps1 -ServerConfig "<Win64>\Briefcase\Admin\server.json"
```

L’ancienne option `--generate-password-file` reste compatible : elle crée une copie de récupération locale chiffrée par DPAPI, en plus du mot de passe dans la configuration. Le mode `-PasswordFile` du script de copie permet toujours de lire ces anciens fichiers.

Pour un client distant, choisir explicitement l'adresse d'écoute et l'adresse accessible correspondantes. `127.0.0.1` limite l'accès à cette machine. Le pare-feu et la redirection TCP restent à configurer par l'administrateur si nécessaires.

Fichiers produits :

```text
Win64/Briefcase/Admin/
├── server.json    # privé : identité, clé TLS protégée, mot de passe en clair
└── pairing.json   # public : adresse, identité, empreinte
```

Conserver le fichier privé avec le compte Windows utilisé par le serveur. Seul `pairing.json` doit être transmis au client. Au premier lancement configuré, le journal du serveur annonce l'écoute TLS. Le Shipping doit toujours conserver Win64 comme répertoire de travail.

## Depuis le client

1. Ouvrir F1 → Serveurs → bouclier du serveur.
2. Copier l'adresse d'administration et l'empreinte depuis `pairing.json`.
3. Entrer le mot de passe et cliquer sur **Se connecter**.
4. Consulter l'état et les mods, puis modifier un réglage.
5. Cliquer sur **Enregistrer pour le prochain démarrage**.
6. Vérifier que la colonne **Actif** conserve la valeur du serveur en cours.
7. Dans **Serveur**, demander le redémarrage, confirmer, patienter puis se reconnecter pour vérifier les valeurs actives.

Les identités approuvées sont conservées dans `Win64/Briefcase/Admin/clients.json`, associées à l'adresse de jeu du favori. Cocher **Mémoriser le mot de passe sur ce client** permet de se reconnecter avec le champ vide. Le coffre `Admin/passwords.json` est protégé par DPAPI pour le compte Windows courant ; la protection lie le secret à l’adresse de jeu, à l’adresse d’administration et à l’empreinte du certificat. Changer d’identité ne réutilise donc pas le secret. **Oublier** retire la copie locale. Seule une authentification réussie enregistre le mot de passe ; le JSON d’interface contient uniquement un indicateur de présence. Le bouclier du listing se connecte automatiquement lorsqu’un mot de passe est enregistré pour cette identité. Un mot de passe refusé ramène à l’écran de connexion sans boucle de tentatives. Le retour au listing et F1 conservent la session. L’icône de déconnexion termine explicitement la session.

## Écritures et protocole

Chaque requête possède une version, un identifiant aléatoire, une opération et un objet de paramètres. Les messages JSON UTF-8 sont précédés de leur longueur sur quatre octets en ordre réseau. Limites : requête 64 Kio, réponse 1 Mio, profondeur JSON 16, clés dupliquées refusées.

Opérations : `hello`, `authenticate`, `server.status`, `mods.list`, `mod.config.read`, `mod.config.write`, `logout`, `server.config.read/write`, `mods.selection.read/write`, `balance.read/write`, `server.logs`, `server.restart`, `translations.read` (extension d’administration version 3).

Une écriture fournit le mod, la révision attendue, les valeurs complètes et un identifiant d'écriture. Le serveur choisit le chemin, valide les valeurs avec le schéma enregistré par le mod et remplace atomiquement le fichier. Les trois identifiants autorisés sont explicitement listés ; aucun chemin reçu du client n'est utilisé.

Les reçus d’idempotence concernent les écritures `mod.config.write`. Les autres sauvegardes utilisent une révision attendue et ne sont pas réessayées automatiquement.

Deux administrateurs travaillant sur la même révision ne peuvent pas écraser silencieusement leurs changements. Les 128 dernières écritures reconnues pendant ce processus ont un reçu : une répétition identique est idempotente et une réutilisation du même identifiant avec un autre contenu est refusée. Le client ne réessaie pas automatiquement une écriture après perte du réseau ; actualiser ou se reconnecter permet de vérifier la valeur enregistrée.

Les chemins redirigés et les fichiers liés sont refusés. Une erreur d'écriture conserve le fichier précédent. L'accès aux fichiers est réservé aux chemins enregistrés par le serveur.

## Onglets d’administration

Les noms internes du jeu sont conservés pour identifier précisément chaque ligne. Les bornes proviennent de `Community Balance Template/CommunityBalanceProfile.default.json` ; les champs absents de ce catalogue restent visibles en lecture seule. Les sauvegardes changent uniquement les valeurs demandées et préservent les métadonnées du profil. Le fichier actif reste `DeceiveInc/CommunityBalanceProfile.json`, limité à 1 Mio et 2048 entrées. Le jeu le recharge et produit son propre hash au redémarrage ; aucune substitution de RPC n’est ajoutée.

Les réglages serveur utilisent `Saved/Config/WindowsServer/TripwireServer.ini`. Les clés inconnues, autres sections et mots de passe administrateur existants sont conservés. Une sauvegarde précédente est placée dans `Briefcase/Admin` avant chaque modification. Les réglages affichés comme actifs sont le relevé du fichier au démarrage du service ; cela ne remplace pas une mesure des valeurs en mémoire du jeu.

Le paquet serveur contient `Briefcase/Tools/Briefcase.ServerRestart.exe`. Ce processus auxiliaire vérifie le chemin exact et la date de création du processus serveur, garde son handle et prépare sa ligne de lancement avant tout arrêt. Il attend l’accusé de réception de la réponse TLS, demande le nettoyage des mods, puis termine le processus restant et relance directement le Shipping avec **Win64 comme répertoire de travail**. Les arguments existants sont conservés ; les ports sont relus depuis l’INI. Aucun chemin ni commande système n’est accepté du client. Le résultat se trouve dans `Admin/restart-result.json`. L’auxiliaire n’est pas inclus dans le paquet client.

Le chiffrement TLS et les primitives communes restent inchangés. La mémorisation locale est isolée dans `Briefcase.Client.Admin/Credentials.*` ; une future version cliente Linux devra fournir son stockage de secrets. Cela n’ajoute aucune dépendance à ImGui ou aux entrées dans le serveur.

## Libellés, langues et recherche

Voir [Traductions et présentation](Localization.md) pour les fichiers JSON du framework, des mods et du serveur, les labels personnalisés et les catégories.

La configuration serveur est répartie en Identité, Réseau, Partie, Bots, Cartes et Heat. La recherche ne tient pas compte de la casse et porte sur le nom affiché, la clé et la valeur. Les valeurs de mot de passe sont exclues. Les colonnes gardent une marge intérieure, y compris dans l’en-tête. Les notifications sont encadrées en vert, rouge, orange ou bleu selon leur rôle.

Les réglages décimaux des mods sont édités et affichés avec trois décimales. Une modification est arrondie à ce pas avant son envoi : les soustractions répétées de 0,1 retombent sur zéro. Les petites valeurs du catalogue d’équilibrage restent prises en charge ; les valeurs existantes sur disque ne sont pas modifiées lors d’une simple consultation.

## Validation et limites

Tests natifs : identité DPAPI, TLS réel, authentification, accès non authentifié, mauvais certificat, mauvais mot de passe, limitation des tentatives, révisions, bornes des mods, concurrence, idempotence, annulation et persistance du client.

Un client CPython/OpenSSL indépendant vérifie TLS 1.2 et 1.3, framing fragmenté, tailles interdites, clés JSON dupliquées, profondeur, troncatures et arrêt avec une connexion inactive. Le banc ImGui vérifie le bouclier, la connexion, l'effacement du champ de mot de passe, l'enregistrement et les colonnes actif/enregistré.

Les mesures locales ne remplacent pas le test en partie. L'état précis du lobby, les joueurs, le hot reload, la modification des secrets à distance et l’exécution de commandes système arbitraires sont hors périmètre.

## Préparation de Linux

Le protocole TLS et le vérificateur de mot de passe sont indépendants du système. La configuration version 2 conserve le mot de passe administrateur en clair. PBKDF2-HMAC-SHA256 calcule un vérificateur uniquement en mémoire au démarrage. Ces opérations utilisent Mbed TLS sur chaque plateforme.

La cible `Briefcase.Admin.Crypto` sépare SHA-256, PBKDF2, l’aléa, RSA/X.509 et les buffers secrets du chargeur, d’Unreal et de Windows. Le projet CMake `tests/portable-crypto` permet de la compiler seule.

Le serveur complet reste Windows : chargement de DLL, hooks, Winsock et persistance doivent encore être portés. DPAPI est isolé dans `WindowsIdentity.cpp` et reste actif pour les clés existantes. La proposition de stockage portable est détaillée dans `PortableIdentityStorage.md` ; elle n’a pas encore été appliquée.

