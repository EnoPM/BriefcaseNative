#pragma once
#include "../Briefcase.NativeHost/Manifest.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
namespace bc::locale {
using Json = nlohmann::json;
using Path = std::filesystem::path;
bool valid_language(const std::string &);
Json read_languages(const Path &, const std::string &prefix = "");
Json remote_catalogues(const Path &, const std::vector<Manifest> &);
std::string humanize(std::string);
// Keep persisted/protocol group IDs compatible; translation IDs and default labels are English.
inline std::string balance_group_label_id(const std::string &id) {
    return id == "Commun" ? "Shared" : id == "PNJ" ? "NPCs" : id;
}
std::string lookup(const Json &catalogues, const std::string &language, const std::string &key,
                   const std::string &fallback);
Json apply_presentation(Json schema, const Json &fields, const Json &categories, const std::string &prefix);
Json presentation(const Path &);
void validate_presentation(const Json &);
void validate_catalogues(const Json &, bool remote);
} // namespace bc::locale
