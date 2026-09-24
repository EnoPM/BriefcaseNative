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
    Environment::server, L"DeceiveIncServer-Win64-Shipping.exe", 0x6AAC58E0, 0x05AF8000,
    "366b09006175c3b6bd2c768787b4e0b3d4e06eee2ebed46f851784f2229066fb"};
inline constexpr GameProfile client_profile{
    Environment::client, L"DeceiveInc-Win64-Shipping.exe", 0x6AAC4146, 0x05F07000,
    "4ea95022cc2ab8dd433a964413553a2109c6d5a5daf93aee7414fe299475138f"};
inline const GameProfile *game_profile(std::wstring_view exe, uint32_t timestamp, uint32_t size) {
    for (const auto *p : {&server_profile, &client_profile})
        if (p->executable == exe && p->timestamp == timestamp && p->image_size == size)
            return p;
    return nullptr;
}
inline bool matches_environment(std::string_view declared, Environment current) {
    return declared == "both" || declared == (current == Environment::client ? "client" : "server");
}
} // namespace bc
