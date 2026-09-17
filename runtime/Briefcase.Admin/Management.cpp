#include "Management.hpp"
#include "../Briefcase.Localization/Catalog.hpp"
#include "../Briefcase.NativeHost/Configuration.hpp"
#include "../Briefcase.NativeHost/Startup.hpp"
#ifdef _WIN32
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <sstream>
namespace bc::admin {
namespace {
constexpr auto section = "/Script/DeceiveInc.TripwireServerSettings";
void require(bool ok, const char *message) {
    if (!ok)
        throw Error("invalid_values", message);
}
std::string revision(const Json &j) {
    return digest(j.dump());
}
Json document(const fs::path &p) {
    return strict_json(read_file(p, 1048576), 1048576);
}
void expected(const Json &p, const Json &current) {
    require(p.is_object() && p.contains("expectedRevision"), "Missing revision.");
    if (p["expectedRevision"] != current["revision"])
        throw Error("conflict", "Values changed. Reload before saving.");
}
std::string trim(std::string s) {
    auto a = s.find_first_not_of(" \t\r");
    if (a == s.npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}
const Json maps = Json::array({"DI_Hardsell", "DI_SR", "DI_DS", "DI_FS", "DI_SE", "DI_FSN", "DI_HSD"});
Json map_read(const std::string &text) {
    std::istringstream stream(text);
    std::string line;
    bool active = false;
    Json out = Json::array();
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty())
            continue;
        if (line[0] == '[') {
            active = line == "[" + std::string(section) + "]";
            continue;
        }
        if (!active || line[0] == ';' || line[0] == '#')
            continue;
        auto eq = line.find('=');
        if (eq == line.npos)
            continue;
        auto key = trim(line.substr(0, eq));
        char op = key.empty() ? 0 : key[0];
        if (op == '+' || op == '!' || op == '-' || op == '.')
            key.erase(0, 1);
        if (key != "MapRotation")
            continue;
        if (op == '!') {
            out = Json::array();
            continue;
        }
        std::istringstream values(line.substr(eq + 1));
        std::string value;
        while (std::getline(values, value, ',')) {
            value = trim(value);
            if (value.size() > 1 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);
            if (op == '-')
                out.erase(std::remove(out.begin(), out.end(), Json(value)), out.end());
            else if (!value.empty() && std::find(out.begin(), out.end(), Json(value)) == out.end())
                out.push_back(value);
        }
    }
    return out.empty() ? maps : out;
}
std::string map_write(const std::string &text, const Json &values) {
    std::istringstream stream(text);
    std::string line, out;
    bool active = false, written = false;
    for (; std::getline(stream, line);) {
        auto t = trim(line);
        if (!t.empty() && t[0] == '[')
            active = t == "[" + std::string(section) + "]";
        auto eq = t.find('=');
        auto key = eq == t.npos ? std::string{} : trim(t.substr(0, eq));
        if (!key.empty() && std::string("+!-.").find(key.front()) != std::string::npos)
            key.erase(0, 1);
        if (active && key == "MapRotation")
            continue;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        out += line + "\r\n";
        if (active && !written && t == "[" + std::string(section) + "]") {
            out += "!MapRotation=ClearArray\r\n";
            for (auto &v : values)
                out += "+MapRotation=" + v.get<std::string>() + "\r\n";
            written = true;
        }
    }
    require(written, "Missing server section.");
    return out;
}
Json ini_values(const std::string &text) {
    auto schema = Management::config_schema();
    Json values = Json::object();
    for (auto it = schema["properties"].begin(); it != schema["properties"].end(); ++it) {
        auto &f = it.value();
        auto v = f["default"];
        std::string raw;
        if (it.key() == "MapRotation") {
            values[it.key()] = map_read(text);
            continue;
        }
        try {
            raw = ini_read(text, section, it.key());
        } catch (...) {
            values[it.key()] = v;
            continue;
        }
        if (f["type"] == "string") {
            if (raw.size() > 1 && raw.front() == '"' && raw.back() == '"')
                raw = raw.substr(1, raw.size() - 2);
            v = raw;
        } else if (f["type"] == "boolean") {
            std::transform(raw.begin(), raw.end(), raw.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            require(raw == "true" || raw == "false" || raw == "1" || raw == "0", "Invalid INI boolean.");
            v = raw == "true" || raw == "1";
        } else {
            try {
                v = Json::parse(raw);
            } catch (...) {
                throw Error("invalid_values", "Invalid INI number.");
            }
        }
        values[it.key()] = v;
    }
    return normalize_config(schema, values);
}
std::string entry_id(const Json &x) {
    return revision(Json::array({x.at("table"), x.at("row"), x.at("field")}));
}
Json profile_entries(const Json &p) {
    require(p.is_object() && p.value("format", "") == "DeceiveCommunityBalanceProfile" &&
                p.value("schemaVersion", 0) == 1 && p.contains("overrides") && p["overrides"].is_array() &&
                p["overrides"].size() <= 2048,
            "Incompatible balancing profile.");
    Json entries = Json::object();
    for (const auto &x : p["overrides"]) {
        for (auto key : {"table", "row", "field"})
            require(x.contains(key) && x[key].is_string() &&
                        x[key].get_ref<const std::string &>().size() <= 128,
                    "Invalid balancing identifier.");
        require(x.contains("value") && (x["value"].is_boolean() ||
                                        (x["value"].is_number() && std::isfinite(x["value"].get<double>()))),
                "Invalid balancing value.");
        auto id = entry_id(x);
        require(!entries.contains(id), "Duplicate balancing setting.");
        entries[id] = x;
    }
    return entries;
}
} // namespace
Json Management::config_schema() {
    Json props = Json::object();
    auto add = [&](const char *k, const char *label, Json value, Json lo = nullptr, Json hi = nullptr,
                   Json options = nullptr) {
        Json f = {{"type", value.is_boolean()          ? "boolean"
                           : value.is_number_integer() ? "integer"
                           : value.is_number()         ? "number"
                           : value.is_array()          ? "array"
                                                       : "string"},
                  {"default", value},
                  {"description", label}};
        if (!lo.is_null())
            f["minimum"] = lo;
        if (!hi.is_null())
            f["maximum"] = hi;
        if (!options.is_null())
            f["enum"] = options;
        if (value.is_array())
            f["items"] = {{"type", "string"}};
        props[k] = f;
    };
    add("ServerName", "Server name", "Deceive Inc. Server");
    add("ServerRegion", "Region", "eu", nullptr, nullptr,
        Json::array({"", "us-east", "us-central", "us-west", "eu", "oce", "br", "asia", "me"}));
    add("GameMode", "Game mode", "Solo", nullptr, nullptr, Json::array({"Solo", "Duo", "Trio"}));
    add("Password", "Game password", "");
    props["Password"]["secret"] = true;
    add("MapRotation", "Map rotation", maps);
    add("GamePort", "Game port", 50000, 1024, 65535);
    add("QueryPort", "Query port", 50001, 1024, 65535);
    add("bCrossplay", "Crossplay", true);
    add("bIsPublic", "Public server", true);
    add("bEnableUPnP", "UPnP", true);
    add("AutoShutdownEmptyMinutes", "Shutdown when empty (minutes, 0 = disabled)", 0, 0, 1440);
    add("bSandboxMode", "Sandbox mode", false);
    add("bFillWithBots", "Fill with bots", true);
    add("BotsDifficulty", "Bot difficulty", "Normal", nullptr, nullptr,
        Json::array({"Easy", "Normal", "Difficult"}));
    add("BotsAmount", "Bot count", 0, 0, 8);
    add("MaxPlayers", "Player limit", 8, 1, 12);
    add("bRandomizeMap", "Random maps", false);
    for (auto k : {"HeatPercentDamagingCivilian", "HeatPercentDamagingStaff", "HeatPercentDamagingGuard",
                   "HeatPercentDamagingTechnician", "HeatPercentDamagingVIP"})
        add(k, k,
            std::string(k) == "HeatPercentDamagingVIP"     ? 100
            : std::string(k) == "HeatPercentDamagingGuard" ? 17
                                                           : 34,
            -1, 100);
    add("ScoldHeatPerSecond", "Heat per second", 1.5, 0, 10);
    add("HeatDelayForSpyHit", "Heat: delay after hitting a spy", 2.5, 0, 30);
    add("HeatDelayPassiveGain", "Heat: passive gain delay", 5.0, 0, 30);
    add("HeatDelayAggroPostCover", "Heat: delay after cover", 5.0, 0, 30);
    add("HeatDelayToDecay", "Heat: delay before decay", 1.0, 0, 30);
    add("HeatDecayRate", "Heat: decay rate", 1.35, 0, 10);
    for (auto it = props.begin(); it != props.end(); ++it) {
        const auto &key = it.key();
        std::string category = "gameplay";
        if (key == "ServerName" || key == "ServerRegion" || key == "Password" || key == "bIsPublic")
            category = "identity";
        else if (key == "GamePort" || key == "QueryPort" || key == "bEnableUPnP" || key == "bCrossplay")
            category = "network";
        else if (key.starts_with("Bots") || key == "bFillWithBots")
            category = "bots";
        else if (key == "MapRotation" || key == "bRandomizeMap")
            category = "maps";
        else if (key.starts_with("Heat") || key == "ScoldHeatPerSecond")
            category = "heat";
        it.value()["category"] = category;
        it.value()["displayName"] = it.value()["description"];
        it.value()["displayNameKey"] = "server.settings." + key;
    }
    return {{"type", "object"}, {"properties", props}};
}
Management::Management(fs::path root, std::vector<Manifest> manifests)
    : root(std::move(root)), manifests(std::move(manifests)) {
    auto game = this->root.parent_path().parent_path().parent_path();
#ifdef _WIN32
    const auto platform_config = "WindowsServer";
#else
    const auto platform_config = "LinuxServer";
#endif
    ini = game / "Saved" / "Config" / platform_config / "TripwireServer.ini";
    profile = game / "CommunityBalanceProfile.json";
    config_active = ini_values(read_file(ini, 262144));
    selection_active = selection_read()["saved"];
    const auto defaults = game / "Community Balance Template" / "CommunityBalanceProfile.default.json";
    if (fs::exists(defaults))
        catalog = profile_entries(document(defaults));
    if (fs::exists(profile))
        balance_active = profile_entries(document(profile));
    else
        balance_active = Json::object();
}
Json Management::config_read() {
    auto values = ini_values(read_file(ini, 262144));
    auto metadata = locale::presentation(root / "Admin" / "presentation.json");
    auto schema = locale::apply_presentation(config_schema(), metadata.value("serverConfig", Json::object()),
                                             metadata.value("categories", Json::object()), "server");
    return {{"schema", schema},
            {"active", config_active},
            {"saved", values},
            {"revision", revision(values)},
            {"restartRequired", values != config_active}};
}
Json Management::config_write(const Json &p) {
    auto current = config_read();
    expected(p, current);
    require(p.size() == 2 && p.contains("values"), "Invalid configuration fields.");
    auto values = p["values"];
    require(values.is_object() && values.size() == config_schema()["properties"].size(),
            "Incomplete configuration.");
    try {
        values = normalize_config(config_schema(), values);
    } catch (...) {
        throw Error("invalid_values", "Configuration values are out of range.");
    }
    for (auto it = values.begin(); it != values.end(); ++it)
        if (it.value().is_string()) {
            const auto &s = it.value().get_ref<const std::string &>();
            require(s.size() <= (it.key() == "ServerName" ? 64u
                                 : it.key() == "Password" ? 128u
                                                          : 24u) &&
                        s.find_first_of("\r\n\"") == s.npos && s.find('\0') == s.npos,
                    "Invalid configuration text.");
            if (it.key() == "ServerName")
                require(!trim(s).empty(), "A name is required.");
        }
    require(values["GamePort"] != values["QueryPort"], "Game and query ports must be different.");
    auto rotation = values["MapRotation"];
    require(!rotation.empty() && rotation.size() <= maps.size(), "Invalid map rotation.");
    std::set<std::string> unique;
    for (auto &map : rotation)
        require(std::find(maps.begin(), maps.end(), map) != maps.end() &&
                    unique.insert(map.get<std::string>()).second,
                "Unknown or duplicate map.");
    auto original = read_file(ini, 262144), text = original;
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (it.key() == "MapRotation") {
            if (current["saved"][it.key()] != it.value())
                text = map_write(text, it.value());
            continue;
        }
        auto value = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
        if (current["saved"][it.key()] != it.value())
            text = ini_write(text, section, it.key(), value);
    }
    if (text != original) {
        write_file(root / "Admin" / "server-configuration.previous.ini", original, true, true);
        write_file(ini, text, true, true);
    }
    return config_read();
}
Json Management::selection_read() {
    Json saved = Json::array();
    if (fs::exists(root / "settings.json"))
        saved = document(root / "settings.json").at("enabledMods");
    else
        for (auto &m : manifests)
            if (m.environment != "client")
                saved.push_back(m.id);
    require(saved.is_array() && saved.size() <= 128, "Invalid mod selection.");
    std::sort(saved.begin(), saved.end());
    return {{"saved", saved},
            {"active", selection_active.is_null() ? saved : selection_active},
            {"revision", revision(saved)},
            {"restartRequired", !selection_active.is_null() && saved != selection_active}};
}
Json Management::selection_write(const Json &p) {
    auto current = selection_read();
    expected(p, current);
    require(p.size() == 2 && p.contains("enabledMods") && p["enabledMods"].is_array() &&
                p["enabledMods"].size() <= 128,
            "Invalid mod selection.");
    std::set<std::string> seen;
    std::vector<Manifest> enabled;
    for (auto &id : p["enabledMods"]) {
        require(id.is_string() && seen.insert(id.get<std::string>()).second, "Duplicate or invalid mod.");
        auto it = std::find_if(manifests.begin(), manifests.end(),
                               [&](auto &m) { return m.id == id.get<std::string>(); });
        require(it != manifests.end() && it->environment != "client", "Mod is missing or client-only.");
        enabled.push_back(*it);
    }
    try {
        dependency_order(enabled);
    } catch (...) {
        throw Error("invalid_values", "Missing or incompatible dependencies. Enable them before this mod.");
    }
    auto file = root / "settings.json";
    auto j = fs::exists(file) ? document(file) : Json{{"schemaVersion", 1}};
    auto next = p["enabledMods"];
    std::sort(next.begin(), next.end());
    j["enabledMods"] = next;
    if (next != current["saved"]) {
        if (fs::exists(file))
            write_file(root / "Admin" / "mod-selection.previous.json", read_file(file), true, true);
        write_file(file, j.dump(2) + "\n");
    }
    return selection_read();
}
std::string Management::group(const std::string &table, const std::string &row) {
    for (auto character : {"Ace", "Cavaliere", "Chavez", "Hans", "Larcin", "Octo", "Sasori", "Socialite",
                           "Squire", "Vigil", "Xiu", "Yumi"})
        if (row.starts_with(std::string(character) + "_") ||
            table.starts_with("DT_" + std::string(character) + "_"))
            return character;
    if (table == "DT_Gadgets_Balancing" || row.starts_with("Drone") || row.starts_with("AutoTurret"))
        return "Gadgets";
    if (row.starts_with("Guard") || row.starts_with("SuperGuard") || row.starts_with("UltraGuard") ||
        row.starts_with("DEye"))
        return "PNJ";
    return "Commun";
}
Json Management::balance_read(const std::string &selected) {
    auto doc = fs::exists(profile) ? document(profile)
                                   : Json{{"format", "DeceiveCommunityBalanceProfile"},
                                          {"schemaVersion", 1},
                                          {"overrides", Json::array()}};
    auto saved = profile_entries(doc), all = catalog;
    for (auto it = saved.begin(); it != saved.end(); ++it)
        if (!all.contains(it.key()))
            all[it.key()] = it.value();
    auto metadata =
        locale::presentation(root / "Admin" / "presentation.json").value("balance", Json::object());
    auto descriptor = [&](const std::string &section, const std::string &id, const std::string &prefix) {
        const auto label_id = prefix == "groups" ? locale::balance_group_label_id(id) : id;
        Json out = {{"displayName", locale::humanize(label_id)},
                    {"displayNameKey", "server.balance." + prefix + "." + label_id}};
        if (metadata.contains(section) && metadata[section].contains(id)) {
            auto custom = metadata[section][id];
            for (auto field : {"displayName", "displayNameKey"})
                if (custom.contains(field)) {
                    require(custom[field].is_string() &&
                                custom[field].get_ref<const std::string &>().size() <= 256,
                            "Invalid label.");
                    out[field] = custom[field];
                }
            if (custom.contains("displayName") && !custom.contains("displayNameKey"))
                out["displayNameKey"] = "";
        }
        return out;
    };
    Json group_labels = Json::object();
    Json rows = Json::array();
    std::map<std::string, size_t> groups;
    for (auto it = all.begin(); it != all.end(); ++it) {
        auto x = it.value();
        auto g = group(x["table"], x["row"]);
        ++groups[g];
        group_labels[g] = descriptor("groups", g, "groups");
        if (g != selected)
            continue;
        auto id = it.key();
        auto base = catalog.contains(id) ? catalog[id]["value"] : x["value"];
        x["id"] = id;
        x["default"] = base;
        x["saved"] = saved.contains(id) ? saved[id]["value"] : base;
        x["active"] = balance_active.contains(id) ? balance_active[id]["value"] : base;
        x["editable"] = catalog.contains(id);
        auto field = x["field"].get<std::string>();
        x["presentation"] = descriptor("fields", field, "fields");
        auto full = x["table"].get<std::string>() + "/" + x["row"].get<std::string>() + "/" + field;
        if (metadata.contains("settings") && metadata["settings"].contains(full))
            x["presentation"] = descriptor("settings", full, "settings");
        x["rowPresentation"] = descriptor("rows", x["row"].get<std::string>(), "rows");
        x.erase("value");
        rows.push_back(std::move(x));
    }
    return {{"groupLabels", group_labels}, {"groups", groups},
            {"group", selected},           {"entries", rows},
            {"revision", revision(doc)},   {"restartRequired", saved != balance_active},
            {"available", !all.empty()}};
}
Json Management::balance_write(const Json &p) {
    require(p.size() == 3 && p.contains("group") && p["group"].is_string() && p.contains("changes") &&
                p["changes"].is_array() && p["changes"].size() <= 256,
            "Invalid balancing changes.");
    auto selected = p["group"].get<std::string>();
    auto current = balance_read(selected);
    expected(p, current);
    auto doc = fs::exists(profile) ? document(profile)
                                   : Json{{"format", "DeceiveCommunityBalanceProfile"},
                                          {"schemaVersion", 1},
                                          {"overrides", Json::array()}};
    auto entries = profile_entries(doc);
    std::set<std::string> seen;
    for (auto &change : p["changes"]) {
        require(change.is_object() && change.size() == 2 && change.contains("id") &&
                    change["id"].is_string() && change.contains("value"),
                "Invalid change.");
        auto id = change["id"].get<std::string>();
        require(catalog.contains(id) && seen.insert(id).second, "Unknown or duplicate setting.");
        auto def = catalog[id];
        require(group(def["table"], def["row"]) == selected, "Setting belongs to another character.");
        auto value = change["value"];
        if (def["value"].is_boolean())
            require(value.is_boolean(), "Expected a boolean.");
        else {
            require(value.is_number() && std::isfinite(value.get<double>()) &&
                        std::abs(value.get<double>()) <= 100000000,
                    "Number out of range.");
            auto range = def.value("allowedRange", std::string{});
            std::istringstream stream(range);
            double low{}, high{};
            std::string to, extra;
            require(bool(stream >> low >> to >> high) && to == "to" && !(stream >> extra) && value >= low &&
                        value <= high,
                    "Value is outside the range allowed by the game.");
        }
        if (entries.contains(id))
            entries[id]["value"] = value;
        else
            entries[id] = {
                {"table", def["table"]}, {"row", def["row"]}, {"field", def["field"]}, {"value", value}};
    }
    require(entries.size() <= 2048, "Too many settings.");
    // Preserve original ordering, metadata and unknown fields.
    std::set<std::string> written;
    for (auto &x : doc["overrides"]) {
        auto id = entry_id(x);
        x = entries[id];
        written.insert(id);
    }
    for (auto it = entries.begin(); it != entries.end(); ++it)
        if (!written.contains(it.key()))
            doc["overrides"].push_back(it.value());
    auto encoded = doc.dump(2) + "\n";
    require(encoded.size() <= 1048576, "Profile is too large for the game.");
    if (Json(revision(doc)) != current["revision"]) {
        if (fs::exists(profile))
            write_file(root / "Admin" / "balance.previous.json", read_file(profile, 1048576));
        write_file(profile, encoded);
    }
    return balance_read(selected);
}
Json Management::dispatch(const std::string &op, const Json &p) {
    std::lock_guard lock(mutex);
    if (op == "translations.read") {
        require(p.empty(), "Invalid request.");
        return locale::remote_catalogues(root, manifests);
    }
    if (op == "server.config.read") {
        require(p.empty(), "Invalid request.");
        return config_read();
    }
    if (op == "server.config.write")
        return config_write(p);
    if (op == "mods.selection.read") {
        require(p.empty(), "Invalid request.");
        return selection_read();
    }
    if (op == "mods.selection.write")
        return selection_write(p);
    if (op == "balance.read") {
        require(p.empty() || (p.size() == 1 && p.contains("group") && p["group"].is_string()),
                "Invalid group.");
        return balance_read(p.value("group", std::string{}));
    }
    if (op == "balance.write")
        return balance_write(p);
    if (op == "server.logs") {
        require(p.size() == 1 && p.contains("source") && p["source"].is_string(), "Invalid log source.");
        auto source = p["source"].get<std::string>();
        require(source == "framework" || source == "game", "Unknown log source.");
        auto path = source == "framework" ? root / "Logs" / "BriefcaseNative.log"
                                          : root.parent_path().parent_path().parent_path() / "Saved" /
                                                "Logs" / "DeceiveInc.log";
        auto out = log_tail(path);
        out["source"] = source;
        return out;
    }
    if (op == "server.restart") {
        require(p.empty(), "Invalid restart request.");
        return schedule_restart(root);
    }
    if (op == "server.shutdown") {
        require(p.empty(), "Invalid shutdown request.");
        return schedule_shutdown(root);
    }
    throw Error("unknown_operation", "Command unavailable.");
}

Json Management::session_status() {
    std::lock_guard lock(mutex);
    const auto path = root.parent_path().parent_path().parent_path() / "Saved" / "Logs" /
                      "DeceiveInc.log";
    const auto tail = log_tail(path, 65536).value("text", std::string{});
    Json result = {{"available", false}};
    const std::map<std::string, std::string> names = {
        {"ServerName", "name"},       {"MapName", "map"},
        {"GameMode", "gameMode"},     {"Region", "region"},
        {"ServerStatus", "state"},    {"ServerVersion", "version"},
        {"CurrentPlayers", "players"}, {"NumPublicConnections", "maxPlayers"},
        {"QueryPort", "queryPort"}};
    constexpr std::string_view marker = "EOS_SessionModification_AddAttribute() named (";
    std::istringstream lines(tail);
    std::string line;
    while (std::getline(lines, line)) {
        const auto begin = line.find(marker);
        if (begin == std::string::npos)
            continue;
        const auto key_begin = begin + marker.size();
        const auto middle = line.find(") with value (", key_begin);
        const auto end = line.rfind(')');
        if (middle == std::string::npos || end == std::string::npos || end <= middle + 14)
            continue;
        const auto key = line.substr(key_begin, middle - key_begin);
        const auto known = names.find(key);
        if (known == names.end())
            continue;
        auto value = line.substr(middle + 14, end - middle - 14);
        if (value.size() > 256)
            continue;
        if (key == "CurrentPlayers" || key == "NumPublicConnections" || key == "QueryPort") {
            try {
                size_t consumed{};
                const auto number = std::stoll(value, &consumed);
                const auto limit = key == "QueryPort" ? 65535 : 128;
                if (consumed == value.size() && number >= 0 && number <= limit)
                    result[known->second] = number;
            } catch (...) {
            }
        } else {
            result[known->second] = std::move(value);
        }
    }
    result["available"] = result.contains("state") || result.contains("players");
    return result;
}
Json log_tail(const fs::path &path, size_t maximum) {
    assert_plain_path(path);
    require(maximum > 0 && maximum <= 65536, "Invalid log limit.");
    bool truncated = false;
#ifdef _WIN32
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return {{"text", "Log unavailable for this startup."}, {"truncated", false}};
    struct Close {
        HANDLE h;
        ~Close() { CloseHandle(h); }
    } close{h};
    BY_HANDLE_FILE_INFORMATION info{};
    require(GetFileInformationByHandle(h, &info) && info.nNumberOfLinks == 1 &&
                !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)),
            "Redirected log.");
    LARGE_INTEGER size{}, offset{};
    require(GetFileSizeEx(h, &size) && size.QuadPart >= 0, "Invalid log size.");
    offset.QuadPart = std::max<LONGLONG>(0, size.QuadPart - maximum);
    SetFilePointerEx(h, offset, nullptr, FILE_BEGIN);
    std::string text(size_t(size.QuadPart - offset.QuadPart), 0);
    DWORD n{};
    require(ReadFile(h, text.data(), DWORD(text.size()), &n, nullptr), "Unable to read the log.");
    text.resize(n);
    truncated = offset.QuadPart != 0;
#else
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return {{"text", "Log unavailable for this startup."}, {"truncated", false}};
    struct Close { int fd; ~Close() { ::close(fd); } } close{fd};
    struct stat info{};
    require(fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 && info.st_size >= 0,
            "Redirected or invalid log.");
    const auto offset = std::max<off_t>(0, info.st_size - off_t(maximum));
    truncated = offset != 0;
    std::string text(size_t(info.st_size - offset), 0);
    size_t total = 0;
    while (total < text.size()) {
        const auto count = pread(fd, text.data() + total, text.size() - total, offset + off_t(total));
        if (count < 0 && errno == EINTR) continue;
        require(count >= 0, "Unable to read the log.");
        if (!count) break; // Log may have been truncated during the read.
        total += size_t(count);
    }
    text.resize(total);
#endif
    if (truncated) {
        auto nl = text.find('\n');
        text = nl == text.npos ? std::string{} : text.substr(nl + 1);
    }
    std::istringstream lines(text);
    std::string line, out;
    while (std::getline(lines, line)) {
        auto lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (lower.find("password") != lower.npos || lower.find("protectedkey") != lower.npos ||
            lower.find("mot de passe") != lower.npos || lower.find("authorization:") != lower.npos ||
            lower.find("bearer ") != lower.npos)
            line = "[sensitive content redacted]";
        out += line + "\n";
    }
    // Replace malformed UTF-8 in legacy game logs before sending JSON.
    out = Json::parse(Json(out).dump(-1, ' ', false, Json::error_handler_t::replace)).get<std::string>();
    return {{"text", out}, {"truncated", truncated}};
}
} // namespace bc::admin
