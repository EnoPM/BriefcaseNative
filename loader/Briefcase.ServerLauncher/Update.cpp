#include "Update.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <regex>
#include <set>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace bc::launcher {
void remove_file(const fs::path& path);
namespace {
bool matches(const std::string& value, const char* expression) { return std::regex_match(value, std::regex(expression)); }
std::string legacy_name(std::string value) {
#ifdef _WIN32
    std::ranges::replace(value, '\\', '/');
#endif
    return value;
}
bool mod_id(const std::string& value) { return matches(value, "[a-z0-9]+([.-][a-z0-9]+)*"); }
std::array<uint64_t, 3> version(const std::string& value) {
    std::smatch match;
    require(std::regex_match(value, match, std::regex("v?(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)")), "Invalid stable version");
    return {std::stoull(match[1]), std::stoull(match[2]), std::stoull(match[3])};
}
bool mode_ok(unsigned value) { return value == 0644 || value == 0755 || value == 0600; }
uint64_t number(const Json& value) {
    require(value.is_number_unsigned() || (value.is_number_integer() && value.get<int64_t>() >= 0), "Expected unsigned integer");
    return value.get<uint64_t>();
}
#ifdef _WIN32
const std::set<std::string> required{
    "Briefcase.ServerLauncher.exe", "Briefcase/Core/Briefcase.NativeHost.dll",
    "Briefcase/Core/Briefcase.ServerBootstrap.dll", "Briefcase/Core/Tools/Briefcase.AdminSetup.exe",
    "Briefcase/Core/Tools/Briefcase.ServerRestart.exe", "Briefcase/Core/Tools/Briefcase.ServerUpdater.exe",
    "Briefcase/Core/Updater/build.json"};
#else
const std::set<std::string> required{
    "Briefcase.ServerLauncher", "Briefcase/Core/libBriefcase.NativeHost.so", "Briefcase/Core/libBriefcase.ServerBootstrap.so",
    "Briefcase/Core/Tools/Briefcase.AdminSetup", "Briefcase/Core/Updater/build.json"};
#endif
bool native_binary(const std::string& name) {
#ifdef _WIN32
    return name == "Briefcase.ServerLauncher.exe" || name.ends_with(".dll") ||
           name == "Briefcase/Core/Tools/Briefcase.AdminSetup.exe" ||
           name == "Briefcase/Core/Tools/Briefcase.ServerRestart.exe" ||
           name == "Briefcase/Core/Tools/Briefcase.ServerUpdater.exe";
#else
    return name == "Briefcase.ServerLauncher" || name.ends_with(".so") ||
           name == "Briefcase/Core/Tools/Briefcase.AdminSetup";
#endif
}
void validate_binary(const std::string& data) {
#ifdef _WIN32
    require(data.size() >= 0x40 && data[0] == 'M' && data[1] == 'Z', "Expected Windows x64 PE binary");
    const auto offset = static_cast<uint32_t>(static_cast<unsigned char>(data[0x3c])) |
                        (static_cast<uint32_t>(static_cast<unsigned char>(data[0x3d])) << 8) |
                        (static_cast<uint32_t>(static_cast<unsigned char>(data[0x3e])) << 16) |
                        (static_cast<uint32_t>(static_cast<unsigned char>(data[0x3f])) << 24);
    require(offset <= data.size() - 6 && data.substr(offset, 4) == std::string("PE\0\0", 4) &&
            static_cast<unsigned char>(data[offset + 4]) == 0x64 &&
            static_cast<unsigned char>(data[offset + 5]) == 0x86, "Expected Windows x64 PE binary");
#else
    require(data.size() >= 20 && data.substr(0, 6) == std::string("\177ELF\2\1", 6) &&
            static_cast<unsigned char>(data[18]) == 62 && data[19] == 0, "Expected Linux x64 ELF binary");
#endif
}
}
std::optional<Json> select_asset(const Json& release, const std::string& repository, const std::string& current) {
    require(matches(repository, "[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+"), "Invalid repository");
    if (release.value("draft", false) || release.value("prerelease", false)) return {};
    const auto tag = release.at("tag_name").get<std::string>();
    if (version(tag) <= version(current)) return {};
    const auto target = tag.starts_with('v') ? tag.substr(1) : tag;
    const auto name = "BriefcaseNative-Server-" + std::string(platform_name) + "-" + target + ".zip";
    std::optional<Json> selected;
    for (const auto& asset : release.at("assets")) {
        if (asset.at("name") != name) continue;
        require(!selected, "Duplicate release asset");
        const auto url = "https://github.com/" + repository + "/releases/download/" + tag + "/" + name;
        const auto hash = asset.at("digest").get<std::string>();
        require(asset.at("browser_download_url") == url && asset.at("state") == "uploaded" &&
                number(asset.at("size")) > 0 && number(asset.at("size")) <= max_archive &&
                matches(hash, "sha256:[a-f0-9]{64}"), "Invalid GitHub asset metadata");
        selected = Json{{"version", target}, {"url", url}, {"size", asset.at("size")}, {"sha256", hash.substr(7)}};
    }
    return selected;
}
std::optional<Json> select_mod_asset(const Json& release, const std::string& repository,
                                     const std::string& current, const std::string& platform) {
    require(matches(repository, "[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+") &&
            matches(platform, "(windows|linux)-x64"), "Invalid mod update identity");
    if (release.value("draft", false) || release.value("prerelease", false)) return {};
    const auto tag = release.at("tag_name").get<std::string>();
    if (version(tag) <= version(current)) return {};
    const auto target = tag.starts_with('v') ? tag.substr(1) : tag;
    const auto repository_name = repository.substr(repository.find('/') + 1);
    const auto name = repository_name + "-" + platform + "-" + target + ".zip";
    std::optional<Json> selected;
    for (const auto& asset : release.at("assets")) {
        if (asset.at("name") != name) continue;
        require(!selected, "Duplicate mod release asset");
        const auto url = "https://github.com/" + repository + "/releases/download/" + tag + "/" + name;
        const auto hash = asset.at("digest").get<std::string>();
        require(asset.at("browser_download_url") == url && asset.at("state") == "uploaded" &&
                number(asset.at("size")) > 0 && number(asset.at("size")) <= max_archive &&
                matches(hash, "sha256:[a-f0-9]{64}"), "Invalid GitHub mod asset metadata");
        selected = Json{{"version", target}, {"url", url}, {"size", asset.at("size")},
                        {"sha256", hash.substr(7)}};
    }
    return selected;
}
Json package_manifest(const fs::path& stage, const std::string& expected, const std::string& game_hash) {
    version(expected);
    const auto manifest = document(package_path(stage, "Package.json"));
    require(manifest.at("updateSchema") == 1 && manifest.at("environment") == "server" &&
            manifest.at("platform") == platform_name && manifest.at("frameworkVersion") == expected &&
            manifest.at("gameSha256") == game_hash && matches(game_hash, "[a-f0-9]{64}"), "Incompatible package identity");
    std::set<std::string> seen;
    require(manifest.at("files").is_array() && manifest.at("files").size() <= 4095, "Invalid package inventory");
    for (const auto& item : manifest.at("files")) {
        const auto name = item.at("path").get<std::string>();
        const auto path = package_path(stage, name);
        require(name != "Package.json" && managed(name) && seen.insert(name).second, "Invalid manifest path");
        const auto mode = number(item.at("mode"));
        require(number(item.at("bytes")) <= max_archive && (mode == 0644 || mode == 0755) &&
                matches(item.at("sha256").get<std::string>(), "[a-f0-9]{64}") && fs::file_size(path) == item.at("bytes") &&
                digest(path) == item.at("sha256").get<std::string>(), "Package file mismatch");
        if (native_binary(name)) {
#ifndef _WIN32
            require(mode == 0755, "Native binary must be executable");
#endif
            validate_binary(read(path, max_archive));
        }
    }
    std::set<std::string> actual;
    for (const auto& entry : fs::recursive_directory_iterator(stage)) {
        plain(entry.path());
        require(entry.is_directory() || entry.is_regular_file(), "Special file in package");
        if (entry.is_regular_file()) actual.insert(entry.path().lexically_relative(stage).generic_string());
    }
    auto all = seen; all.insert("Package.json");
    require(actual == all && std::includes(seen.begin(), seen.end(), required.begin(), required.end()), "Incomplete or unlisted package content");
    require(document(package_path(stage, "Briefcase/Core/Updater/build.json")).at("frameworkVersion") == expected, "Version marker mismatch");
    return manifest;
}
Json mod_package_manifest(const fs::path& stage, const std::string& id, const std::string& expected,
                          const std::string& repository, const std::string& platform) {
    require(mod_id(id) && matches(repository, "[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+") &&
            matches(platform, "(windows|linux)-x64"), "Invalid mod package identity");
    version(expected);
    const auto manifest = document(package_path(stage, "ModPackage.json"));
    require(manifest.at("updateSchema") == 1 && manifest.at("kind") == "briefcase-mod" &&
            manifest.at("platform") == platform && manifest.at("modId") == id &&
            manifest.at("version") == expected && manifest.at("repository") == repository,
            "Incompatible mod package identity");
    const auto prefix = "Briefcase/Mods/" + id + "/";
    std::set<std::string> seen;
    require(manifest.at("files").is_array() && manifest.at("files").size() <= 1024,
            "Invalid mod package inventory");
    for (const auto& item : manifest.at("files")) {
        const auto name = item.at("path").get<std::string>();
        const auto path = package_path(stage, name);
        const bool preserve = item.value("preserve", false);
        require(name.starts_with(prefix) && name.size() > prefix.size() && seen.insert(name).second &&
                (!preserve || name == prefix + "Data/config.json"), "Invalid mod package path");
        const auto mode = number(item.at("mode"));
        require(number(item.at("bytes")) <= max_archive && (mode == 0644 || mode == 0755) &&
                matches(item.at("sha256").get<std::string>(), "[a-f0-9]{64}") &&
                fs::file_size(path) == item.at("bytes") && digest(path) == item.at("sha256").get<std::string>(),
                "Mod package file mismatch");
    }
    std::set<std::string> actual;
    for (const auto& entry : fs::recursive_directory_iterator(stage)) {
        plain(entry.path());
        require(entry.is_directory() || entry.is_regular_file(), "Special file in mod package");
        if (entry.is_regular_file()) actual.insert(entry.path().lexically_relative(stage).generic_string());
    }
    auto all = seen; all.insert("ModPackage.json");
    require(actual == all, "Unlisted mod package content");
    const auto mod = document(package_path(stage, prefix + "briefcase.mod.json"));
    require(mod.at("schemaVersion") == 1 && mod.at("id") == id && mod.at("version") == expected &&
            (mod.at("environment") == "server" || mod.at("environment") == "both") &&
            mod.at("update").at("provider") == "github-releases" &&
            mod.at("update").at("repository") == repository, "Packaged mod manifest mismatch");
    const auto entry = mod.at("entry").get<std::string>();
    require(matches(entry, platform == "linux-x64" ? "[A-Za-z0-9_.-]+\\.so" : "[A-Za-z0-9_.-]+\\.dll") &&
            seen.contains(prefix + entry), "Mod entry is missing or incompatible");
    validate_binary(read(package_path(stage, prefix + entry), max_archive));
    return manifest;
}
Updater::Updater(fs::path root, Download fetch) : root_(plain(root)), fetch_(std::move(fetch)) {}
void Updater::recover() {
    const auto journal_path = package_path(root_, "Briefcase/Updates/transaction.json");
    if (!fs::exists(journal_path)) return;
    auto journal = document(journal_path);
    const auto state = journal.at("state").get<std::string>();
    require(state == "installing" || state == "installed" || state == "rolled-back", "Invalid recovery state");
    if (state != "installing") return;
    const auto id = journal.at("id").get<std::string>();
    require(matches(id, "[a-f0-9]{32}"), "Invalid recovery identifier");
    require(journal.at("files").is_array() && journal.at("files").size() <= 8192, "Invalid recovery inventory");
    std::set<std::string> seen;
    for (const auto& row : journal.at("files")) {
        const auto name = legacy_name(row.at("path").get<std::string>());
        require(managed(name, true) && seen.insert(name).second && row.at("existed").is_boolean(), "Invalid recovery target");
        const auto target = package_path(root_, name);
        if (fs::exists(target)) read(target, max_archive);
        if (row.at("existed")) {
            const auto backup = package_path(root_, "Briefcase/Updates/" + id + "/backup/" + name);
            const auto mode = row.value("mode", 0644u);
            auto hash = row.contains("sha256") ? row.at("sha256").get<std::string>() : row.at("previousHash").get<std::string>();
            std::ranges::transform(hash, hash.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            require(mode <= 0777 && mode_ok(mode) && digest(backup) == hash, "Damaged recovery backup");
        }
    }
    for (const auto& row : journal.at("files")) {
        const auto name = legacy_name(row.at("path").get<std::string>());
        const auto target = package_path(root_, name);
        if (row.at("existed")) atomic(target, read(package_path(root_, "Briefcase/Updates/" + id + "/backup/" + name), max_archive), row.value("mode", 0644u));
        else remove_file(target);
    }
    journal["state"] = "rolled-back"; write_json(journal_path, journal);
}
void Updater::install(const fs::path& stage, const Json& supplied, const std::function<void(size_t)>& after_write) {
    // Revalidate immediately before mutating installed files.
    const auto manifest = package_manifest(stage, supplied.at("frameworkVersion"), digest(root_ / game_name));
    require(manifest == supplied, "Manifest changed before installation");
    recover();
    const auto id = identifier();
    const auto old_path = package_path(root_, "Package.json");
    std::set<std::string> names{"Package.json"};
    std::map<std::string, Json> wanted;
    for (const auto& item : manifest.at("files")) { const auto name = item.at("path").get<std::string>(); names.insert(name); wanted[name] = item; }
    if (fs::exists(old_path)) {
        const auto old = document(old_path);
        require(old.at("environment") == "server" && old.at("platform") == platform_name && old.at("files").size() <= 4095, "Invalid installed manifest");
        for (const auto& item : old.at("files")) {
            const auto name = legacy_name(item.at("path").get<std::string>());
            require(managed(name, true), "Unmanaged installed manifest path"); package_path(root_, name); names.insert(name);
        }
    }
    Json backups = Json::array();
    for (const auto& name : names) {
        const auto target = package_path(root_, name);
        Json row{{"path", name}, {"existed", fs::exists(target)}};
        if (row["existed"]) {
            const auto data = read(target, max_archive);
            const auto mode = file_mode(target); require(mode_ok(mode), "Unsupported installed mode");
            const auto backup = package_path(root_, "Briefcase/Updates/" + id + "/backup/" + name);
            atomic(backup, data, mode);
            row["sha256"] = digest(backup); row["mode"] = mode;
        }
        backups.push_back(row);
    }
    Json journal{{"state", "installing"}, {"id", id}, {"files", backups}};
    const auto journal_path = package_path(root_, "Briefcase/Updates/transaction.json");
    write_json(journal_path, journal);
    try {
        size_t index = 0;
        for (const auto& name : names) {
            if (name == "Package.json") continue;
            const auto target = package_path(root_, name);
            if (const auto found = wanted.find(name); found != wanted.end()) {
                atomic(target, read(package_path(stage, name), max_archive), static_cast<unsigned>(number(found->second.at("mode"))));
                require(digest(target) == found->second.at("sha256").get<std::string>(), "Installed file readback mismatch");
            } else remove_file(target);
            if (after_write) after_write(index++);
        }
        atomic(old_path, read(package_path(stage, "Package.json")), 0644);
        journal["state"] = "installed"; write_json(journal_path, journal);
    } catch (...) { recover(); throw; }
}
void Updater::recover_mod() {
    const auto journal_path = package_path(root_, "Briefcase/Updates/mod-transaction.json");
    if (!fs::exists(journal_path)) return;
    auto journal = document(journal_path);
    if (journal.at("state") != "installing") return;
    const auto transaction = journal.at("id").get<std::string>();
    const auto mod = journal.at("modId").get<std::string>();
    require(matches(transaction, "[a-f0-9]{32}") && mod_id(mod) && journal.at("files").is_array() &&
            journal.at("files").size() <= 2048, "Invalid mod recovery journal");
    const auto prefix = "Briefcase/Mods/" + mod + "/";
    std::set<std::string> seen;
    for (const auto& row : journal.at("files")) {
        const auto name = legacy_name(row.at("path").get<std::string>());
        require(name.starts_with(prefix) && seen.insert(name).second && row.at("existed").is_boolean(),
                "Invalid mod recovery target");
        const auto target = package_path(root_, name);
        if (fs::exists(target)) read(target, max_archive);
        if (row.at("existed")) {
            const auto backup = package_path(root_, "Briefcase/Updates/" + transaction + "/backup/" + name);
            const auto mode = row.value("mode", 0644u);
            auto hash = row.contains("sha256") ? row.at("sha256").get<std::string>() : row.at("previousHash").get<std::string>();
            std::ranges::transform(hash, hash.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            require(mode <= 0777 && mode_ok(mode) && digest(backup) == hash, "Damaged mod recovery backup");
        }
    }
    for (const auto& row : journal.at("files")) {
        const auto name = legacy_name(row.at("path").get<std::string>());
        const auto target = package_path(root_, name);
        if (row.at("existed"))
            atomic(target, read(package_path(root_, "Briefcase/Updates/" + transaction + "/backup/" +
                                            name), max_archive), row.value("mode", 0644u));
        else remove_file(target);
    }
    journal["state"] = "rolled-back";
    write_json(journal_path, journal);
}
void Updater::install_mod(const fs::path& stage, const Json& supplied) {
    const auto id = supplied.at("modId").get<std::string>();
    const auto repository = supplied.at("repository").get<std::string>();
    const auto platform = supplied.at("platform").get<std::string>();
    const auto release = supplied.at("version").get<std::string>();
    const auto manifest = mod_package_manifest(stage, id, release, repository, platform);
    require(manifest == supplied, "Mod manifest changed before installation");
    recover_mod();
    const auto prefix = "Briefcase/Mods/" + id + "/";
    const auto metadata_name = prefix + ".briefcase-update.json";
    const auto metadata_path = package_path(root_, metadata_name);
    std::map<std::string, Json> wanted;
    std::set<std::string> names{metadata_name}, managed_files;
    for (const auto& item : manifest.at("files")) {
        const auto name = item.at("path").get<std::string>();
        if (item.value("preserve", false)) {
            if (!fs::exists(package_path(root_, name))) { wanted[name] = item; names.insert(name); }
        } else { wanted[name] = item; names.insert(name); managed_files.insert(name); }
    }
    if (fs::exists(metadata_path)) {
        const auto previous = document(metadata_path);
        require(previous.at("schemaVersion") == 1 && previous.at("modId") == id &&
                previous.at("files").is_array() && previous.at("files").size() <= 1024,
                "Invalid installed mod update metadata");
        for (const auto& value : previous.at("files")) {
            const auto name = value.get<std::string>();
            require(name.starts_with(prefix), "Invalid installed mod managed path");
            if (!managed_files.contains(name)) names.insert(name);
        }
    }
    Json metadata{{"schemaVersion", 1}, {"modId", id}, {"repository", repository},
                  {"version", release}, {"platform", platform}, {"files", Json::array()}};
    for (const auto& name : managed_files) metadata["files"].push_back(name);
    const auto transaction = identifier();
    Json backups = Json::array();
    for (const auto& name : names) {
        const auto target = package_path(root_, name);
        Json row{{"path", name}, {"existed", fs::exists(target)}};
        if (row["existed"]) {
            const auto data = read(target, max_archive);
            const auto mode = file_mode(target);
            require(mode_ok(mode), "Unsupported mod file mode");
            const auto backup = package_path(root_, "Briefcase/Updates/" + transaction + "/backup/" + name);
            atomic(backup, data, mode);
            row["sha256"] = digest(backup); row["mode"] = mode;
        }
        backups.push_back(row);
    }
    Json journal{{"state", "installing"}, {"id", transaction}, {"modId", id}, {"files", backups}};
    const auto journal_path = package_path(root_, "Briefcase/Updates/mod-transaction.json");
    write_json(journal_path, journal);
    try {
        for (const auto& name : names) {
            if (name == metadata_name) continue;
            const auto target = package_path(root_, name);
            if (const auto found = wanted.find(name); found != wanted.end()) {
                atomic(target, read(package_path(stage, name), max_archive), static_cast<unsigned>(number(found->second.at("mode"))));
                require(digest(target) == found->second.at("sha256").get<std::string>(), "Installed mod file mismatch");
            } else remove_file(target);
        }
        atomic(metadata_path, metadata.dump(2) + "\n", 0600);
        journal["state"] = "installed"; write_json(journal_path, journal);
    } catch (...) { recover_mod(); throw; }
}
Json Updater::update_mods(const std::function<void(const std::string&)>& log) {
    recover_mod();
    Json result{{"state", "completed"}, {"checked", 0}, {"updated", 0}, {"current", 0},
                {"failed", 0}, {"mods", Json::array()}};
    Json config;
    int timeout{};
    try {
        config = document(package_path(root_, "Briefcase/updater.json"));
        const auto configured_timeout = number(config.at("timeoutSeconds"));
        require(configured_timeout <= 120, "Invalid updater timeout");
        timeout = static_cast<int>(configured_timeout);
        require(config.at("schemaVersion") == 1 && config.at("enabled").is_boolean() &&
                (!config.contains("updateMods") || config.at("updateMods").is_boolean()) &&
                timeout >= 1 && timeout <= 120, "Invalid updater settings");
    } catch (const std::exception& error) {
        result["state"] = "failed-kept-installed"; result["message"] = error.what();
        log("Mod updates unavailable; installed mods retained: " + std::string(error.what()));
        return result;
    }
    if (!config.at("enabled").get<bool>() ||
        (config.contains("updateMods") && !config.at("updateMods").get<bool>())) {
        result["state"] = "disabled"; return result;
    }
    const auto mods = package_path(root_, "Briefcase/Mods");
    if (!fs::exists(mods)) return result;
    plain(mods);
    size_t count = 0;
    for (const auto& directory : fs::directory_iterator(mods)) {
        require(++count <= 256, "Too many installed mods");
        plain(directory.path());
        if (!directory.is_directory()) continue;
        const auto installed_path = directory.path() / "briefcase.mod.json";
        if (!fs::exists(installed_path)) continue;
        std::string id = directory.path().filename().string();
        try {
            const auto installed = document(installed_path);
            require(mod_id(id) && installed.at("id") == id &&
                    (installed.at("environment") == "server" || installed.at("environment") == "both"),
                    "Invalid installed server mod manifest");
            if (!installed.contains("update")) continue;
            const auto repository = installed.at("update").at("repository").get<std::string>();
            require(installed.at("update").at("provider") == "github-releases" &&
                    matches(repository, "[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+"), "Invalid mod update source");
            const auto current = installed.at("version").get<std::string>(); version(current);
            result["checked"] = result["checked"].get<size_t>() + 1;
            const auto work = package_path(root_, "Briefcase/Updates/" + identifier());
            fs::create_directories(work);
            fetch_("https://api.github.com/repos/" + repository + "/releases/latest",
                   work / "release.json", timeout, 2 * 1024 * 1024);
            const auto asset = select_mod_asset(document(work / "release.json"), repository, current, platform_name);
            if (!asset) {
                result["current"] = result["current"].get<size_t>() + 1;
                result["mods"].push_back({{"id", id}, {"state", "current"}, {"version", current}}); continue;
            }
            const auto archive = work / "release.zip";
            fetch_(asset->at("url"), archive, timeout, asset->at("size"));
            require(fs::file_size(archive) == asset->at("size") &&
                    digest(archive) == asset->at("sha256").get<std::string>(), "Mod archive digest mismatch");
            const auto stage = work / "stage"; extract(archive, stage, true);
            const auto package = mod_package_manifest(stage, id, asset->at("version"), repository, platform_name);
            install_mod(stage, package);
            result["updated"] = result["updated"].get<size_t>() + 1;
            result["mods"].push_back({{"id", id}, {"state", "updated"}, {"version", asset->at("version")}});
            log("Mod updated: " + id + " " + asset->at("version").get<std::string>());
        } catch (const std::exception& error) {
            recover_mod();
            result["failed"] = result["failed"].get<size_t>() + 1;
            result["mods"].push_back({{"id", id}, {"state", "failed"}, {"message", error.what()}});
            log("Mod update unavailable for " + id + "; installed version retained: " + error.what());
        }
    }
    return result;
}
void Updater::cleanup(const std::function<void(const std::string&)>& log) {
    const auto updates = package_path(root_, "Briefcase/Updates");
    if (!fs::exists(updates)) return;
    plain(updates);
    std::set<std::string> active;
    for (const auto* name : {"transaction.json", "mod-transaction.json"}) {
        const auto journal = updates / name;
        if (!fs::exists(journal)) continue;
        try {
            const auto value = document(journal);
            const auto state = value.at("state").get<std::string>();
            require(state == "installing" || state == "installed" || state == "rolled-back",
                    "Invalid update journal state");
            if (state == "installing") {
                const auto id = value.at("id").get<std::string>();
                require(matches(id, "[a-f0-9]{32}"), "Invalid active update identifier");
                active.insert(id);
            }
        } catch (const std::exception& error) {
            log("Could not inspect update journal " + std::string(name) + ": " + error.what());
            return;
        }
    }
    std::vector<fs::path> work;
    for (const auto& entry : fs::directory_iterator(updates)) {
        const auto name = entry.path().filename().string();
        const auto id = name.starts_with("install-") ? name.substr(8) : name;
        if (entry.is_directory() && !active.contains(id) &&
            (matches(name, "[a-f0-9]{32}") || matches(name, "install-[a-f0-9]{32}")))
            work.push_back(entry.path());
    }
    for (const auto& directory : work) {
        try {
            plain(directory);
            for (const auto& entry : fs::recursive_directory_iterator(directory)) {
                plain(entry.path());
                require(entry.is_directory() || entry.is_regular_file(), "Unsafe update work entry");
            }
            std::error_code error;
            fs::remove_all(directory, error);
            require(!error, "Cannot remove completed update work directory");
        } catch (const std::exception& error) {
            log("Could not clean completed update work directory " + directory.filename().string() + ": " + error.what());
        }
    }
    for (const auto* name : {"transaction.json", "mod-transaction.json"}) {
        const auto journal = updates / name;
        try {
            if (!fs::exists(journal)) continue;
            const auto state = document(journal).at("state").get<std::string>();
            if (state == "installed" || state == "rolled-back") remove_file(journal);
        } catch (const std::exception& error) {
            log("Could not clean completed update journal " + std::string(name) + ": " + error.what());
        }
    }
}
std::string Updater::update(const std::function<void(const std::string&)>& log) {
    recover(); // Recovery errors must prevent launch, even with updates disabled.
    const auto config_path = package_path(root_, "Briefcase/updater.json");
    fs::path stage; Json manifest;
    try {
        // The launcher holds the installation lock. Only initialize a missing
        // user configuration; never reset an opt-out or custom repository.
        if (!fs::exists(config_path)) {
            write_json(config_path, {{"schemaVersion", 1}, {"enabled", true}, {"updateMods", true},
                                    {"repository", "EnoPM/BriefcaseNative"}, {"timeoutSeconds", 20}});
        }
        const auto config = document(config_path);
        const auto configured_timeout = number(config.at("timeoutSeconds"));
        require(configured_timeout >= 1 && configured_timeout <= 120, "Invalid updater timeout");
        const auto timeout = static_cast<int>(configured_timeout);
        require(config.at("schemaVersion") == 1 && config.at("enabled").is_boolean(), "Invalid updater settings");
        if (!config.at("enabled")) return "disabled";
        const auto repository = config.value("repository", std::string{});
        if (repository.empty()) return "not-configured";
        require(matches(repository, "[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+"), "Invalid repository");
        const auto current = document(package_path(root_, "Briefcase/Core/Updater/build.json")).at("frameworkVersion").get<std::string>();
        const auto directory = package_path(root_, "Briefcase/Updates/" + identifier());
        fs::create_directories(directory);
        fetch_("https://api.github.com/repos/" + repository + "/releases/latest", directory / "release.json", timeout, 2 * 1024 * 1024);
        const auto asset = select_asset(document(directory / "release.json"), repository, current);
        if (!asset) return "current";
        const auto archive = directory / "release.zip";
        fetch_(asset->at("url"), archive, timeout, asset->at("size").get<uint64_t>());
        require(fs::file_size(archive) == asset->at("size") && digest(archive) == asset->at("sha256").get<std::string>(), "Archive digest mismatch");
        stage = directory / "stage"; extract(archive, stage);
        manifest = package_manifest(stage, asset->at("version"), digest(root_ / game_name));
    } catch (const std::exception& error) {
        log("Update unavailable; installed version retained: " + std::string(error.what()));
        return "failed-kept-installed";
    }
    try { install(stage, manifest); }
    catch (const std::exception&) {
        recover(); log("Update failed and was rolled back"); return "failed-kept-installed";
    }
    log("Update installed: " + manifest.at("frameworkVersion").get<std::string>());
    return "installed";
}
}
