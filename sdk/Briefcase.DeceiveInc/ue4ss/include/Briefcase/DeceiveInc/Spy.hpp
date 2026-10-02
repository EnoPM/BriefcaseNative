#pragma once

#include <cstdint>
#include <string>

namespace RC::Unreal {
class UObject;
}

namespace briefcase::deceive {

// Typed, non-owning view of /Script/DeceiveInc.Spy. Game-specific names and
// reflected call layouts live here so individual mods do not duplicate them.
class Spy {
  public:
    explicit Spy(RC::Unreal::UObject *object = nullptr) noexcept : object_(object) {}

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] RC::Unreal::UObject *object() const noexcept { return object_; }
    [[nodiscard]] bool is_template() const noexcept;
    [[nodiscard]] bool is_authoritative_player() const;
    [[nodiscard]] std::uint8_t role() const;
    [[nodiscard]] float stamina() const;
    [[nodiscard]] float cover_ratio() const;
    [[nodiscard]] float stamina_drain_multiplier() const;
    void set_stamina_drain_multiplier(float value) const;
    [[nodiscard]] bool run_drain_enabled() const;
    void set_run_drain_enabled(bool enabled) const;
    [[nodiscard]] std::wstring path() const;

  private:
    RC::Unreal::UObject *object_{};
};

} // namespace briefcase::deceive
