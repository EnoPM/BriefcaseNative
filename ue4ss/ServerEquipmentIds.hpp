#pragma once

#include <string_view>

namespace briefcase::server {

// The game asset is named BounceMat, while the UI texture and Briefcase's
// balancing catalog call it BouncingMat.
inline std::string_view gadget_id(std::string_view name) noexcept {
    return name == "BounceMat" ? "BouncingMat" : name;
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

} // namespace briefcase::server
