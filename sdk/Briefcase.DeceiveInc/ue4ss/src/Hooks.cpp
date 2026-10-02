#include <Briefcase/DeceiveInc/Hooks.hpp>

#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <stdexcept>
#include <utility>

namespace briefcase::deceive {
namespace {
using namespace RC::Unreal;

UFunction *required(const wchar_t *path) {
    auto *function = UObjectGlobals::StaticFindObject<UFunction *>(nullptr, nullptr, path);
    if (!function)
        throw std::runtime_error("Required Deceive Inc hook target is unavailable");
    return function;
}

auto no_op = [](UnrealScriptFunctionCallableContext &, void *) {};
} // namespace

FunctionHook::FunctionHook(FunctionHook &&other) noexcept
    : function_(std::exchange(other.function_, nullptr)), pre_(std::exchange(other.pre_, 0)),
      post_(std::exchange(other.post_, 0)) {}

FunctionHook &FunctionHook::operator=(FunctionHook &&other) noexcept {
    if (this != &other) {
        reset();
        function_ = std::exchange(other.function_, nullptr);
        pre_ = std::exchange(other.pre_, 0);
        post_ = std::exchange(other.post_, 0);
    }
    return *this;
}

FunctionHook::~FunctionHook() { reset(); }

void FunctionHook::reset() noexcept {
    if (!function_)
        return;
    try {
        UObjectGlobals::UnregisterHook(static_cast<UFunction *>(function_), {pre_, post_});
    } catch (...) {
    }
    function_ = nullptr;
    pre_ = post_ = 0;
}

FunctionHook hook_reduce_stamina(StaminaCallback before, StaminaCallback after) {
    auto *function = required(STR("/Script/DeceiveInc.Spy:ReduceStamina"));
    auto pre = before ? UnrealScriptFunctionCallable{[callback = std::move(before)](
                                                        UnrealScriptFunctionCallableContext &context,
                                                        void *) {
                             struct Params {
                                 float Delta;
                             };
                             auto &params = context.GetParams<Params>();
                             callback(Spy{context.Context}, params.Delta);
                         }}
                      : UnrealScriptFunctionCallable{no_op};
    auto post = after ? UnrealScriptFunctionCallable{[callback = std::move(after)](
                                                         UnrealScriptFunctionCallableContext &context,
                                                         void *) {
                              struct Params {
                                  float Delta;
                              };
                              auto &params = context.GetParams<Params>();
                              callback(Spy{context.Context}, params.Delta);
                          }}
                       : UnrealScriptFunctionCallable{no_op};
    const auto ids = UObjectGlobals::RegisterHook(function, std::move(pre), std::move(post), nullptr);
    return {function, ids.first, ids.second};
}

FunctionHook hook_reset_stamina(SpyCallback after) {
    auto *function = required(STR("/Script/DeceiveInc.Spy:ResetStaminaToMax"));
    auto post = after ? UnrealScriptFunctionCallable{[callback = std::move(after)](
                                                         UnrealScriptFunctionCallableContext &context,
                                                         void *) { callback(Spy{context.Context}); }}
                       : UnrealScriptFunctionCallable{no_op};
    const auto ids = UObjectGlobals::RegisterHook(function, no_op, std::move(post), nullptr);
    return {function, ids.first, ids.second};
}

} // namespace briefcase::deceive
