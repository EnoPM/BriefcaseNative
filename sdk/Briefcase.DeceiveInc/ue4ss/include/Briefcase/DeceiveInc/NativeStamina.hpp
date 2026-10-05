#pragma once

#include <Briefcase/DeceiveInc/Spy.hpp>
#include <functional>
#include <memory>

namespace briefcase::deceive {

using NativeStaminaCallback = std::function<void(Spy, float &)>;

// Hooks the native ReduceStamina implementation used by direct C++ gameplay
// calls. The SDK resolves it from the reflected Unreal thunk and validates the
// argument setup before installing the detour; no game-build RVA is required.
class NativeStaminaHook final {
  public:
    NativeStaminaHook() noexcept;
    NativeStaminaHook(const NativeStaminaHook &) = delete;
    NativeStaminaHook &operator=(const NativeStaminaHook &) = delete;
    NativeStaminaHook(NativeStaminaHook &&) noexcept;
    NativeStaminaHook &operator=(NativeStaminaHook &&) noexcept;
    ~NativeStaminaHook();

    [[nodiscard]] explicit operator bool() const noexcept { return implementation_ != nullptr; }
    void reset() noexcept;

  private:
    struct Implementation;
    explicit NativeStaminaHook(std::unique_ptr<Implementation>) noexcept;
    std::unique_ptr<Implementation> implementation_;
    friend NativeStaminaHook hook_native_reduce_stamina(NativeStaminaCallback,
                                                         NativeStaminaCallback);
};

[[nodiscard]] NativeStaminaHook hook_native_reduce_stamina(
    NativeStaminaCallback before, NativeStaminaCallback after = {});

// Observes direct native resets, including calls that bypass ProcessEvent.
class NativeResetStaminaHook final {
  public:
    NativeResetStaminaHook() noexcept;
    NativeResetStaminaHook(const NativeResetStaminaHook &) = delete;
    NativeResetStaminaHook &operator=(const NativeResetStaminaHook &) = delete;
    NativeResetStaminaHook(NativeResetStaminaHook &&) noexcept;
    NativeResetStaminaHook &operator=(NativeResetStaminaHook &&) noexcept;
    ~NativeResetStaminaHook();

    [[nodiscard]] explicit operator bool() const noexcept { return implementation_ != nullptr; }
    void reset() noexcept;

  private:
    struct Implementation;
    explicit NativeResetStaminaHook(std::unique_ptr<Implementation>) noexcept;
    std::unique_ptr<Implementation> implementation_;
    friend NativeResetStaminaHook hook_native_reset_stamina(std::function<void(Spy)>);
};

[[nodiscard]] NativeResetStaminaHook hook_native_reset_stamina(std::function<void(Spy)> after);

} // namespace briefcase::deceive
