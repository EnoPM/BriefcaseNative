#pragma once
#include <Briefcase/ClientModApi.h>
#include <Windows.h>
#include <array>
#include <vector>
namespace bc::input {
struct Event {
    UINT message;
    WPARAM w;
    LPARAM l;
};
struct Frame {
    bool menu{}, focused{};
    float x{}, y{}, wheel_x{}, wheel_y{};
    std::array<bool, 5> mouse{};
    std::vector<Event> events;
};
using MenuChanged = void (*)(bool);
using Shutdown = void (*)();
bool install(HWND, MenuChanged, Shutdown);
void stop() noexcept;
void request_menu(bool) noexcept;
bool menu_open() noexcept;
bool capturing() noexcept;
void set_menu_key(uint32_t) noexcept;
void binding_capture(bool) noexcept;
Frame frame(uint64_t frame_number, uint32_t width, uint32_t height);
BcResult key(uint32_t, BcKeyState *) noexcept;
} // namespace bc::input
