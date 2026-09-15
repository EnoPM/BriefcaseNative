"""Audit shipped translation references and English literal fallbacks."""
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
catalogs = {lang: json.loads((root / "resources" / "Localization" / f"{lang}.json").read_text(encoding="utf-8"))["translations"] for lang in ("en", "fr")}
assert catalogs["en"].keys() == catalogs["fr"].keys(), "Catalogue keys differ"
english = catalogs["en"]
string = r'"(?:\\.|[^"\\])*"'
pair = re.compile(r'("(?:ui|server|mods)\.[^"]+"\s*,\s*)(' + string + r'(?:\s*' + string + r')*)')
errors, checked = [], 0
for path in (root / "runtime" / "Briefcase.Client.Menu").iterdir():
    if path.suffix not in (".cpp", ".hpp", ".inc"):
        continue
    source = path.read_text(encoding="utf-8")
    for key in re.findall(r'"(ui\.[a-zA-Z0-9_.]+)"', source):
        if key not in english:
            errors.append(f"{path.name}: missing key {key}")
    for match in pair.finditer(source):
        key = json.loads(re.match(string, match[1])[0])
        if key not in english:
            continue
        fallback = "".join(json.loads(s[0]) for s in re.finditer(string, match[2]))
        checked += 1
        if fallback != english[key]:
            errors.append(f"{path.name}: fallback differs from English catalogue for {key}: {fallback!r}")
for old in ("ui.serveurs", "ui.ajouter_un_serveur", "ui.enregistrer_ce_groupe", "server.balance.groups.Commun", "server.balance.groups.PNJ"):
    assert old not in english, f"Legacy translation key {old}"
for path in (root / "samples").rglob("Translations/en.json"):
    data = json.loads(path.read_text(encoding="utf-8"))["translations"]
    french_path = path.with_name("fr.json")
    assert data.keys() == json.loads(french_path.read_text(encoding="utf-8"))["translations"].keys(), path
assert checked > 150, f"Source audit covered only {checked} fallbacks"
assert not errors, "\n".join(errors)
print(f"PASS {checked} English fallbacks, complete menu references and matching catalogue keys")
