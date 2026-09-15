#include "data/ServerSettingsFixtures.hpp"
#include "../runtime/Briefcase.Admin/Service.hpp"
#include "../runtime/Briefcase.NativeHost/Configuration.hpp"
#include <Windows.h>
#include <future>
#include <iostream>
using namespace bc::admin;
static unsigned checks;
static void check(bool ok, const char *what) {
    ++checks;
    if (!ok)
        throw std::runtime_error(what);
}
template <class F> static void rejected(F fn, const std::string &code) {
    try {
        fn();
    } catch (const Error &e) {
        check(e.code == code, "wrong rejection");
        return;
    }
    throw std::runtime_error("operation unexpectedly accepted");
}
int main() {
    try {
        const auto root = fs::current_path() / ("admin-fixture-" + hex(random_bytes(6)));
        fs::create_directories(root / "Briefcase");
        struct Cleanup {
            fs::path path;
            ~Cleanup() {
                std::error_code e;
                if (path.parent_path() == fs::current_path() &&
                    path.filename().string().starts_with("admin-fixture-"))
                    fs::remove_all(path, e);
            }
        } cleanup{root};
        const auto briefcase = root / "Briefcase";
        const std::string password = "Fixture-only-password-2026!";
        const auto started = Clock::now();
        auto settings = provision(briefcase, "127.0.0.1", 50002, "127.0.0.1:50002", password);
        check(settings.serialize().dump().find(password) == std::string::npos, "password leaked in settings");
        auto public_data = read_file(briefcase / "Admin/pairing.json");
        check(public_data.find("protectedKey") == std::string::npos &&
                  public_data.find("verifier") == std::string::npos,
              "pairing contains secrets");
        auto document = bc::strict_json(read_file(briefcase / "Admin/server.json"));
        check(document["version"] == 2 && document["password"] == password,
              "cleartext password not persisted");
        check(document["protectedKey"] == settings.identity.protected_key, "TLS key protection changed");
        check(public_data.find(password) == std::string::npos, "password leaked to public pairing");
        auto loaded = Settings::load(briefcase / "Admin/server.json");
        check(equal(derive(password, loaded.salt, loaded.iterations), loaded.verifier),
              "configured cleartext password does not authenticate");
        auto legacy = Settings::parse(settings.serialize());
        check(legacy.verifier == settings.verifier, "legacy verifier configuration no longer supported");
        auto changed_password = document;
        changed_password["password"] = "Different-local-password-2026";
        auto updated = Settings::parse(changed_password);
        check(equal(derive("Different-local-password-2026", updated.salt), updated.verifier) &&
                  !equal(derive(password, updated.salt), updated.verifier),
              "modified configuration retained previous password");
        check(updated.identity.protected_key == loaded.identity.protected_key &&
                  updated.identity.fingerprint == loaded.identity.fingerprint,
              "changing password changed TLS identity");
        for (const auto &bad : Json::array({"", "too-short", std::string(257, 'x'), 123, Json::object()})) {
            auto invalid = document;
            invalid["password"] = bad;
            bool refused = false;
            try {
                (void)Settings::parse(invalid);
            } catch (const Error &) {
                refused = true;
            }
            check(refused, "invalid configured password accepted");
        }
        auto malformed = briefcase / "Admin/malformed-fixture.json";
        write_file(malformed, std::string("{\"password\":\"") + password, false, true);
        try {
            (void)Settings::load(malformed);
            throw std::runtime_error("malformed password configuration accepted");
        } catch (const Error &e) {
            check(std::string(e.what()).find(password) == std::string::npos, "JSON error leaked password");
        }
        // The real TLS dispatcher below authenticates from the configuration loaded from disk.
        check(loaded.server_id == settings.server_id &&
              loaded.identity.protected_key == settings.identity.protected_key, "identity roundtrip");
        settings = loaded;
        rejected([&] { provision(briefcase, "127.0.0.1", 50002, "127.0.0.1:50002", password); },
                 "already_configured");
        auto configs = std::make_shared<ConfigStore>();
        check(ConfigStore::allowed("sample.registered"), "registered mod identifier accepted");
        for (auto id : {"", "../outside", "sample..bad", "Sample.upper", "sample/"})
            check(!ConfigStore::allowed(id), "invalid mod identifier rejected");
        rejected([&] { configs->read("sample.unregistered"); }, "mod_not_editable");
        const std::array<std::pair<const char *, std::string_view>, 3> definitions{
            {{"sample.settings-alpha", sample_alpha::schema},
             {"sample.settings-beta", sample_beta::schema},
             {"sample.settings-gamma", sample_gamma::schema}}};
        for (const auto &[id, text] : definitions) {
            auto path = briefcase / "Mods" / id / "Data";
            fs::create_directories(path);
            auto schema = bc::strict_json(text), active = bc::normalize_config(schema, Json::object());
            write_file(path / "config.json", active.dump(), false);
            configs->add(id, path / "config.json", schema, active);
            check(configs->read(id).at("active") == active, "active config registration");
        }
        rejected([&] { configs->read("../../Admin/server"); }, "mod_not_editable");
        auto before = configs->read("sample.settings-beta");
        auto values = before["saved"];
        values["delayMilliseconds"] = 30;
        Json edit{{"modId", "sample.settings-beta"},
                  {"expectedRevision", before["revision"]},
                  {"writeId", hex(random_bytes(16))},
                  {"values", values}};
        auto changed = configs->write(edit);
        check(changed["saved"]["delayMilliseconds"] == 30 && changed["active"]["delayMilliseconds"] == 90 &&
                  changed["restartRequired"] == true,
              "saved and active settings confused");
        check(configs->write(edit) == changed, "idempotent retry failed");
        auto stale = edit;
        stale["writeId"] = hex(random_bytes(16));
        rejected([&] { configs->write(stale); }, "conflict");
        auto reused = edit;
        reused["values"]["delayMilliseconds"] = 31;
        rejected([&] { configs->write(reused); }, "write_id_reused");
        auto invalid = edit;
        invalid["expectedRevision"] = changed["revision"];
        invalid["writeId"] = hex(random_bytes(16));
        invalid["values"]["delayMilliseconds"] = 0;
        rejected([&] { configs->write(invalid); }, "invalid_values");
        check(configs->read("sample.settings-beta") == changed, "rejected values changed file");
        for (auto bad : Json::array({13, -1, 1.5, "12"})) {
            auto cap = configs->read("sample.settings-alpha");
            Json patch{{"modId", "sample.settings-alpha"},
                       {"expectedRevision", cap["revision"]},
                       {"writeId", hex(random_bytes(16))},
                       {"values", cap["saved"]}};
            patch["values"]["itemLimit"] = bad;
            rejected([&] { configs->write(patch); }, "invalid_values");
        }
        // Atomic replacement failure and hard-link checks must preserve the original settings.
        const auto cap_path = briefcase / "Mods" / "sample.settings-alpha" / "Data" / "config.json";
        auto cap_before = configs->read("sample.settings-alpha");
        Json denied{{"modId", "sample.settings-alpha"},
                    {"expectedRevision", cap_before["revision"]},
                    {"writeId", hex(random_bytes(16))},
                    {"values", cap_before["saved"]}};
        denied["values"]["itemLimit"] = 11;
        HANDLE held =
            CreateFileW(cap_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        check(held != INVALID_HANDLE_VALUE, "lock configuration file");
        try {
            rejected([&] { configs->write(denied); }, "write_failed");
        } catch (...) {
            CloseHandle(held);
            throw;
        }
        CloseHandle(held);
        check(configs->read("sample.settings-alpha") == cap_before,
              "failed replacement changed configuration");
        auto link = briefcase / "cap-hardlink.json";
        check(CreateHardLinkW(link.c_str(), cap_path.c_str(), nullptr), "create fixture hard link");
        rejected([&] { configs->read("sample.settings-alpha"); }, "unsafe_path");
        fs::remove(link);
        // Two administrators editing the same revision: exactly one may commit.
        auto concurrent = edit;
        concurrent["expectedRevision"] = changed["revision"];
        concurrent["values"]["delayMilliseconds"] = 32;
        auto a = concurrent, b = concurrent;
        a["writeId"] = hex(random_bytes(16));
        b["writeId"] = hex(random_bytes(16));
        b["values"]["delayMilliseconds"] = 33;
        auto save = [&](Json p) {
            try {
                configs->write(p);
                return true;
            } catch (const Error &e) {
                if (e.code != "conflict")
                    throw;
                return false;
            }
        };
        auto first = std::async(std::launch::async, save, a),
             second = std::async(std::launch::async, save, b);
        check(first.get() != second.get(), "concurrent write did not conflict");
        // Real encrypted server with production password verifier and production dispatcher.
        settings.port = 0;
        std::atomic<unsigned> status_reads{};
        std::atomic<bool> stopping{};
        Server server(
            settings, configs,
            [&] {
                ++status_reads;
                return Json{{"framework", "fixture"}, {"ready", true}};
            },
            [] { return Json::array({Json{{"id", "sample.settings-alpha"}}}); });
        server.start();
        const auto endpoint = "127.0.0.1:" + std::to_string(server.port());
        Credentials credentials;
        {
            auto unauth = Stream::connect(credentials, endpoint, settings.identity.fingerprint, stopping);
            auto id = hex(random_bytes(16));
            unauth.send(Json{{"version", 1},
                             {"requestId", id},
                             {"operation", "server.status"},
                             {"payload", Json::object()}}
                            .dump());
            auto out = bc::strict_json(unauth.receive());
            check(out["ok"] == false && out["error"]["code"] == "unauthorized" && status_reads == 0,
                  "unauthenticated data disclosure");
            unauth.send(
                Json{{"version", 2}, {"requestId", id}, {"operation", "hello"}, {"payload", Json::object()}}
                    .dump());
            check(bc::strict_json(unauth.receive())["error"]["code"] == "version_mismatch",
                  "protocol version not checked");
        }
        auto wrong = hex(random_bytes(32));
        bool pin_rejected = false;
        try {
            (void)Stream::connect(credentials, endpoint, wrong, stopping);
        } catch (...) {
            pin_rejected = true;
        }
        check(pin_rejected && status_reads == 0, "wrong server identity accepted");
        Connection client;
        std::string wrong_password = "This-password-is-wrong";
        rejected([&] { client.connect(endpoint, settings.identity.fingerprint, wrong_password, stopping); },
                 "unauthorized");
        check(wrong_password.empty(), "failed password retained");
        std::string input = password;
        const auto login_started = Clock::now();
        client.connect(endpoint, settings.identity.fingerprint, input, stopping);
        const auto login_ms = std::chrono::duration<double, std::milli>(Clock::now() - login_started).count();
        check(input.empty() && client.server_id == settings.server_id,
              "authenticated identity/password wipe");
        check(client.request("server.status")["ready"] == true && status_reads == 1,
              "status unavailable after authentication");
        const auto requests_started = Clock::now();
        for (int i = 0; i < 10; ++i)
            (void)client.request("server.status");
        const auto status_us =
            std::chrono::duration<double, std::micro>(Clock::now() - requests_started).count() / 10;
        check(client.request("mods.list").size() == 1, "mod inventory unavailable");
        auto remote = client.request("mod.config.read", {{"modId", "sample.settings-gamma"}});
        auto patch = Json{{"modId", "sample.settings-gamma"},
                          {"expectedRevision", remote["revision"]},
                          {"writeId", hex(random_bytes(16))},
                          {"values", remote["saved"]}};
        patch["values"]["multiplier"] = 0.1;
        auto saved = client.request("mod.config.write", patch);
        check(saved["saved"]["multiplier"] == 0.1 && saved["restartRequired"] == true,
              "remote sample config did not persist");
        rejected([&] { client.request("server.stop"); }, "unknown_operation");
        check(client.request("logout")["loggedOut"] == true, "logout rejected");
        // Admission limits count all attempts, independent of TCP connection.
        for (int i = 0; i < 2; ++i) {
            Connection c;
            std::string pass = "This-password-is-wrong";
            rejected([&] { c.connect(endpoint, settings.identity.fingerprint, pass, stopping); },
                     "unauthorized");
        }
        Connection limited;
        input = password;
        rejected([&] { limited.connect(endpoint, settings.identity.fingerprint, input, stopping); },
                 "rate_limited");
        const auto stop_started = Clock::now();
        server.stop();
        check(Clock::now() - stop_started < std::chrono::seconds(2), "shutdown did not cancel pending reads");
        std::cout << "PASS " << checks << " administration contracts; TLS=" << client.protocol << "; login "
                  << login_ms << " ms; status roundtrip " << status_us << " us; total "
                  << std::chrono::duration<double, std::milli>(Clock::now() - started).count() << " ms\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
