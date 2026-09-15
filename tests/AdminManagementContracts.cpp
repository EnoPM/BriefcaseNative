#include "../runtime/Briefcase.Admin/Management.hpp"
#ifdef _WIN32
#include "../runtime/Briefcase.Client.Admin/Credentials.hpp"
#endif
#include "../runtime/Briefcase.NativeHost/Configuration.hpp"

#include <iostream>
using namespace bc::admin;
static unsigned checks;
void check(bool ok, const char *what) {
    ++checks;
    if (!ok)
        throw std::runtime_error(what);
}
template <class F> void rejects(F f, const std::string &code = "invalid_values") {
    try {
        f();
    } catch (const Error &e) {
        check(e.code == code, "rejection code");
        return;
    }
    throw std::runtime_error("Accepted invalid operation");
}
int main() {
    try {
#ifdef _WIN32
        const auto fixture_base = fs::current_path();
#else
        const auto fixture_base = fs::temp_directory_path();
#endif
        auto root = fixture_base / ("management-fixture-" + hex(random_bytes(8)));
        struct Cleanup {
            fs::path p, base;
            ~Cleanup() {
                std::error_code e;
                if (p.parent_path() == base &&
                    p.filename().string().starts_with("management-fixture-"))
                    fs::remove_all(p, e);
            }
        } cleanup{root, fixture_base};
#ifdef _WIN32
        const auto binaries = "Win64", config = "WindowsServer";
#else
        const auto binaries = "Linux", config = "LinuxServer";
#endif
        auto game = root / "DeceiveInc", briefcase = game / "Binaries" / binaries / "Briefcase",
             ini = game / "Saved" / "Config" / config / "TripwireServer.ini";
        fs::create_directories(ini.parent_path());
        fs::create_directories(briefcase / "Admin");
        fs::create_directories(game / "Community Balance Template");
        const std::string original =
            "; keep this "
            "comment\r\n[/Script/"
            "DeceiveInc.TripwireServerSettings]\r\nServerName=Fixture\r\nGamePort=50000\r\nQueryPort="
            "50001\r\nAdminPassword=LOCAL-ONLY-SECRET\r\nUnknownSetting=retain\r\n!MapRotation="
            "ClearArray\r\n+MapRotation=DI_SR\r\n+MapRotation=DI_DS\r\n[Other]\r\nValue=keep\r\n";
        write_file(ini, original);
        auto a = bc::Manifest{};
        a.id = "a";
        a.name = "A";
        a.environment = "server";
        a.version = {1, 0, 0};
        auto b = a;
        b.id = "b";
        b.dependencies.push_back({"a", {1, 0, 0}});
        auto c = a;
        c.id = "c";
        c.environment = "client";
        write_file(briefcase / "settings.json",
                   Json{{"schemaVersion", 1}, {"enabledMods", Json::array({"a", "b"})}, {"keep", "metadata"}}
                       .dump());
        Json entries = Json::array({{{"table", "DT_Balancing_HitscanWeapons"},
                                     {"row", "Ace_Weapon_Base"},
                                     {"field", "Damage"},
                                     {"value", 10},
                                     {"allowedRange", "0 to 20"},
                                     {"info", "Damage"}},
                                    {{"table", "DT_Balancing_Projectiles"},
                                     {"row", "Yumi_Projectile_Base"},
                                     {"field", "Speed"},
                                     {"value", 500},
                                     {"allowedRange", "0 to 1000"}},
                                    {{"table", "DT_Projectiles_Balancing"},
                                     {"row", "Yumi_Projectile_Base"},
                                     {"field", "Radius"},
                                     {"value", 2.5},
                                     {"allowedRange", "0 to 10"}},
                                    {{"table", "DT_Chavez_ActivesBalancing"},
                                     {"row", "Chavez_Active_Base"},
                                     {"field", "Enabled"},
                                     {"value", true}},
                                    {{"table", "DT_SpyShared_HealthPool"},
                                     {"row", "HealthPool_Normal"},
                                     {"field", "Health"},
                                     {"value", 100},
                                     {"allowedRange", "0 to 200"}}});
        auto defaults =
            Json{{"format", "DeceiveCommunityBalanceProfile"}, {"schemaVersion", 1}, {"overrides", entries}};
        write_file(game / "Community Balance Template" / "CommunityBalanceProfile.default.json",
                   defaults.dump());
        auto profile = defaults;
        profile["note"] = "preserve metadata";
        profile["overrides"].erase(2);
        write_file(game / "CommunityBalanceProfile.json", profile.dump());
        a.directory = briefcase / "Mods" / "a";
        fs::create_directories(a.directory / "Translations");
        fs::create_directories(briefcase / "Translations");
        write_file(a.directory / "Translations" / "fr.json",
                   Json{{"translations", {{"name", "Mod A traduit"}}}}.dump());
        write_file(briefcase / "Translations" / "fr.json",
                   Json{{"translations", {{"settings.ServerName", "Nom public"}}}}.dump());
        write_file(briefcase / "Admin" / "presentation.json",
                   Json{{"serverConfig",
                         {{"ServerName", {{"displayName", "Nom choisi"}, {"category", "identity"}}}}},
                        {"categories", {{"identity", {{"displayName", "Identité du serveur"}}}}},
                        {"balance",
                         {{"groups", {{"Ace", {{"displayName", "Agent Ace"}}}}},
                          {"fields", {{"Damage", {{"displayName", "Dégâts"}}}}}}}}
                       .dump());
        Management m(briefcase, {a, b, c});
        auto translations = m.dispatch("translations.read", Json::object());
        check(translations["fr"]["mods.a.name"] == "Mod A traduit" &&
                  translations["fr"]["server.settings.ServerName"] == "Nom public",
              "authenticated translation extension scopes keys");
        auto metadata =
            m.dispatch("server.config.read", Json::object())["schema"]["properties"]["ServerName"];
        check(metadata["displayName"] == "Nom choisi" && metadata["categoryLabel"] == "Identité du serveur",
              "server presentation applied");
        auto namedBalance = m.dispatch("balance.read", {{"group", "Ace"}});
        check(namedBalance["groupLabels"]["Ace"]["displayName"] == "Agent Ace" &&
                  namedBalance["entries"][0]["presentation"]["displayName"] == "Dégâts",
              "balance labels applied");

        auto cfg = m.dispatch("server.config.read", {});
        check(!cfg["saved"].contains("AdminPassword") &&
                  cfg.dump().find("LOCAL-ONLY-SECRET") == std::string::npos,
              "admin secret exposed");
        check(cfg["saved"]["MapRotation"] == Json::array({"DI_SR", "DI_DS"}), "INI array parse");
        auto values = cfg["saved"];
        values["ServerName"] = "Changed";
        values["bCrossplay"] = false;
        values["GamePort"] = 50010;
        values["MapRotation"] = Json::array({"DI_DS", "DI_SR"});
        auto next =
            m.dispatch("server.config.write", {{"expectedRevision", cfg["revision"]}, {"values", values}});
        check(next["restartRequired"] && next["active"]["ServerName"] == "Fixture" &&
                  next["saved"]["ServerName"] == "Changed",
              "saved vs startup config");
        auto after = read_file(ini);
        check(after.find("AdminPassword=LOCAL-ONLY-SECRET") != after.npos &&
                  after.find("; keep this comment") != after.npos &&
                  after.find("UnknownSetting=retain") != after.npos &&
                  after.find("[Other]\r\nValue=keep") != after.npos,
              "unmanaged INI preserved");
        check(read_file(briefcase / "Admin" / "server-configuration.previous.ini") == original, "INI backup");
        rejects(
            [&] {
                m.dispatch("server.config.write",
                           {{"expectedRevision", cfg["revision"]}, {"values", values}});
            },
            "conflict");
        auto bad = values;
        bad["GamePort"] = bad["QueryPort"];
        rejects([&] {
            m.dispatch("server.config.write", {{"expectedRevision", next["revision"]}, {"values", bad}});
        });
        bad = values;
        bad["ServerName"] = "name\nAdminPassword=evil";
        rejects([&] {
            m.dispatch("server.config.write", {{"expectedRevision", next["revision"]}, {"values", bad}});
        });
        bad = values;
        bad["MapRotation"] = Json::array({"unknown"});
        rejects([&] {
            m.dispatch("server.config.write", {{"expectedRevision", next["revision"]}, {"values", bad}});
        });
        check(read_file(ini) == after, "invalid configuration changed disk");
        auto selection = m.dispatch("mods.selection.read", {});
        rejects([&] {
            m.dispatch("mods.selection.write",
                       {{"expectedRevision", selection["revision"]}, {"enabledMods", Json::array({"b"})}});
        });
        rejects([&] {
            m.dispatch("mods.selection.write",
                       {{"expectedRevision", selection["revision"]}, {"enabledMods", Json::array({"c"})}});
        });
        auto selected = m.dispatch("mods.selection.write", {{"expectedRevision", selection["revision"]},
                                                            {"enabledMods", Json::array({"a"})}});
        check(selected["restartRequired"] && selected["active"].size() == 2 && selected["saved"].size() == 1,
              "mod selection staging");
        check(bc::strict_json(read_file(briefcase / "settings.json"))["keep"] == "metadata",
              "loader metadata preserved");
        auto balance = m.dispatch("balance.read", {{"group", "Yumi"}});
        check(balance["entries"].size() == 2 && balance["groups"]["Ace"] == 1 &&
                  balance["groups"]["Commun"] == 1,
              "group characters across projectile tables");
        check(balance["groupLabels"]["Commun"]["displayNameKey"] == "server.balance.groups.Shared" &&
                  balance["groupLabels"]["Commun"]["displayName"] == "Shared",
              "legacy group ID leaked into translation key/fallback");
        check(Management::config_schema()["properties"]["MapRotation"]["displayName"] == "Map rotation",
              "server setting fallback is not English");
        auto x = balance["entries"][0];
        auto id = x["id"];
        Json changes = Json::array({{{"id", id}, {"value", 5}}});
        auto stored =
            m.dispatch("balance.write",
                       {{"group", "Yumi"}, {"expectedRevision", balance["revision"]}, {"changes", changes}});
        check(stored["restartRequired"], "balance staged");
        auto written = bc::strict_json(read_file(game / "CommunityBalanceProfile.json", 1048576), 1048576);
        check(written["note"] == "preserve metadata" && written["format"] == profile["format"],
              "profile metadata retained");
        rejects(
            [&] {
                m.dispatch(
                    "balance.write",
                    {{"group", "Yumi"}, {"expectedRevision", balance["revision"]}, {"changes", changes}});
            },
            "conflict");
        changes[0]["value"] = 100000001;
        rejects([&] {
            m.dispatch("balance.write",
                       {{"group", "Yumi"}, {"expectedRevision", stored["revision"]}, {"changes", changes}});
        });
        auto ace = m.dispatch("balance.read", {{"group", "Ace"}});
        changes = Json::array({{{"id", ace["entries"][0]["id"]}, {"value", 21}}});
        rejects([&] {
            m.dispatch("balance.write",
                       {{"group", "Ace"}, {"expectedRevision", ace["revision"]}, {"changes", changes}});
        });
        changes[0]["value"] = 15;
        rejects([&] {
            m.dispatch("balance.write",
                       {{"group", "Yumi"}, {"expectedRevision", ace["revision"]}, {"changes", changes}});
        });
        check(bc::strict_json(read_file(game / "CommunityBalanceProfile.json", 1048576), 1048576) == written,
              "bad balance requests modified profile");
        auto chavez = m.dispatch("balance.read", {{"group", "Chavez"}});
        changes = Json::array({{{"id", chavez["entries"][0]["id"]}, {"value", false}}});
        auto boolsaved =
            m.dispatch("balance.write",
                       {{"group", "Chavez"}, {"expectedRevision", chavez["revision"]}, {"changes", changes}});
        check(boolsaved["entries"][0]["saved"] == false && boolsaved["entries"][0]["active"] == true,
              "boolean balancing");
        rejects([&] { m.dispatch("server.logs", {{"source", "../../Admin/server.json"}}); });
        fs::create_directories(briefcase / "Logs");
        write_file(briefcase / "Logs" / "BriefcaseNative.log",
                   std::string(50000, 'x') +
                       "\nPassword=secret\nhealthy line\nAuthorization: Bearer secret\n");
        auto logs = m.dispatch("server.logs", {{"source", "framework"}});
        check(logs["truncated"] && logs["text"].get<std::string>().find("secret") == std::string::npos &&
                  logs["text"].get<std::string>().find("healthy line") != std::string::npos,
              "bounded redacted log tail");
        rejects([&] { m.dispatch("server.restart", {{"command", "calc"}}); });
        rejects([&] { m.dispatch("server.restart", {}); },
                "unavailable"); // A fixture cannot restart any Shipping process.
#ifdef _WIN32
        PasswordStore vault(briefcase);
        const std::string password = "Synthetic-client-password-2026";
        vault.put("game:1", "admin:2", std::string(64, 'a'), password);
        check(vault.contains("game:1", "admin:2", std::string(64, 'a')), "saved password absent");
        check(vault.get("game:1", "admin:2", std::string(64, 'a')) == password, "saved password restore");
        check(read_file(briefcase / "Admin" / "passwords.json", 262144).find(password) == std::string::npos,
              "cleartext client password on disk");
        rejects([&] { vault.get("game:1", "admin:3", std::string(64, 'a')); }, "credentials");
        rejects([&] { vault.get("game:1", "admin:2", std::string(64, 'b')); }, "credentials");
        rejects([&] { vault.get("other:1", "admin:2", std::string(64, 'a')); }, "credentials");
        auto tampered = bc::strict_json(read_file(briefcase / "Admin" / "passwords.json"));
        tampered["game:1"]["fingerprint"] = std::string(64, 'b');
        write_file(briefcase / "Admin" / "passwords.json", tampered.dump(), true, true);
        rejects([&] { vault.get("game:1", "admin:2", std::string(64, 'b')); },
                "credentials"); // DPAPI entropy binds the actual identity, not only the JSON metadata.
        vault.forget("game:1");
        check(!vault.contains("game:1", "admin:2", std::string(64, 'b')), "forget failed");
#endif
        std::cout << "PASS " << checks << " administration management checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
