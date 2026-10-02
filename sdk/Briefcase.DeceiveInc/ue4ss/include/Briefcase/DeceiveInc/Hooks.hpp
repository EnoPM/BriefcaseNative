#pragma once

#include <Briefcase/DeceiveInc/Spy.hpp>
#include <functional>

namespace briefcase::deceive {

class FunctionHook final {
  public:
    FunctionHook() noexcept = default;
    FunctionHook(const FunctionHook &) = delete;
    FunctionHook &operator=(const FunctionHook &) = delete;
    FunctionHook(FunctionHook &&other) noexcept;
    FunctionHook &operator=(FunctionHook &&other) noexcept;
    ~FunctionHook();

    [[nodiscard]] explicit operator bool() const noexcept { return function_ != nullptr; }
    void reset() noexcept;

  private:
    friend FunctionHook hook_reduce_stamina(std::function<void(Spy, float &)>,
                                             std::function<void(Spy, float &)>);
    friend FunctionHook hook_reset_stamina(std::function<void(Spy)>);
    FunctionHook(void *function, int pre, int post) noexcept
        : function_(function), pre_(pre), post_(post) {}

    void *function_{};
    int pre_{};
    int post_{};
};

using StaminaCallback = std::function<void(Spy, float &)>;
using SpyCallback = std::function<void(Spy)>;

[[nodiscard]] FunctionHook hook_reduce_stamina(StaminaCallback before = {},
                                                StaminaCallback after = {});
[[nodiscard]] FunctionHook hook_reset_stamina(SpyCallback after);

} // namespace briefcase::deceive
