#include "LinuxUpdate.hpp"
#include <array>
#include <regex>
#include <set>
#include <sys/stat.h>

namespace bc::launcher {
void remove_file(const fs::path& path);
namespace {
bool matches(const std::string& value, const char* expression) { return std::regex_match(value, std::regex(expression)); }
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
const std::set<std::string> required{
    "Briefcase.ServerLauncher", "Briefcase/Runtime/libBriefcase.NativeHost.so", "Briefcase/Runtime/libBriefcase.ServerBootstrap.so",
    "Briefcase/Tools/Briefcase.AdminSetup", "Briefcase/Updater/build.json"};
}
std::optional<Json> select_asset(const Json& release, const std::string& repository, const std::string& current) {
    require(matches(repository, "[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+"), "Invalid repository");
    if (release.value("draft", false) || release.value("prerelease", false)) return {};
    const auto tag = release.at("tag_name").get<std::string>();
    if (version(tag) <= version(current)) return {};
    const auto target = tag.starts_with('v') ? tag.substr(1) : tag;
    const auto name = "BriefcaseNative-Server-linux-x64-" + target + ".zip";
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
    require(selected.has_value(), "Release has no Linux server asset");
    return selected;
}
Json package_manifest(const fs::path& stage, const std::string& expected, const std::string& game_hash) {
    version(expected);
    const auto manifest = document(package_path(stage, "Package.json"));
    require(manifest.at("updateSchema") == 1 && manifest.at("environment") == "server" &&
            manifest.at("platform") == "linux-x64" && manifest.at("frameworkVersion") == expected &&
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
        if (name == "Briefcase.ServerLauncher" || name.ends_with(".so") || name == "Briefcase/Tools/Briefcase.AdminSetup") {
            require(mode == 0755, "Native binary must be executable");
            const auto data = read(path, max_archive);
            require(data.size() >= 20 && data.substr(0, 6) == std::string("\177ELF\2\1", 6) &&
                    static_cast<unsigned char>(data[18]) == 62 && data[19] == 0, "Expected Linux x64 ELF binary");
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
    require(document(package_path(stage, "Briefcase/Updater/build.json")).at("frameworkVersion") == expected, "Version marker mismatch");
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
        const auto name = row.at("path").get<std::string>();
        require(managed(name, true) && seen.insert(name).second && row.at("existed").is_boolean(), "Invalid recovery target");
        const auto target = package_path(root_, name);
        if (fs::exists(target)) read(target, max_archive);
        if (row.at("existed")) {
            const auto backup = package_path(root_, "Briefcase/Updates/" + id + "/backup/" + name);
            require(number(row.at("mode")) <= 0777 && mode_ok(number(row.at("mode"))) && digest(backup) == row.at("sha256").get<std::string>(), "Damaged recovery backup");
        }
    }
    for (const auto& row : journal.at("files")) {
        const auto name = row.at("path").get<std::string>();
        const auto target = package_path(root_, name);
        if (row.at("existed")) atomic(target, read(package_path(root_, "Briefcase/Updates/" + id + "/backup/" + name), max_archive), number(row.at("mode")));
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
        require(old.at("environment") == "server" && old.at("platform") == "linux-x64" && old.at("files").size() <= 4095, "Invalid installed manifest");
        for (const auto& item : old.at("files")) {
            const auto name = item.at("path").get<std::string>();
            require(managed(name, true), "Unmanaged installed manifest path"); package_path(root_, name); names.insert(name);
        }
    }
    Json backups = Json::array();
    for (const auto& name : names) {
        const auto target = package_path(root_, name);
        Json row{{"path", name}, {"existed", fs::exists(target)}};
        if (row["existed"]) {
            const auto data = read(target, max_archive);
            struct stat info{}; require(lstat(target.c_str(), &info) == 0 && mode_ok(info.st_mode & 07777), "Unsupported installed mode");
            const auto backup = package_path(root_, "Briefcase/Updates/" + id + "/backup/" + name);
            atomic(backup, data, info.st_mode & 0777);
            row["sha256"] = digest(backup); row["mode"] = info.st_mode & 0777;
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
                atomic(target, read(package_path(stage, name), max_archive), number(found->second.at("mode")));
                require(digest(target) == found->second.at("sha256").get<std::string>(), "Installed file readback mismatch");
            } else remove_file(target);
            if (after_write) after_write(index++);
        }
        atomic(old_path, read(package_path(stage, "Package.json")), 0644);
        journal["state"] = "installed"; write_json(journal_path, journal);
    } catch (...) { recover(); throw; }
}
std::string Updater::update(const std::function<void(const std::string&)>& log) {
    recover(); // Recovery errors must prevent launch, even with updates disabled.
    const auto config_path = package_path(root_, "Briefcase/updater.json");
    fs::path stage; Json manifest;
    try {
        // The launcher holds the installation lock. Only initialize a missing
        // user configuration; never reset an opt-out or custom repository.
        if (!fs::exists(config_path)) {
            write_json(config_path, {{"schemaVersion", 1}, {"enabled", true},
                                    {"repository", "EnoPM/BriefcaseNative"}, {"timeoutSeconds", 20}});
        }
        const auto config = document(config_path);
        const auto timeout = number(config.at("timeoutSeconds"));
        require(config.at("schemaVersion") == 1 && config.at("enabled").is_boolean() && timeout >= 1 && timeout <= 120, "Invalid updater settings");
        if (!config.at("enabled")) return "disabled";
        const auto repository = config.value("repository", std::string{});
        if (repository.empty()) return "not-configured";
        require(matches(repository, "[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+"), "Invalid repository");
        const auto current = document(package_path(root_, "Briefcase/Updater/build.json")).at("frameworkVersion").get<std::string>();
        const auto directory = package_path(root_, "Briefcase/Updates/" + identifier());
        fs::create_directories(directory);
        fetch_("https://api.github.com/repos/" + repository + "/releases/latest", directory / "release.json", timeout, 2 * 1024 * 1024);
        const auto asset = select_asset(document(directory / "release.json"), repository, current);
        if (!asset) return "current";
        const auto archive = directory / "release.zip";
        fetch_(asset->at("url"), archive, timeout, asset->at("size"));
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
