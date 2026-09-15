#pragma once
#include "I18n.hpp"
#include <array>
namespace bc::menu {
struct MapName {
    const char *id, *name;
};
inline constexpr std::array<MapName, 7> map_names{{{"DI_Hardsell", "Hard Sell"},
                                                   {"DI_SR", "Silver Reef"},
                                                   {"DI_DS", "Diamond Spire"},
                                                   {"DI_FS", "Fragrant Shore"},
                                                   {"DI_SE", "Sound Eclipse"},
                                                   {"DI_FSN", "Fragrant Shore (Night)"},
                                                   {"DI_HSD", "Hard Sell (Morning)"}}};
inline std::string map_name(const std::string &id) {
    for (auto &map : map_names)
        if (id == map.id)
            return i18n::tr("server.maps." + id, map.name);
    return id;
}
inline std::string map_rotation_names(const nlohmann::json &values) {
    std::string result;
    for (auto &value : values) {
        if (!result.empty())
            result += ", ";
        result += map_name(value.get<std::string>());
    }
    return result.empty() ? "—" : result;
}
} // namespace bc::menu
