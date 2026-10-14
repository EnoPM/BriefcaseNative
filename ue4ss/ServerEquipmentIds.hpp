#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace briefcase::server {

inline bool asset_id_char(unsigned char ch) noexcept {
    return std::isalnum(ch) || ch == '_';
}

inline std::string agent_id(std::string name) {
    const auto prefix = name.find("DA_AgentData_");
    if (prefix != std::string::npos) name.erase(0, prefix + sizeof("DA_AgentData_") - 1);
    if (name == "SquireSeason4") name = "Squire";
    if (name.empty() || name == "None" || name == "Invalid" || name.size() > 40 ||
        !std::all_of(name.begin(), name.end(), asset_id_char)) return {};
    return name;
}

// The game asset is named BounceMat, while the UI texture and Briefcase's
// balancing catalog call it BouncingMat.
inline std::string_view gadget_id(std::string_view name) noexcept {
    return name == "BounceMat" ? "BouncingMat" : name;
}

inline std::string gadget_asset_id(std::string name) {
    for (const auto prefix : {"DA_GadgetData_", "DA_Gadget_", "Gadget_"}) {
        if (name.starts_with(prefix)) { name.erase(0, std::char_traits<char>::length(prefix)); break; }
    }
    if (name.empty() || name == "None" || name.size() > 48 ||
        !std::all_of(name.begin(), name.end(), asset_id_char)) return {};
    return std::string(gadget_id(name));
}

inline std::optional<int> selection_variant_id(std::string_view name, std::string_view prefix,
                                                std::string_view agent) {
    if (agent.empty()) return std::nullopt;
    const auto expected = std::string(prefix) + std::string(agent) + "_";
    if (!name.starts_with(expected)) return std::nullopt;
    const auto variant = name.substr(expected.size());
    if (variant == "Default") return 0;
    if (variant == "Mod1") return 1;
    if (variant == "Mod2") return 2;
    return std::nullopt;
}

// EPowerupType names are read from Unreal at runtime. Keep their mapping to
// the shipped UI textures explicit so plain names such as Health are not lost.
inline std::string_view powerup_icon_id(std::string_view name) noexcept {
    if (const auto scope = name.rfind("::"); scope != std::string_view::npos)
        name.remove_prefix(scope + 2);
    if (name == "Cover") return "Boost_Cover";
    if (name == "Ammo") return "Maxup_Ammo";
    if (name == "Intel") return "Maxup_Intel";
    if (name == "Hacking") return "Boost_HackSpeed";
    if (name == "Health") return "Maxup_Health";
    if (name == "Expertise") return "Boost_Expertise";
    if (name == "SocialBattery") return "SocialHealthRegen";
    if (name == "Foodie") return "SnacksHealth";
    if (name == "Exfiltrator") return "BriefcaseScan_Duration";
    return {};
}

inline std::string powerup_id(std::string name) {
    if (auto mapped = powerup_icon_id(name); !mapped.empty()) return std::string(mapped);
    if (auto colon = name.rfind("::"); colon != std::string::npos) name.erase(0, colon + 2);
    if (name.empty() || name == "Count" || name == "First" || name == "Last" ||
        name.size() > 48 || !std::all_of(name.begin(), name.end(), asset_id_char)) return {};
    return name;
}

} // namespace briefcase::server
