#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
namespace bc {
nlohmann::json strict_json(std::string_view text, size_t limit = 65536);
nlohmann::json normalize_config(const nlohmann::json &schema, const nlohmann::json &input);
std::string ini_read(std::string_view text, std::string_view section, std::string_view key);
std::string ini_write(std::string_view text, std::string_view section, std::string_view key,
                      std::string_view value);
} // namespace bc
