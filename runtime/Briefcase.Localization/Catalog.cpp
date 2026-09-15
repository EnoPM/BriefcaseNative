#include "Catalog.hpp"
#include "../Briefcase.NativeHost/Configuration.hpp"
#include "../Briefcase.NativeHost/Startup.hpp"
#include <regex>
namespace bc::locale {
bool valid_language(const std::string &s) {
    return std::regex_match(s, std::regex("[a-z]{2,3}(-[A-Za-z0-9]{2,8})?"));
}
void validate_catalogues(const Json &j, bool remote) {
    if (!j.is_object() || j.size() > 32 || j.dump().size() > 524288)
        throw std::runtime_error("Translation catalogue too large");
    for (auto lang = j.begin(); lang != j.end(); ++lang) {
        if (!valid_language(lang.key()) || !lang.value().is_object() || lang.value().size() > 4096)
            throw std::runtime_error("Invalid translation catalogue");
        for (auto text = lang.value().begin(); text != lang.value().end(); ++text) {
            if (text.key().empty() || text.key().size() > 256 || !text.value().is_string() ||
                text.value().get_ref<const std::string &>().size() > 2048 ||
                text.value().get_ref<const std::string &>().find('\0') != std::string::npos)
                throw std::runtime_error("Invalid translation text");
            if (remote && !text.key().starts_with("server.") && !text.key().starts_with("mods."))
                throw std::runtime_error("Remote translation namespace forbidden");
        }
    }
}
Json read_languages(const Path &directory, const std::string &prefix) {
    Json out = Json::object();
    assert_plain_path(directory);
    if (!std::filesystem::exists(directory))
        return out;
    for (auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".json")
            continue;
        auto lang = entry.path().stem().string();
        if (!valid_language(lang))
            continue;
        assert_plain_path(entry.path());
        if (out.size() >= 32)
            throw std::runtime_error("Too many languages");
        auto j = strict_json(read_bounded(entry.path(), 262144), 262144);
        if (!j.is_object() || !j.contains("translations") || !j["translations"].is_object())
            throw std::runtime_error("Language file needs translations");
        for (auto it = j["translations"].begin(); it != j["translations"].end(); ++it)
            out[lang][prefix + it.key()] = it.value();
        if (prefix.empty())
            out[lang]["language.name"] = j.value("name", lang);
    }
    validate_catalogues(out, false);
    return out;
}
Json remote_catalogues(const Path &root, const std::vector<Manifest> &mods) {
    auto out = read_languages(root / "Translations", "server.");
    for (auto &mod : mods)
        if (mod.environment != "client" && !mod.directory.empty()) {
            auto bundle = read_languages(mod.directory / "Translations", "mods." + mod.id + ".");
            for (auto it = bundle.begin(); it != bundle.end(); ++it)
                out[it.key()].update(it.value());
        }
    validate_catalogues(out, true);
    return out;
}
std::string lookup(const Json &catalogues, const std::string &language, const std::string &key,
                   const std::string &fallback) {
    auto find = [&](const std::string &lang) -> const Json * {
        auto l = catalogues.find(lang);
        if (l == catalogues.end() || !l->is_object())
            return nullptr;
        auto v = l->find(key);
        return v != l->end() && v->is_string() ? &*v : nullptr;
    };
    if (auto v = find(language))
        return v->get<std::string>();
    if (auto at = language.find('-'); at != std::string::npos)
        if (auto v = find(language.substr(0, at)))
            return v->get<std::string>();
    if (auto v = find("en"))
        return v->get<std::string>();
    return fallback;
}
std::string humanize(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '_')
            s[i] = ' ';
        else if (i && s[i] >= 'A' && s[i] <= 'Z' && s[i - 1] >= 'a' && s[i - 1] <= 'z') {
            s.insert(i, " ");
            ++i;
        }
    }
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z')
        s[0] -= 32;
    return s;
}
Json presentation(const Path &path) {
    if (!std::filesystem::exists(path))
        return Json::object();
    assert_plain_path(path);
    auto j = strict_json(read_bounded(path, 262144), 262144);
    if (!j.is_object())
        throw std::runtime_error("Invalid presentation metadata");
    return j;
}
void validate_presentation(const Json &j) {
    if (!j.is_object())
        throw std::runtime_error("Invalid presentation object");
    for (auto name :
         {"displayName", "displayNameKey", "category", "categoryLabel", "categoryKey", "descriptionKey"})
        if (j.contains(name) &&
            (!j[name].is_string() || j[name].get_ref<const std::string &>().size() > 256 ||
             j[name].get_ref<const std::string &>().find('\0') != std::string::npos))
            throw std::runtime_error("Invalid presentation text");
}
Json apply_presentation(Json schema, const Json &fields, const Json &categories, const std::string &prefix) {
    if (!fields.is_object() || !categories.is_object())
        throw std::runtime_error("Invalid presentation sections");
    for (auto it = schema["properties"].begin(); it != schema["properties"].end(); ++it) {
        auto &field = it.value();
        auto key = it.key();
        if (!field.contains("displayName"))
            field["displayName"] = humanize(key);
        if (!field.contains("displayNameKey"))
            field["displayNameKey"] = prefix + ".settings." + key;
        if (!field.contains("category"))
            field["category"] = "general";
        if (fields.contains(key)) {
            const auto &data = fields.at(key);
            validate_presentation(data);
            if (!data.is_object())
                throw std::runtime_error("Invalid setting presentation");
            for (auto name : {"displayName", "displayNameKey", "category", "descriptionKey"})
                if (data.contains(name)) {
                    if (!data[name].is_string() || data[name].get_ref<const std::string &>().size() > 256)
                        throw std::runtime_error("Invalid display name");
                    field[name] = data[name];
                    // A literal override takes priority unless a translation key was explicitly supplied.
                    if (std::string(name) == "displayName" && !data.contains("displayNameKey"))
                        field["displayNameKey"] = "";
                }
        }
        auto category = field["category"].get<std::string>();
        field["categoryLabel"] = humanize(category);
        field["categoryKey"] = prefix + ".categories." + category;
        if (categories.contains(category)) {
            auto &data = categories[category];
            validate_presentation(data);
            for (auto name : {"displayName", "displayNameKey"})
                if (data.contains(name)) {
                    if (!data[name].is_string() || data[name].get_ref<const std::string &>().size() > 256)
                        throw std::runtime_error("Invalid category label");
                    field[std::string(name) == "displayName" ? "categoryLabel" : "categoryKey"] = data[name];
                }
            if (data.contains("displayName") && !data.contains("displayNameKey"))
                field["categoryKey"] = "";
        }
    }
    return schema;
}
} // namespace bc::locale
