#pragma once

#include <cstdint>
#include <string>

namespace RC::Unreal {
class UObject;
}

namespace briefcase::deceive {

enum class MatchPhase : std::uint8_t {
    Unknown = 0xff,
};

// C#-style facade over the reflected Deceive Inc match state. Mods use this
// class instead of repeating property names, parameter buffers and ProcessEvent
// calls. It never owns the UObject.
class MatchState {
  public:
    explicit MatchState(RC::Unreal::UObject *object = nullptr) noexcept : object_(object) {}

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] RC::Unreal::UObject *object() const noexcept { return object_; }
    [[nodiscard]] MatchPhase phase() const;
    [[nodiscard]] std::int32_t remaining_seconds() const;
    void set_remaining_seconds(std::int32_t seconds) const;
    [[nodiscard]] bool is_template() const noexcept;
    [[nodiscard]] std::wstring path() const;

  private:
    RC::Unreal::UObject *object_{};
};

[[nodiscard]] std::uint8_t resolve_match_phase(const wchar_t *enumerator);

} // namespace briefcase::deceive
