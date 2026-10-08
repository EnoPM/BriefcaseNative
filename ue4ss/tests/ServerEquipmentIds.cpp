#include "../ServerEquipmentIds.hpp"
#include <array>
#include <string>
#include <string_view>
#include <utility>

int main() {
    using briefcase::server::gadget_id;
    using briefcase::server::powerup_icon_id;
    if (gadget_id("BounceMat") != "BouncingMat" || gadget_id("Drone") != "Drone") return 1;
    constexpr std::array<std::pair<std::string_view, std::string_view>, 9> cases{{
        {"Cover", "Boost_Cover"}, {"Ammo", "Maxup_Ammo"},
        {"Intel", "Maxup_Intel"}, {"Hacking", "Boost_HackSpeed"},
        {"Health", "Maxup_Health"}, {"Expertise", "Boost_Expertise"},
        {"SocialBattery", "SocialHealthRegen"}, {"Foodie", "SnacksHealth"},
        {"Exfiltrator", "BriefcaseScan_Duration"}
    }};
    for (const auto& [game_name, icon_name] : cases) {
        if (powerup_icon_id(game_name) != icon_name ||
            powerup_icon_id(std::string("EPowerupType::") + std::string(game_name)) != icon_name)
            return 2;
    }
    if (!powerup_icon_id("UnknownFuturePowerup").empty()) return 3;
}
