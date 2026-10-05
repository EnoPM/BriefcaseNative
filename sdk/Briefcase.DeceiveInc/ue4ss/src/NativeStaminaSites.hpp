#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace briefcase::deceive::detail {

// The reflected Spy:ReduceStamina thunk is known through UE4SS. Resolve the
// direct native implementation call from its decoded argument setup instead
// of maintaining a game-build RVA or a byte signature. Reject changed or
// ambiguous thunks rather than guessing a hook address.
[[nodiscard]] std::uintptr_t find_native_reduce_stamina(
    std::span<const std::uint8_t> executable_text, std::uintptr_t text_address,
    std::uintptr_t thunk_address);

// ResetStaminaToMax's reflected entry tail-jumps into its native implementation.
// Resolve that jump rather than relying on a build-specific address.
[[nodiscard]] std::uintptr_t find_native_reset_stamina(
    std::span<const std::uint8_t> executable_text, std::uintptr_t text_address,
    std::uintptr_t thunk_address);

} // namespace briefcase::deceive::detail
