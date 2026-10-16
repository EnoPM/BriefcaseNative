#pragma once
#include <string_view>

namespace bc {
// Called after the headless Unreal backend is ready. The bridge owns a local
// Unix socket and schedules all Unreal access on the game's thread.
void linux_server_bridge_start() noexcept;
void linux_server_bridge_stop() noexcept;
void* linux_loaded_mod_symbol(std::string_view id, const char* name) noexcept;
}
