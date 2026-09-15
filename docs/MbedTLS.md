# Mbed TLS

Version épinglée : **3.6.7**, branche LTS 3.6.

Archive officielle :
https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2

SHA-256 vérifié contre le fichier de sommes officiel :
`a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6`

Licence choisie : **Apache-2.0**. Le fichier `third_party/mbedtls-3.6.7/LICENSE` est conservé et distribué sous `Briefcase/Licenses/MbedTLS.txt`.

Le code amont n'est pas modifié. `runtime/Briefcase.Admin/TlsConfig.h` configure des mutex C++ standard, désactive le stockage PSA persistant, DTLS, les données précoces et la renégociation. Chaque connexion possède son contexte TLS ; les structures partagées PSA sont protégées par les callbacks de mutex configurés une fois au démarrage du service.

Mbed TLS est lié statiquement. Aucune DLL tierce supplémentaire n'est chargée par le jeu. La génération RSA/X.509, SHA-256, PBKDF2-HMAC-SHA256, l’effacement des buffers et l’aléa utilisent Mbed TLS dans la cible indépendante `Briefcase.Admin.Crypto`. La bibliothèque choisit sa source d’entropie système. Ce module ne contient aucun appel Windows.

Le stockage persistant actuel reste DPAPI, isolé dans `runtime/Briefcase.Admin/WindowsIdentity.cpp`, afin de conserver les protections et les identités existantes. Le transport Winsock et les opérations de fichiers sécurisées restent également spécifiques à Windows ; la compilation de ce seul module ne signifie donc pas que le serveur complet est porté sous Linux.
