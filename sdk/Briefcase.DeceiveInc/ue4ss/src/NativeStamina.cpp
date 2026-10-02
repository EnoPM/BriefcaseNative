#include <Briefcase/DeceiveInc/NativeStamina.hpp>

#ifdef _WIN32
#include "NativeStaminaSites.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <polyhook2/Detour/x64Detour.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <utility>

namespace briefcase::deceive {
namespace {
std::span<const std::uint8_t> game_code() {
    auto *module = reinterpret_cast<std::uint8_t *>(GetModuleHandleW(nullptr));
    if (!module) throw std::runtime_error("Game executable is unavailable");
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096)
        throw std::runtime_error("Invalid game executable header");
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->FileHeader.NumberOfSections > 32 || nt->OptionalHeader.SizeOfImage < 4096)
        throw std::runtime_error("Invalid game executable layout");
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto &section = IMAGE_FIRST_SECTION(nt)[i];
        if (std::memcmp(section.Name, ".text", 5) != 0) continue;
        const auto start = section.VirtualAddress;
        const auto size = section.Misc.VirtualSize;
        if (start > nt->OptionalHeader.SizeOfImage ||
            size > nt->OptionalHeader.SizeOfImage - start || !size)
            throw std::runtime_error("Invalid game code section");
        return {module + start, size};
    }
    throw std::runtime_error("Game code section is missing");
}

std::uintptr_t native_reduce_stamina_address() {
    auto *function = RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UFunction *>(
        nullptr, nullptr, STR("/Script/DeceiveInc.Spy:ReduceStamina"));
    if (!function || !function->GetFuncPtr())
        throw std::runtime_error("Reflected Spy:ReduceStamina is unavailable");
    const auto text = game_code();
    return detail::find_native_reduce_stamina(
        text, reinterpret_cast<std::uintptr_t>(text.data()),
        reinterpret_cast<std::uintptr_t>(function->GetFuncPtr()));
}
} // namespace

struct NativeStaminaHook::Implementation {
    using Original = void (*)(RC::Unreal::UObject *, float);
    inline static Implementation *active{};

    NativeStaminaCallback before;
    NativeStaminaCallback after;
    std::uint64_t trampoline{};
    std::unique_ptr<PLH::x64Detour> detour;

    Implementation(NativeStaminaCallback before_callback, NativeStaminaCallback after_callback)
        : before(std::move(before_callback)), after(std::move(after_callback)) {
        if (active)
            throw std::runtime_error("Native ReduceStamina hook is already installed");
        const auto target = native_reduce_stamina_address();

        detour = std::make_unique<PLH::x64Detour>(static_cast<std::uint64_t>(target),
                                                  reinterpret_cast<std::uint64_t>(&dispatch),
                                                  &trampoline);
        active = this;
        if (!detour->hook()) {
            active = nullptr;
            detour.reset();
            throw std::runtime_error("Unable to install native ReduceStamina hook");
        }
    }

    ~Implementation() {
        if (detour)
            detour->unHook();
        if (active == this)
            active = nullptr;
    }

    static void dispatch(RC::Unreal::UObject *object, float delta) noexcept {
        auto *self = active;
        if (!self || !self->trampoline)
            return;
        try {
            if (self->before)
                self->before(Spy{object}, delta);
        } catch (...) {
        }
        reinterpret_cast<Original>(self->trampoline)(object, delta);
        try {
            if (self->after)
                self->after(Spy{object}, delta);
        } catch (...) {
        }
    }
};

NativeStaminaHook::NativeStaminaHook(std::unique_ptr<Implementation> implementation) noexcept
    : implementation_(std::move(implementation)) {}
NativeStaminaHook::NativeStaminaHook() noexcept = default;
NativeStaminaHook::NativeStaminaHook(NativeStaminaHook &&) noexcept = default;
NativeStaminaHook &NativeStaminaHook::operator=(NativeStaminaHook &&) noexcept = default;
NativeStaminaHook::~NativeStaminaHook() = default;
void NativeStaminaHook::reset() noexcept { implementation_.reset(); }

NativeStaminaHook hook_native_reduce_stamina(NativeStaminaCallback before,
                                              NativeStaminaCallback after) {
    if (!before)
        throw std::invalid_argument("A native ReduceStamina callback is required");
    return NativeStaminaHook{
        std::make_unique<NativeStaminaHook::Implementation>(std::move(before), std::move(after))};
}

} // namespace briefcase::deceive
#else
#include <stdexcept>
namespace briefcase::deceive {
struct NativeStaminaHook::Implementation {};
NativeStaminaHook::NativeStaminaHook(std::unique_ptr<Implementation>) noexcept {}
NativeStaminaHook::NativeStaminaHook() noexcept = default;
NativeStaminaHook::NativeStaminaHook(NativeStaminaHook &&) noexcept = default;
NativeStaminaHook &NativeStaminaHook::operator=(NativeStaminaHook &&) noexcept = default;
NativeStaminaHook::~NativeStaminaHook() = default;
void NativeStaminaHook::reset() noexcept { implementation_.reset(); }
NativeStaminaHook hook_native_reduce_stamina(NativeStaminaCallback, NativeStaminaCallback) {
    throw std::runtime_error("Native stamina hooks are not available on this platform");
}
} // namespace briefcase::deceive
#endif
