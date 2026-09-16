# Localization and presentation

Choose the menu language under **F1 → Settings**. English and French catalogs are
included. Changes apply immediately and persist in `Briefcase/ui-settings.json`.
**Reload translations** rereads local files without restarting the game.

## Language files

Use one UTF-8 JSON file per language (`en.json`, `fr.json`, `pt-BR.json`, and so
on). The filename defines the language code:

```json
{
  "language": "en",
  "name": "English",
  "translations": {
    "ui.settings": "Settings",
    "ui.setting": "Setting"
  }
}
```

- Framework: `Briefcase/Core/Localization/<language>.json` contains menu text.
- Local overrides: `Briefcase/Localization/Overrides/<language>.json` takes
  precedence and is preserved during package updates.
- Mod: `Briefcase/Mods/<id>/Translations/<language>.json`. Keys automatically
  receive the `mods.<id>.` prefix. Only packages compatible with the current
  environment contribute catalogs.
- Server: `Briefcase/Translations/<language>.json`. Keys automatically receive the
  `server.` prefix. Server and server-mod catalogs arrive after authentication and
  refresh through the administration refresh action.

Example mod `Translations/en.json`:

```json
{
  "translations": {
    "name": "Overlay sample",
    "settings.opacity": "Text opacity",
    "categories.display": "Display"
  }
}
```

Example server `Briefcase/Translations/en.json`:

```json
{
  "translations": {
    "settings.ServerName": "Public server name",
    "categories.identity": "Identity",
    "balance.groups.Ace": "Ace",
    "balance.fields.Damage": "Damage",
    "balance.rows.Ace_Weapon_Base": "Primary weapon"
  }
}
```

The server may supply only `server.` and `mods.` keys. It cannot replace framework
`ui.` text. Remote text applies only inside that server's administration view.

Lookup order is exact locale, base language (`fr` for `fr-CA`), English, then the
fallback label. In administration, remote text for the requested locale takes
precedence over a local equivalent. Every language offered in Settings needs at
least a partial framework catalog. The Segoe UI atlas covers extended Latin,
Greek, Cyrillic and typographic punctuation; other scripts require another font.

Format directives such as `%s` and `%u` must remain identical and in the same
order. An incompatible translation falls back safely. Arbitrary translated text
is never interpreted as a format string. ImGui identifiers remain stable when the
language changes.

Limits per catalog are 32 languages, 4,096 keys per language, 256 bytes per key,
2,048 bytes per value, 256 KiB per file and 512 KiB for an aggregated catalog.
Logs, user-entered server names and technical diagnostics retain their source text.

## Setting and category names

A mod may add `Data/presentation.json` to define field labels and categories. The
server may add `Briefcase/Admin/presentation.json`:

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

`displayName` alone forces a literal label. With `displayNameKey`, the translation
is used and `displayName` is its fallback. Without customization, generated keys
are `server.settings.<key>` or `mods.<id>.settings.<key>`, plus
`<prefix>.categories.<category>`. Balance presentation may target a group, row,
field or exact `table/row/field` setting.

Presentation metadata never changes a type, bound, persisted key or value.
Refreshing administration rereads server metadata. Customization files are absent
from packages and survive deployment.

## Naming convention

Keys and fallback strings embedded in code are English, for example
`ui.add_server` with `Add a server` and `ui.save_group` with `Save this group`.
French text belongs in `fr.json`. Persisted setting names and balance group IDs do
not change; only translation IDs and default labels are normalized. The
`TranslationSourceContracts` test checks references and ensures literal fallbacks
match the English catalog.
