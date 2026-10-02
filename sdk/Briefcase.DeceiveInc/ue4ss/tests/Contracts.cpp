#include <Briefcase/DeceiveInc/Hooks.hpp>
#include <Briefcase/DeceiveInc/MatchState.hpp>
#include <Briefcase/DeceiveInc/NativeStamina.hpp>
#include <Briefcase/DeceiveInc/Paths.hpp>
#include <Briefcase/DeceiveInc/Spy.hpp>
#include <concepts>
#include <type_traits>

static_assert(std::is_copy_constructible_v<briefcase::deceive::Spy>);
static_assert(std::is_nothrow_move_constructible_v<briefcase::deceive::FunctionHook>);
static_assert(!std::is_copy_constructible_v<briefcase::deceive::FunctionHook>);
static_assert(std::is_nothrow_move_constructible_v<briefcase::deceive::NativeStaminaHook>);
static_assert(!std::is_copy_constructible_v<briefcase::deceive::NativeStaminaHook>);
static_assert(std::same_as<decltype(std::declval<briefcase::deceive::Spy>().stamina()), float>);
static_assert(std::same_as<decltype(std::declval<briefcase::deceive::MatchState>().remaining_seconds()),
                           std::int32_t>);

int main() { return 0; }
