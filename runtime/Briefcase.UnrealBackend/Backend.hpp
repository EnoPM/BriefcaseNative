#pragma once
#include <Briefcase/ModApi.h>
#include <cstdint>
#include <string_view>
#include <string>
namespace bc {
void log(std::string_view message) noexcept;
void backend_start(uint32_t game_thread,uint32_t delay_ms=5000);
uint32_t backend_status() noexcept;
double backend_initialization_ms() noexcept;
bool backend_ready() noexcept;
BcResult backend_client_connect(std::string_view endpoint, std::string &error);
BcResult backend_find(uint64_t owner, std::string_view path, BcHandle *out);
BcResult backend_validate(uint64_t owner, BcHandle handle);
BcResult backend_release(uint64_t owner, BcHandle handle);
BcResult backend_post(BcTask task, void *user, uint64_t owner = 0);
void backend_set_shutdown_handler(BcTask callback, void *user);
} // namespace bc
