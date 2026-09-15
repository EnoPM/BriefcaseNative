#include "Manifest.hpp"
#include <charconv>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <stdexcept>
namespace bc {
Version version(const std::string &value) {
    Version out{};
    size_t begin = 0;
    for (size_t i = 0; i < 3; ++i) {
        const auto end = value.find('.', begin);
        const auto part = value.substr(begin, end == std::string::npos ? end : end - begin);
        if (part.empty() || (part.size() > 1 && part[0] == '0'))
            throw std::runtime_error("Invalid version: " + value);
        auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), out[i]);
        if (ec != std::errc{} || ptr != part.data() + part.size() || ((i == 2) != (end == std::string::npos)))
            throw std::runtime_error("Invalid version: " + value);
        begin = end + 1;
    }
    return out;
}
static bool valid_id(const std::string &id) {
    return std::regex_match(id, std::regex("[a-z0-9]+([.-][a-z0-9]+)*"));
}
Manifest parse_manifest(const std::string &text) {
    if (text.size() > 65536)
        throw std::runtime_error("Manifest exceeds 64 KiB");
    std::vector<std::set<std::string>> keys;
    auto j = nlohmann::json::parse(
        text, [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json &value) {
            if (depth > 16)
                throw std::runtime_error("Manifest nesting exceeds 16 levels");
            if (event == nlohmann::json::parse_event_t::object_start)
                keys.emplace_back();
            if (event == nlohmann::json::parse_event_t::key &&
                !keys.back().insert(value.get<std::string>()).second)
                throw std::runtime_error("Duplicate JSON key");
            if (event == nlohmann::json::parse_event_t::object_end)
                keys.pop_back();
            return true;
        });
    Manifest m;
    if (!j.at("capabilities").is_array() || !j.at("dependencies").is_array())
        throw std::runtime_error("Capabilities and dependencies must be arrays");
    if (!j.at("schemaVersion").is_number_unsigned() || j.at("schemaVersion").get<uint64_t>() != 1)
        throw std::runtime_error("Unsupported manifest schema");
    m.id = j.at("id").get<std::string>();
    m.name = j.at("name").get<std::string>();
    m.author = j.at("author").get<std::string>();
    m.version = version(j.at("version").get<std::string>());
    m.entry = j.at("entry").get<std::string>();
    m.environment = j.at("environment").get<std::string>();
    if (!valid_id(m.id) || m.name.empty() || m.author.empty())
        throw std::runtime_error("Invalid mod identity");
    if (!std::regex_match(m.entry, std::regex("[A-Za-z0-9_.-]+\\.dll")) ||
        m.entry.find("..") != std::string::npos)
        throw std::runtime_error("Entry must be a DLL filename within its mod directory");
    if (m.environment != "server" && m.environment != "client" && m.environment != "both")
        throw std::runtime_error("Invalid environment");
    if (!j.at("minimumApi").is_number_unsigned() || j.at("minimumApi").get<uint64_t>() != BC_API_VERSION)
        throw std::runtime_error("Incompatible API version");
    m.phase = j.value("loadPhase", std::string("ready"));
    if (m.phase != "startup" && m.phase != "ready")
        throw std::runtime_error("Invalid load phase");
    if (j.contains("iniBindings")) {
        const auto &bindings = j.at("iniBindings");
        if (!bindings.is_object() || bindings.size() > 16)
            throw std::runtime_error("Invalid INI bindings");
        for (auto it = bindings.begin(); it != bindings.end(); ++it) {
            auto file = it.value().get<std::string>();
            if (!valid_id(it.key()) || !std::regex_match(file, std::regex("[A-Za-z0-9_-]+\\.ini")))
                throw std::runtime_error("INI binding must name a file in server config");
            m.ini_bindings.emplace(it.key(), file);
        }
    }
    std::set<std::string> caps;
    for (const auto &c : j.at("capabilities")) {
        auto name = c.get<std::string>();
        if (!caps.insert(name).second)
            throw std::runtime_error("Duplicate capability");
        if (name == "log")
            m.capabilities |= BC_CAP_LOG;
        else if (name == "unreal.find")
            m.capabilities |= BC_CAP_FIND;
        else if (name == "game-thread")
            m.capabilities |= BC_CAP_SCHEDULE;
        else if (name == "config")
            m.capabilities |= BC_CAP_CONFIG;
        else if (name == "startup.immediate")
            m.capabilities |= BC_CAP_STARTUP_PATCH;
        else if (name == "server-config.ini")
            m.capabilities |= BC_CAP_INI;
        else if (name == "unreal.reflection")
            m.capabilities |= BC_CAP_REFLECTION;
        else if (name == "unreal.invoke")
            m.capabilities |= BC_CAP_INVOCATION;
        else if (name == "unreal.hooks")
            m.capabilities |= BC_CAP_HOOKS;
        else if (name == "unreal.write")
            m.capabilities |= BC_CAP_UNREAL_WRITE;
        else if (name == "unreal.lifecycle")
            m.capabilities |= BC_CAP_LIFECYCLE;
        else if (name == "client.render")
            m.capabilities |= BC_CAP_CLIENT_RENDER;
        else if (name == "client.input")
            m.capabilities |= BC_CAP_CLIENT_INPUT;
        else if (name == "native.hooks")
            m.capabilities |= BC_CAP_NATIVE_HOOKS;
        else
            throw std::runtime_error("Unknown capability: " + name);
    }
    if ((m.capabilities & (BC_CAP_INI | BC_CAP_STARTUP_PATCH)) && m.phase != "startup")
        throw std::runtime_error("Startup capabilities require startup phase");
    if (!m.ini_bindings.empty() && !(m.capabilities & BC_CAP_INI))
        throw std::runtime_error("INI binding requires capability");
    std::set<std::string> deps;
    for (const auto &d : j.at("dependencies")) {
        Dependency dep{d.at("id").get<std::string>(), version(d.at("minimumVersion").get<std::string>())};
        if (!valid_id(dep.id) || dep.id == m.id || !deps.insert(dep.id).second)
            throw std::runtime_error("Invalid or duplicate dependency");
        m.dependencies.push_back(std::move(dep));
    }
    return m;
}
std::vector<size_t> dependency_order(const std::vector<Manifest> &mods) {
    std::map<std::string, size_t> ids;
    for (size_t i = 0; i < mods.size(); ++i)
        if (!ids.emplace(mods[i].id, i).second)
            throw std::runtime_error("Duplicate mod id: " + mods[i].id);
    std::vector<int> state(mods.size());
    std::vector<size_t> result;
    std::function<void(size_t)> visit = [&](size_t i) {
        if (state[i] == 2)
            return;
        if (state[i] == 1)
            throw std::runtime_error("Dependency cycle at " + mods[i].id);
        state[i] = 1;
        for (const auto &d : mods[i].dependencies) {
            auto it = ids.find(d.id);
            if (it == ids.end())
                throw std::runtime_error("Missing dependency: " + d.id);
            if (mods[it->second].version < d.minimum)
                throw std::runtime_error("Dependency version too old: " + d.id);
            if (mods[i].phase == "startup" && mods[it->second].phase != "startup")
                throw std::runtime_error("Startup dependency cannot require ready phase");
            visit(it->second);
        }
        state[i] = 2;
        result.push_back(i);
    };
    for (const auto &[id, i] : ids)
        visit(i);
    return result;
}
} // namespace bc
