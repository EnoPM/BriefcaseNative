#pragma once
#include <cstdint>
#include <string_view>
namespace bc {
enum class Environment { server, client };
struct GameProfile {
    Environment environment;
    std::wstring_view executable;
    uint32_t timestamp, image_size;
    std::string_view sha256;
};
inline constexpr GameProfile server_profile{
    Environment::server, L"DeceiveIncServer-Win64-Shipping.exe", 0x6AB71EA7, 0x05AFE000,
    "f2125f09cbeb7922a4912706cc546477454ce229c15ed477af2731a21c828fd3"};
inline constexpr GameProfile smoketest_server_profile{
    Environment::server, L"DeceiveIncServer-Win64-Shipping.exe", 0x6AC59711, 0x05B06000,
    "0fec2be320ce099f088316c569a24c91974c518cd4b75479bf069d6b14d29d59"};
inline constexpr GameProfile client_profile{
    Environment::client, L"DeceiveInc-Win64-Shipping.exe", 0x6AAC4146, 0x05F07000,
    "4ea95022cc2ab8dd433a964413553a2109c6d5a5daf93aee7414fe299475138f"};
inline const GameProfile *game_profile(std::wstring_view exe, uint32_t timestamp, uint32_t size) {
    for (const auto *p : {&server_profile, &smoketest_server_profile, &client_profile})
        if (p->executable == exe && p->timestamp == timestamp && p->image_size == size)
            return p;
    return nullptr;
}
inline bool matches_environment(std::string_view declared, Environment current) {
    return declared == "both" || declared == (current == Environment::client ? "client" : "server");
}
} // namespace bc
