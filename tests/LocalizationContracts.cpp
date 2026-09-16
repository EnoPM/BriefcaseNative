#include "../runtime/Briefcase.Admin/Service.hpp"
#include "../runtime/Briefcase.Client.Menu/I18n.hpp"
#include "../runtime/Briefcase.Client.Menu/Numbers.hpp"
#include "../runtime/Briefcase.Localization/Client.hpp"
#include <fstream>
#include <iostream>
using namespace bc::locale;
static unsigned checks;
static void check(bool condition, const char *what) {
    ++checks;
    if (!condition)
        throw std::runtime_error(what);
}
template <class Fn> static void rejects(Fn fn, const char *what) {
    try {
        fn();
    } catch (...) {
        ++checks;
        return;
    }
    throw std::runtime_error(what);
}
static void write(const Path &p, const Json &j) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p) << j.dump(2);
}
static Json await(Client &client) {
    uint64_t version = 0;
    std::string text;
    Json state;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < end) {
        if (client.snapshot(text, version))
            state = Json::parse(text);
        if (!state.value("pending", true) && !state.value("catalogues", Json::object()).empty())
            return state;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Language worker timeout");
}
int main() {
    try {
        auto bundled =
            read_languages(Path(__FILE__).parent_path().parent_path() / "resources" / "Localization");
        check(bundled.size() == 2, "bundled languages");
        for (auto it = bundled["fr"].begin(); it != bundled["fr"].end(); ++it) {
            check(bundled["en"].contains(it.key()), "English translation missing");
            check(bc::menu::i18n::tokens(it.value().get<std::string>()) ==
                      bc::menu::i18n::tokens(bundled["en"][it.key()].get<std::string>()),
                  "translated format signature differs");
        }
        check(lookup(bundled, "fr-CA", "ui.settings", "Settings") == "Paramètres", "base language fallback");
        check(lookup(bundled, "de", "ui.settings", "Settings") == "Settings", "English fallback");
        check(lookup(bundled, "fr", "unknown", "fallback") == "fallback", "literal fallback");
        for (auto code : {"fr", "en", "pt-BR", "zh-Hant"})
            check(valid_language(code), "valid language rejected");
        for (auto code : {"../en", "en.json", "FR", "en/xx", ""})
            check(!valid_language(code), "unsafe language accepted");
        rejects([] { validate_catalogues({{"fr", {{"ui.home", "Home"}}}}, true); },
                "server overrides framework");
        rejects([] { validate_catalogues({{"fr", {{"server.test", 1}}}}, true); },
                "numeric translation accepted");
        rejects([] { validate_catalogues({{"fr", {{"server.test", std::string(2049, 'x')}}}}, true); },
                "oversize translation accepted");
        rejects([] { validate_presentation({{"displayName", 7}}); }, "numeric label accepted");
        rejects([] { validate_presentation({{"category", std::string("a\0b", 3)}}); },
                "NUL category accepted");
        auto schema =
            Json{{"properties", {{"multiplier", {{"type", "number"}, {"minimum", 0}, {"maximum", 10}}}}}};
        auto decorated = apply_presentation(
            schema, {{"multiplier", {{"displayName", "Consommation"}, {"category", "movement"}}}},
            {{"movement", {{"displayName", "Déplacements"}}}}, "mods.sample.display");
        auto field = decorated["properties"]["multiplier"];
        check(field["displayName"] == "Consommation" && field["displayNameKey"] == "" &&
                  field["categoryLabel"] == "Déplacements" && field["categoryKey"] == "",
              "literal presentation precedence");
        check(field["minimum"] == 0 && field["maximum"] == 10 && field["type"] == "number",
              "presentation changed validation");
        auto automatic = apply_presentation(schema, Json::object(), Json::object(),
                                            "mods.sample.display")["properties"]["multiplier"];
        check(automatic["displayNameKey"] == "mods.sample.display.settings.multiplier" &&
                  automatic["categoryKey"] == "mods.sample.display.categories.general",
              "default keys not scoped");

        namespace ui = bc::menu::i18n;
        ui::state = {{"language", "fr"}, {"catalogues", bundled}};
        ui::cache.clear();
        check(ui::matches("CAPACITÉ", "Capacité maximale", "itemLimit", 12), "Unicode label search");
        check(ui::matches("itemlimit", "Capacité maximale", "itemLimit", 12), "raw key search");
        check(ui::matches("12", "Capacité maximale", "itemLimit", 12), "value search");
        check(ui::matches("oui", "Activer", "enabled", true), "translated boolean search");
        check(!ui::matches("s3cr3t", "Mot de passe", "Password", "s3cr3t", true), "secret searchable");
        ui::set_remote({{"fr", {{"mods.sample.display.name", "Exemple distant"}}}});
        ui::use_remote(true);
        check(ui::tr("mods.sample.display.name", "Local") == "Exemple distant", "remote translations unavailable");
        ui::use_remote(false);
        check(ui::tr("mods.sample.display.name", "Local") == "Local", "remote translations leaked outside admin");
        ui::state["catalogues"]["fr"]["test.format"] = "%n";
        ui::cache.clear();
        check(ui::format("test.format", "Value: %d", 7) == "Value: 7", "unsafe printf conversion");
        ui::state["catalogues"]["fr"]["test.format"] = "Texte: %s";
        ui::cache.clear();
        check(ui::format("test.format", "Value: %d", 7) == "Value: 7", "printf type mismatch");
        ui::state["catalogues"]["fr"]["test.format"] = "Valeur: %d";
        ui::cache.clear();
        check(ui::format("test.format", "Value: %d", 7) == "Valeur: 7", "valid translated format");
        auto frId =
            ui::label("ui.settings", "Settings").substr(ui::label("ui.settings", "Settings").find("###"));
        ui::state["language"] = "en";
        ui::cache.clear();
        check(ui::label("ui.settings", "Settings").ends_with(frId), "IDs change with language");

        namespace num = bc::menu::numbers;
        double v = .3;
        for (int i = 0; i < 3; ++i)
            v = num::decimal_value(v - .1);
        check(v == 0 && Json(v).dump() == "0.0", "decimal steps persisted floating point residual");
        check(num::decimal_value(2.7755575615628914e-17) == 0, "zero residue");
        check(num::decimal_value(.125) == .125 && num::decimal_value(.0006) == .001, "three-digit precision");
        check(num::display(2.7755575615628914e-17, "%.3f") == "0.000", "active residual shown");
        check(num::display(1.25, "%.3f") == "1.250", "active/editor precision mismatch");
        check(num::display(.00000012) == "1.2e-07", "balance small values rounded to zero");

        auto parent = std::filesystem::current_path();
        auto root = parent / ("localization-" + bc::admin::hex(bc::admin::random_bytes(8)));
        struct Cleanup {
            Path root, parent;
            ~Cleanup() {
                std::error_code e;
                if (root.parent_path() == parent && root.filename().string().starts_with("localization-"))
                    std::filesystem::remove_all(root, e);
            }
        } cleanup{root, parent};
        std::filesystem::create_directories(root / "Core" / "Localization");
        std::filesystem::copy(
            Path(__FILE__).parent_path().parent_path() / "resources" / "Localization", root / "Core" / "Localization",
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
        write(root / "Localization" / "Overrides" / "fr.json",
              {{"name", "Français"}, {"translations", {{"ui.settings", "Préférences"}}}});
        bc::Manifest localMod;
        localMod.id = "fixture";
        localMod.environment = "both";
        localMod.directory = root / "Mods" / "fixture";
        write(localMod.directory / "Translations" / "fr.json",
              {{"translations", {{"name", "Mod traduit"}, {"settings.multiplier", "Vitesse"}}}});
        write(root / "Translations" / "fr.json",
              {{"translations", {{"settings.ServerName", "Nom communautaire"}}}});
        auto remote = remote_catalogues(root, {localMod});
        validate_catalogues(remote, true);
        check(remote["fr"]["mods.fixture.name"] == "Mod traduit" &&
                  remote["fr"]["server.settings.ServerName"] == "Nom communautaire",
              "server/mod namespace prefixes");
        {
            Client client(root, {localMod});
            check(!std::filesystem::exists(root / "ui-settings.json"),
                  "locale bootstrap wrote before first menu");
            auto state = await(client);
            check(state["language"] == "fr" && state["catalogues"]["fr"]["ui.settings"] == "Préférences",
                  "override load");
            check(state["catalogues"]["fr"]["mods.fixture.settings.multiplier"] == "Vitesse",
                  "local mod translation");
            check(client.menu_key() == 0x70, "default menu shortcut");
            check(client.select("en"), "language selection rejected");
            state = await(client);
            check(state["language"] == "en", "language change not applied");
            check(!client.select("../en"), "unsafe language selected");
            check(client.select_menu_key(0x75), "menu shortcut rejected");
            state = await(client);
            check(state["language"] == "en" && state["menuKey"] == 0x75 && client.menu_key() == 0x75,
                  "key update lost language or failed to activate");
            check(client.select("fr"), "language update rejected after key change");
            state = await(client);
            check(state["language"] == "fr" && client.menu_key() == 0x75, "language update lost menu key");
            check(!client.select_menu_key(0) && !client.select_menu_key(0x1b) &&
                      !client.select_menu_key(0x12),
                  "invalid menu key accepted");
        }
        {
            Client restored(root, {localMod});
            check(restored.menu_key() == 0x75, "saved shortcut not restored before first ImGui frame");
            check(await(restored)["language"] == "fr", "language preference not restored");
            // Force a real atomic-write failure; effective binding must remain usable.
            std::filesystem::remove(root / "ui-settings.json");
            std::filesystem::create_directory(root / "ui-settings.json");
            check(restored.select_menu_key(0x76), "failed-save fixture could not queue key");
            auto result = await(restored);
            check(restored.menu_key() == 0x75 && result["errorKey"] == "ui.keybind.save_failed",
                  "Could not save the shortcut. Please try again.");
            std::filesystem::remove(root / "ui-settings.json");
        }
        write(root / "ui-settings.json", {{"language", "en"}, {"menuKey", 27}, {"keep", "value"}});
        {
            Client invalid(root, {});
            check(invalid.menu_key() == 0x70, "invalid stored shortcut stranded menu");
            check(await(invalid)["menuKey"] == 0x70, "invalid preference not normalized");
            auto prefs = Json::parse(bc::admin::read_file(root / "ui-settings.json"));
            check(prefs["keep"] == "value", "saving preferences removed unrelated data");
        }
        std::cout << "PASS " << checks << " localization and numeric precision checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
