# Stockage portable de l’identité d’administration — proposition à approuver

Le mot de passe administrateur et la clé privée TLS sont deux secrets distincts.

## Mot de passe administrateur

L’administrateur a explicitement choisi de stocker le mot de passe en clair dans le champ `password` de `Briefcase/Admin/server.json` (version 2). Le service calcule un vérificateur PBKDF2 en mémoire au démarrage. Les échanges restent sous TLS après vérification du certificat. Ce choix ne change pas la protection de la clé privée TLS décrite ci-dessous.

## Proposition pour la clé TLS

La clé privée pourrait être stockée en PEM standard, non chiffrée, dans `Admin/tls-private-key.pem`, avec accès limité au compte du serveur et aux administrateurs du système : ACL restrictive sous Windows, propriétaire du service et mode 0600 sous Linux, répertoire privé en mode 0700.

Une prochaine version de configuration référencerait ce nom de fichier relatif et conserverait le certificat public, le sel, le vérificateur, l’adresse et le port. La clé serait exclue des paquets utilisateurs et du dépôt Git.

Le serveur pourrait démarrer sans saisie d’un secret supplémentaire. La migration Windows convertirait la clé DPAPI existante en conservant le certificat, son empreinte, le mot de passe administrateur et les réglages. Une sauvegarde privée de la version précédente serait créée avant toute conversion. Aucun nouveau certificat ne serait approuvé automatiquement côté client.

**Conséquence concrète :** une personne obtenant ce fichier privé ou une sauvegarde qui l’inclut pourrait utiliser la clé sans disposer du compte Windows d’origine. Les permissions et les sauvegardes deviennent donc la protection du secret au repos. Le trafic réseau reste chiffré en TLS.

Cette migration n’est pas appliquée. Le contrôle automatique d’approbation a refusé de supprimer le chiffrement DPAPI de la clé TLS sans autorisation explicite portant sur cette clé.

## Alternative avec chiffrement au repos

Utiliser une clé privée PKCS#8 chiffrée par une phrase secrète fournie au démarrage ou par un gestionnaire de secrets indépendant. Ce choix conserve le chiffrement au repos, mais exige de définir comment le serveur obtient cette phrase. Stocker une seconde clé de déchiffrement à côté du fichier chiffré n’ajouterait pas une protection utile contre la copie des deux fichiers.
