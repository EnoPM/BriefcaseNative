# Traductions et présentation

Dans **F1 → Paramètres / Settings**, choisir la langue du menu. Français et anglais sont inclus. Le changement est immédiat ; le choix est conservé dans `Briefcase/ui-settings.json`. Le bouton **Recharger les traductions** relit les fichiers locaux sans redémarrer le jeu.

## Fichiers de langue

Un fichier JSON UTF-8 par langue (`fr.json`, `en.json`, `pt-BR.json`…). Le nom de fichier détermine le code. Exemple :

```json
{
  "language": "fr",
  "name": "Français",
  "translations": {
    "ui.settings": "Paramètres",
    "ui.setting": "Réglage"
  }
}
```

- Framework : `Briefcase/Localization/<langue>.json`. Le paquet fournit les textes du menu.
- Personnalisation locale : `Briefcase/Localization/Overrides/<langue>.json`, prioritaire sur le fichier fourni. Une mise à jour du paquet conserve ce dossier.
- Mod : `Briefcase/Mods/<identifiant>/Translations/<langue>.json`. Les clés sont automatiquement préfixées par `mods.<identifiant>.`. Seuls les paquets compatibles avec l’environnement participent.
- Serveur : `Briefcase/Translations/<langue>.json`. Les clés sont automatiquement préfixées par `server.`. Les catalogues du serveur et de ses mods sont transmis après authentification et actualisés via le bouton d’actualisation de l’administration.

Exemple de fichier `Translations/fr.json` d’un mod :

```json
{
  "translations": {
    "name": "Exemple de rendu",
    "settings.opacity": "Opacité du texte",
    "categories.display": "Affichage"
  }
}
```

Exemple de fichier `Briefcase/Translations/fr.json` côté serveur :

```json
{
  "translations": {
    "settings.ServerName": "Nom public du serveur",
    "categories.identity": "Identité",
    "balance.groups.Ace": "Ace",
    "balance.fields.Damage": "Dégâts",
    "balance.rows.Ace_Weapon_Base": "Arme principale"
  }
}
```

Le serveur ne peut fournir que des clés `server.` et `mods.`. Il ne peut pas remplacer les textes `ui.` du framework. Ses textes ne sont utilisés que dans sa vue d’administration ; les pages locales ne les héritent pas.

La recherche de traduction suit : langue exacte, langue de base (par exemple `fr` pour `fr-CA`), anglais, puis libellé de secours. Dans l’administration, le texte distant de la langue demandée est prioritaire sur son équivalent local. Une langue proposée dans Settings doit posséder un fichier de framework, même partiel. Les caractères latins étendus, grecs, cyrilliques et la ponctuation typographique sont inclus dans l’atlas Segoe UI ; les autres écritures demanderont une police adaptée.

Les directives de format (`%s`, `%u`, etc.) doivent rester identiques et dans le même ordre. Une traduction incompatible utilise le format de secours, sans jamais interpréter une directive fournie arbitrairement. Les identifiants ImGui restent stables lors d’un changement de langue.

Limites : 32 langues par catalogue, 4096 clés par langue, 256 octets par clé, 2048 octets par texte, 256 Kio par fichier et 512 Kio par catalogue agrégé. Les journaux, noms de serveurs saisis par l’utilisateur et diagnostics techniques gardent leur texte d’origine.

## Noms de réglages et catégories

Chaque mod peut ajouter `Data/presentation.json` pour définir les libellés et catégories de ses champs.

Le serveur peut ajouter `Briefcase/Admin/presentation.json` :

```json
{
  "serverConfig": {
    "ServerName": {
      "displayName": "Public name",
      "displayNameKey": "server.settings.ServerName",
      "category": "identity"
    }
  },
  "categories": {
    "identity": {
      "displayName": "Identity",
      "displayNameKey": "server.categories.identity"
    }
  },
  "balance": {
    "groups": { "Ace": { "displayName": "Ace" } },
    "rows": {
      "Ace_Weapon_Base": { "displayNameKey": "server.balance.rows.Ace_Weapon_Base" }
    },
    "fields": { "Damage": { "displayNameKey": "server.balance.fields.Damage" } },
    "settings": {
      "DT_Balancing_HitscanWeapons/Ace_Weapon_Base/Damage": {
        "displayName": "Primary weapon damage"
      }
    }
  }
}
```

`displayName` seul impose un libellé littéral. Avec `displayNameKey`, la traduction est utilisée et `displayName` sert de secours. Sans personnalisation, les clés automatiques sont `server.settings.<clé>` ou `mods.<id>.settings.<clé>`, et `<préfixe>.categories.<catégorie>`. Les réglages d’équilibrage acceptent les niveaux groupe, ligne, champ, puis réglage exact (`table/ligne/champ`).

Ces métadonnées changent la présentation, jamais le type, les bornes, la clé du fichier ou les valeurs. Actualiser l’administration relit les métadonnées du serveur. Les fichiers de personnalisation ne sont pas inclus dans les paquets et restent conservés au déploiement.

## Convention de nommage

Les clés et les textes de secours intégrés au code sont en anglais : par exemple
ui.add_server avec Add a server, ou ui.save_group avec Save this group.
Le français appartient aux valeurs de fr.json. La langue sélectionnée et la chaîne
de repli (langue exacte, langue de base, anglais, texte de secours) ne changent pas.
Les noms de paramètres persistés et les identifiants des groupes d’équilibrage
restent inchangés ; seuls leurs identifiants de traduction et leurs labels par
défaut sont normalisés. Les personnalisations peuvent toujours choisir un label.
Le test TranslationSourceContracts vérifie les références et l’accord des textes
littéraux de secours avec le catalogue anglais.

