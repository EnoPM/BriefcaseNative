#include "data/ServerSettingsFixtures.hpp"
#include "../runtime/Briefcase.Admin/Management.hpp"
#include "../runtime/Briefcase.Client.Admin/Client.hpp"
#include "../runtime/Briefcase.Client.Admin/Credentials.hpp"
#include "../runtime/Briefcase.NativeHost/Configuration.hpp"
#include <future>
#include <iostream>
using namespace bc::admin;
static unsigned checks;
static void check(bool ok, const char *what) {
    ++checks;
    if (!ok)
        throw std::runtime_error(what);
}
static Json await(Client &client, const std::string &wanted) {
    uint64_t revision = 0;
    std::string buffer;
    Json state;
    const auto end = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < end) {
        if (client.snapshot(buffer, revision))
            state = bc::strict_json(buffer, 1048576);
        if (state.value("state", "") == wanted && !state.value("pending", true))
            return state;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Client state timeout: " + state.dump());
}
int main() {
    try {
        const auto parent = fs::current_path(), root = parent / ("admin-client-" + hex(random_bytes(8)));
        fs::create_directories(root / "server");
        fs::create_directories(root / "client");
        struct Cleanup {
            fs::path root, parent;
            ~Cleanup() {
                std::error_code e;
                if (root.parent_path() == parent && root.filename().string().starts_with("admin-client-"))
                    fs::remove_all(root, e);
            }
        } cleanup{root, parent};
        const std::string password = "Client-fixture-password-2026!";
        auto settings = provision(root / "server", "127.0.0.1", 32189, "127.0.0.1:32189", password);
        settings.port = 0;
        auto store = std::make_shared<ConfigStore>();
        auto schema = bc::strict_json(sample_alpha::schema),
             values = bc::normalize_config(schema, Json::object());
        auto path = root / "server" / "cap.json";
        write_file(path, values.dump());
        store->add("sample.settings-alpha", path, schema, values);
        std::atomic<bool> malformed{}, bad_bounds{};
        Server server(
            settings, store,
            [] {
                return Json{{"framework", "fixture"}, {"gameBuild", "test"}, {"unreal", "4.27"},
                            {"backendState", 2},      {"uptimeSeconds", 10}, {"loadedMods", 1},
                            {"discoveredMods", 1}};
            },
            [&] {
                if (malformed)
                    return Json::array({Json{{"id", 1}}});
                return Json::array({Json{{"id", "sample.settings-alpha"},
                                         {"name", "Sample Alpha"},
                                         {"version", "0.1.0"},
                                         {"author", "Briefcase"},
                                         {"error", ""},
                                         {"state", 1},
                                         {"editable", true},
                                         {"dependencies", Json::array()}}});
            },
            {},
            [&](const std::string &op, const Json &) {
                if (op != "server.config.read")
                    throw Error("unavailable", "Unavailable in fixture.");
                auto schema = Management::config_schema();
                schema["properties"]["ServerName"]["description"] = "Nom du serveur";
                auto values = bc::normalize_config(schema, Json::object());
                if (bad_bounds)
                    schema["properties"]["GamePort"]["maximum"] = 65534;
                return Json{{"schema", schema},
                            {"saved", values},
                            {"active", values},
                            {"restartRequired", false},
                            {"revision", std::string(64, 'a')}};
            });
        server.start();
        const auto endpoint = "127.0.0.1:" + std::to_string(server.port());
        Client client(root / "client", std::chrono::milliseconds(100));
        check(!fs::exists(root / "client" / "Admin"), "idle client wrote to disk");
        check(client.select(7, "Fixture", "127.0.0.1:50000"), "select rejected");
        auto state = await(client, "idle");
        check(state["favoriteId"] == 7 && state["endpoint"] == "127.0.0.1:32189", "selection snapshot");
        auto start = Clock::now();
        check(client.connect(7, "Fixture", "127.0.0.1:50000", endpoint, settings.identity.fingerprint,
                             password, true),
              "connect enqueue rejected");
        auto enqueue = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
        check(!client.submit("refresh"), "operation accepted while connecting");
        state = await(client, "ready");
        check(state["configs"]["sample.settings-alpha"]["active"]["itemLimit"] == 12,
              "remote config not exposed");
        check(state["messageKey"] == "ui.message.refreshed" && state["message"] == "Data refreshed.",
              "dynamic message key or English fallback");
        check(client.submit("server.config.read"), "legacy description request");
        state = await(client, "ready");
        check(state["serverConfig"]["schema"]["properties"]["ServerName"]["description"] == "Nom du serveur",
              "localized description broke existing server compatibility");
        auto pairing = read_file(root / "client" / "Admin" / "clients.json");
        check(pairing.find(password) == std::string::npos && pairing.find("password") == std::string::npos,
              "client stored password");
        check(state.value("passwordSaved", false), "remember preference not published");
        check(state.dump().find(password) == std::string::npos, "password exposed in UI snapshot");
        check(read_file(root / "client" / "Admin" / "passwords.json", 262144).find(password) ==
                  std::string::npos,
              "cleartext remembered password");
        uint64_t revision = 0;
        std::string snapshot;
        check(client.snapshot(snapshot, revision), "snapshot not delivered");
        check(!client.snapshot(snapshot, revision), "unchanged snapshot copied again");
        auto cfg = state["configs"]["sample.settings-alpha"];
        auto changed = cfg["saved"];
        changed["itemLimit"] = 9;
        check(client.submit("save", {{"modId", "sample.settings-alpha"},
                                     {"expectedRevision", cfg["revision"]},
                                     {"values", changed}}),
              "save enqueue");
        state = await(client, "ready");
        check(state["configs"]["sample.settings-alpha"]["saved"]["itemLimit"] == 9 &&
                  state["configs"]["sample.settings-alpha"]["active"]["itemLimit"] == 12,
              "async saved/active values");
        check(client.submit("save", {{"modId", "sample.settings-alpha"},
                                     {"expectedRevision", cfg["revision"]},
                                     {"values", changed}}),
              "stale write enqueue");
        state = await(client, "ready");
        check(state["errorCode"] == "conflict", "conflict did not preserve session");
        malformed = true;
        check(client.submit("refresh"), "malformed refresh enqueue");
        state = await(client, "error");
        check(!state.contains("mods") && !state.contains("configs"), "malformed server data published");
        client.disconnect();
        state = await(client, "idle");
        check(client.select(8, "Second", "127.0.0.1:50001"), "different server select");
        state = await(client, "idle");
        check(state["fingerprint"] == "", "different server inherited trusted fingerprint");
        client.stop();
        malformed = false;
        Client restored(root / "client", std::chrono::milliseconds(100));
        check(restored.select(7, "Fixture", "127.0.0.1:50000"), "restored select");
        auto reconnected = await(restored, "ready");
        check(reconnected["fingerprint"] == settings.identity.fingerprint &&
                  reconnected["endpoint"] == endpoint,
              "saved pairing was not restored");
        check(reconnected.value("passwordSaved", false),
              "saved selection did not authenticate automatically");
        auto versionBefore = reconnected["serverId"];
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        check(restored.submit("refresh"), "session alive after heartbeats");
        reconnected = await(restored, "ready");
        check(reconnected["serverId"] == versionBefore, "heartbeat lost session");
        restored.disconnect();
        await(restored, "idle");
        PasswordStore savedPasswords(root / "client");
        savedPasswords.put("127.0.0.1:50000", endpoint, settings.identity.fingerprint,
                           "Deliberately-invalid-password");
        check(restored.select(7, "Fixture", "127.0.0.1:50000"), "invalid saved password selection");
        auto rejected = await(restored, "error");
        check(rejected.value("errorCode", "") == "unauthorized" && !rejected.contains("configs"),
              "bad saved password must return to login");
        check(restored.connect(7, "Fixture", "127.0.0.1:50000", endpoint, settings.identity.fingerprint,
                               password, true),
              "manual recovery enqueue");
        await(restored, "ready");
        check(restored.submit("forget"), "forget enqueue");
        reconnected = await(restored, "ready");
        check(!reconnected.value("passwordSaved", true), "forget not reflected in UI");
        bad_bounds=true;
        check(restored.submit("server.config.read"),"invalid bound request");
        reconnected=await(restored,"error");
        check(reconnected["errorCode"]=="protocol_error","description compatibility weakened bound validation");
        const auto stop = Clock::now();
        restored.stop();
        check(Clock::now() - stop < std::chrono::seconds(2), "pending client shutdown blocked");
        server.stop();
        std::cout << "PASS " << checks << " async admin client checks; connect enqueue " << enqueue
                  << " us\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
