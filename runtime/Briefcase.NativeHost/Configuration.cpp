#include "Configuration.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <regex>
#include <stdexcept>
namespace bc {
using json = nlohmann::json;
json strict_json(std::string_view text, size_t limit) {
    if (text.size() > limit)
        throw std::runtime_error("JSON size limit exceeded");
    std::vector<std::set<std::string>> keys;
    return json::parse(text, [&](int depth, json::parse_event_t e, json &v) {
        if (depth > 16)
            throw std::runtime_error("JSON nesting limit exceeded");
        if (e == json::parse_event_t::object_start)
            keys.emplace_back();
        if (e == json::parse_event_t::key && !keys.back().insert(v.get<std::string>()).second)
            throw std::runtime_error("Duplicate JSON key");
        if (e == json::parse_event_t::object_end)
            keys.pop_back();
        return true;
    });
}
static void valid_value(const json &field, const json &value) {
    const auto type = field.at("type").get<std::string>();
    bool valid = false;
    if (type == "boolean")
        valid = value.is_boolean();
    else if (type == "integer")
        valid =
            value.is_number_integer() && (!value.is_number_unsigned() || value.get<uint64_t>() <= INT64_MAX);
    else if (type == "number")
        valid = value.is_number() && std::isfinite(value.get<double>());
    else if (type == "string")
        valid = value.is_string() && value.get_ref<const std::string &>().size() <= 4096;
    else if (type == "array") {
        valid = value.is_array() && value.size() <= 256 && field.at("items").at("type") == "string";
        if (valid)
            for (const auto &item : value)
                if (!item.is_string() || item.get_ref<const std::string &>().size() > 4096)
                    valid = false;
    } else
        throw std::runtime_error("Unsupported configuration type");
    if (valid && type=="string" && field.contains("pattern")) {
        const auto pattern=field.at("pattern").get<std::string>();
        if(pattern.size()>128)throw std::runtime_error("Configuration pattern too long");
        valid=std::regex_match(value.get_ref<const std::string&>(),std::regex(pattern));
    }
    if (!valid)
        throw std::runtime_error("Configuration type/size mismatch");
    if (field.contains("minimum") && (!value.is_number() || value < field.at("minimum")))
        throw std::runtime_error("Configuration below minimum");
    if (field.contains("maximum") && (!value.is_number() || value > field.at("maximum")))
        throw std::runtime_error("Configuration above maximum");
    if (field.contains("enum")) {
        const auto &values = field.at("enum");
        if (!values.is_array() || values.empty() ||
            std::find(values.begin(), values.end(), value) == values.end())
            throw std::runtime_error("Configuration outside enum");
    }
}
json normalize_config(const json &schema, const json &input) {
    if (schema.at("type") != "object" || !schema.at("properties").is_object() || !input.is_object())
        throw std::runtime_error("Configuration/schema must be objects");
    const auto &props = schema.at("properties");
    if (props.size() > 128)
        throw std::runtime_error("Too many configuration fields");
    for (auto it = input.begin(); it != input.end(); ++it)
        if (!props.contains(it.key()))
            throw std::runtime_error("Unknown configuration key: " + it.key());
    json out = json::object();
    for (auto it = props.begin(); it != props.end(); ++it) {
        const auto &field = it.value();
        if (!field.at("description").is_string())
            throw std::runtime_error("Missing configuration description");
        if (field.contains("minimum") && field.contains("maximum") &&
            field.at("minimum") > field.at("maximum"))
            throw std::runtime_error("Inverted configuration range");
        valid_value(field, field.at("default"));
        const auto &value = input.contains(it.key()) ? input.at(it.key()) : field.at("default");
        try {
            valid_value(field, value);
        } catch (const std::exception &e) {
            throw std::runtime_error(it.key() + ": " + e.what());
        }
        out[it.key()] = value;
    }
    return out;
}
static std::string_view trim(std::string_view s) {
    const auto start = s.find_first_not_of(" \t\r");
    if (start == s.npos)
        return {};
    return s.substr(start, s.find_last_not_of(" \t\r") - start + 1);
}
struct IniLocation {
    size_t begin{}, end{}, insert{};
    bool found{}, section{};
    std::string value;
};
static IniLocation locate(std::string_view text, std::string_view section, std::string_view key) {
    if (text.size() > 1048576 || text.find('\0') != text.npos || section.empty() || key.empty() ||
        section.find_first_of("\r\n[]") != section.npos || key.find_first_of("\r\n=") != key.npos)
        throw std::runtime_error("Invalid INI input");
    IniLocation loc;
    bool active = false;
    for (size_t start = 0; start < text.size();) {
        auto end = text.find('\n', start);
        if (end == text.npos)
            end = text.size();
        auto line = trim(text.substr(start, end - start));
        if (start == 0 && line.starts_with("\xEF\xBB\xBF"))
            line.remove_prefix(3);
        if (line.starts_with("[") && line.ends_with("]")) {
            if (active)
                loc.insert = start;
            active = line.substr(1, line.size() - 2) == section;
            if (active) {
                if (loc.section)
                    throw std::runtime_error("Duplicate INI section");
                loc.section = true;
                loc.insert = end < text.size() ? end + 1 : end;
            }
        } else if (active && !line.empty() && line.front() != ';' && line.front() != '#') {
            auto equal = line.find('=');
            if (equal != line.npos && trim(line.substr(0, equal)) == key) {
                if (loc.found)
                    throw std::runtime_error("Duplicate INI key");
                loc.found = true;
                loc.begin = start;
                loc.end = end;
                if (end > start && text[end - 1] == '\r')
                    --loc.end;
                loc.value = std::string(trim(line.substr(equal + 1)));
            }
        }
        start = end < text.size() ? end + 1 : end;
        if (active)
            loc.insert = start;
    }
    return loc;
}
std::string ini_read(std::string_view text, std::string_view section, std::string_view key) {
    auto loc = locate(text, section, key);
    if (!loc.section || !loc.found)
        throw std::runtime_error("INI section/key missing");
    return loc.value;
}
std::string ini_write(std::string_view text, std::string_view section, std::string_view key,
                      std::string_view value) {
    if (value.find_first_of("\r\n") != value.npos || value.find('\0') != value.npos)
        throw std::runtime_error("Invalid INI value");
    auto loc = locate(text, section, key);
    if (!loc.section)
        throw std::runtime_error("INI section missing");
    std::string out(text);
    if (loc.found) {
        if (loc.value == value)
            return out;
        auto original = text.substr(loc.begin, loc.end - loc.begin);
        auto equal = original.find('=');
        auto valueStart = equal + 1;
        while (valueStart < original.size() && (original[valueStart] == ' ' || original[valueStart] == '\t'))
            ++valueStart;
        auto tail = original.find_last_not_of(" \t");
        out.replace(loc.begin + valueStart, std::max(tail + 1, valueStart) - valueStart, value);
    } else {
        std::string nl = text.find("\r\n") != text.npos ? "\r\n" : "\n";
        const bool atEnd = loc.insert == text.size(), finalNl = text.ends_with("\n");
        std::string added;
        if (loc.insert && text[loc.insert - 1] != '\n')
            added += nl;
        added += std::string(key) + "=" + std::string(value);
        if (!atEnd || finalNl)
            added += nl;
        out.insert(loc.insert, added);
    }
    return out;
}
} // namespace bc
