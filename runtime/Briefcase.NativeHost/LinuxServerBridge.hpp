#pragma once

namespace bc {
// Called after the headless Unreal backend is ready. The bridge owns a local
// Unix socket and schedules all Unreal access on the game's thread.
void linux_server_bridge_start() noexcept;
void linux_server_bridge_stop() noexcept;
}
