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
    Environment::server, L"DeceiveIncServer-Win64-Shipping.exe", 0x6A966107, 0x05B60000,
    "78afe1dbeecb09027c274def4f0ac855b447dc52ffe3cd9482c1be4341b0dae6"};
inline constexpr GameProfile client_profile{
    Environment::client, L"DeceiveInc-Win64-Shipping.exe", 0x6A96564B, 0x06283000,
    "b753b51f4d51adc41f7577e7e01521a62f941711ec68330cf22546f81c788a87"};
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
