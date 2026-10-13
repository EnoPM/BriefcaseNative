#pragma once

#include <cstdint>

// The server bridge only transports player actions. The optional spectator mod
// owns selections and gameplay behavior. Both DLLs are loaded by UE4SS in the
// same process, so this versioned C ABI contains no C++ or Unreal-owned types.
struct BriefcaseSpectatorApiV1 {
    std::uint32_t version;
    bool (*ready)() noexcept;
    bool (*set_selected)(void* player, const char* identity, bool selected) noexcept;
    bool (*is_selected)(void* player, const char* identity) noexcept;
    void (*tick_player)(void* player, bool match_active) noexcept;
};

using BriefcaseGetSpectatorApiV1 = const BriefcaseSpectatorApiV1* (*)() noexcept;
