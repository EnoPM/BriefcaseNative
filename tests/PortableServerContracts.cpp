#include "Configuration.hpp"
#include "GameProfile.hpp"
#include "Manifest.hpp"
#include "Startup.hpp"
#include "../runtime/Briefcase.Localization/Catalog.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
using Json = nlohmann::json;
static unsigned checks;
static void check(bool value) {
    if (!value) throw std::runtime_error("Portable server contract " + std::to_string(checks + 1));
    ++checks;
}
template<class F> static void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception &) { rejected = true; }
    check(rejected);
}
int main() {
    auto directory = fs::temp_directory_path() / ("briefcase-portable-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(fs::create_directory(directory));
        const auto file = directory / "settings.json";
        { std::ofstream out(file); out << "{}"; }
        check(bc::read_bounded(file, 2) == "{}");
        rejects([&] { bc::read_bounded(file, 1); });
        rejects([&] { bc::read_bounded(directory / "missing", 1024); });
#ifndef _WIN32
        fs::create_directory_symlink(directory, directory / "link");
        rejects([&] { bc::read_bounded(directory / "link/settings.json", 1024); });
        fs::create_symlink(file, directory / "file-link");
        rejects([&] { bc::read_bounded(directory / "file-link", 1024); });
#endif
        const auto schema = bc::strict_json(R"({"type":"object","properties":{
          "enabled":{"type":"boolean","default":false,"description":"Enabled"},
          "limit":{"type":"integer","default":8,"minimum":1,"maximum":16,"description":"Limit"}
        }})");
        check(bc::normalize_config(schema, Json::object()) == Json({{"enabled", false}, {"limit", 8}}));
        check(bc::normalize_config(schema, {{"limit", 12}})["limit"] == 12);
        rejects([&] { bc::normalize_config(schema, {{"limit", 17}}); });
        rejects([&] { bc::normalize_config(schema, {{"limit", "12"}}); });
        rejects([&] { bc::normalize_config(schema, {{"unknown", true}}); });
        rejects([] { bc::strict_json("{\"x\":1,\"x\":2}"); });
        for (const auto &nl : {std::string("\n"), std::string("\r\n")}) {
            const auto ini = "[Server]" + nl + "Limit=8" + nl;
            const auto changed = bc::ini_write(ini, "Server", "Limit", "12");
            check(changed == "[Server]" + nl + "Limit=12" + nl);
            check(bc::ini_read(changed, "Server", "Limit") == "12");
        }
        rejects([] { bc::ini_write("[Server]\nLimit=8", "Server", "Limit", "1\nOther=2"); });
        Json manifest = {{"schemaVersion", 1}, {"id", "sample.portable"}, {"name", "Sample"},
            {"author", "Test"}, {"version", "1.0.0"}, {"entry", "Sample.so"},
            {"environment", "server"}, {"minimumApi", 1}, {"capabilities", {"log"}},
            {"dependencies", Json::array()}};
        check(bc::parse_manifest(manifest.dump()).entry == "Sample.so");
        for (const auto &entry : {"../Sample.so", "/tmp/Sample.so", "sub/Sample.so", "sub\\Sample.so", "Sample.so.1", "Sample.exe"}) {
            manifest["entry"] = entry;
            rejects([&] { bc::parse_manifest(manifest.dump()); });
        }
        check(bc::matches_environment("server", bc::Environment::server));
        check(bc::matches_environment("both", bc::Environment::server));
        check(!bc::matches_environment("client", bc::Environment::server));
        bc::locale::validate_catalogues({{"en", {{"server.limit", "Limit"}}}}, true);
        rejects([] { bc::locale::validate_catalogues({{"en", {{"framework.title", "Override"}}}}, true); });
        fs::remove_all(directory);
        std::cout << "PASS " << checks << " portable server contracts\n";
    } catch (const std::exception &error) {
        std::error_code ignored;
        fs::remove_all(directory, ignored);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
