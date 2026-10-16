#pragma once
#include <cstdint>

// Keep in sync with Briefcase.SpectatorMode/SpectatorApi.hpp. This is a
// private extension between the optional Linux mod and the server bridge.
struct BriefcaseLinuxSpectatorApiV1 {
    std::uint32_t version;
    bool (*ready)() noexcept;
    bool (*set_selected)(const char* token, const char* identity, bool selected) noexcept;
    bool (*is_selected)(const char* token, const char* identity) noexcept;
    void (*forget_token)(const char* token) noexcept;
    bool (*any_selected)() noexcept;
};
using BriefcaseGetLinuxSpectatorApiV1 = const BriefcaseLinuxSpectatorApiV1* (*)() noexcept;
