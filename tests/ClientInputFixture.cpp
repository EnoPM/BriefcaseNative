// Execute the real WndProc/import filters against fake OS state.
// No window, global key injection, cursor movement or modification of the running game.
#include <Windows.h>
#include <array>
#include <iostream>
#include <vector>
static HWND fixture_window = reinterpret_cast<HWND>(uintptr_t(1));
static HWND foreground = fixture_window;
static HWND WINAPI fake_foreground() {
    return foreground;
}
#define GetForegroundWindow fake_foreground
#include "../runtime/Briefcase.Client.Input/Input.cpp"
#undef GetForegroundWindow
static unsigned checks;
static std::array<SHORT, 256> physical{};
static std::vector<std::pair<UINT, WPARAM>> forwarded;
static unsigned transitions;
static void check(bool ok, const char *what) {
    ++checks;
    if (!ok)
        throw std::runtime_error(what);
}
static SHORT WINAPI fake_async(int k) {
    return k > 0 && k < 256 ? physical[k] : 0;
}
static BOOL WINAPI fake_keyboard(PBYTE out) {
    for (int i = 0; i < 256; ++i)
        out[i] = physical[i] ? 0x80 : 0;
    return TRUE;
}
static LRESULT CALLBACK game(HWND, UINT msg, WPARAM key, LPARAM) {
    forwarded.emplace_back(msg, key);
    return 0;
}
static bool saw(UINT msg, WPARAM k) {
    return std::find(forwarded.begin(), forwarded.end(), std::pair{msg, k}) != forwarded.end();
}
static void key(unsigned k, bool down, bool repeat = false) {
    physical[k] = down ? SHORT(0x8000) : 0;
    bc::input::procedure(fixture_window, down ? WM_KEYDOWN : WM_KEYUP, k, repeat ? (LPARAM(1) << 30) : 0);
}
static void reset() {
    using namespace bc::input;
    policy = Policy{};
    open_flag = false;
    capture_flag = false;
    latch_flag = false;
    binding_capture_flag = false;
    swallowed_menu_key = 0;
    menu_key = VK_F6;
    physical.fill(0);
    down.fill(false);
    pressed.fill(false);
    released.fill(false);
    events.clear();
    foreground = fixture_window;
    forwarded.clear();
    transitions = 0;
}
int main() {
    try {
        using namespace bc::input;
        window = fixture_window;
        previous_proc = game;
        changed = [](bool) { ++transitions; };
        original_async = fake_async;
        original_state = fake_async;
        original_keyboard = fake_keyboard;
        original_clip = [](const RECT *) -> BOOL { return TRUE; };
        original_cursor = [](int, int) -> BOOL { return TRUE; };
        original_get_cursor = [](LPPOINT p) -> BOOL {
            *p = {100, 100};
            return TRUE;
        };
        original_shape = [](HCURSOR) -> HCURSOR { return nullptr; };
        reset();
        key(VK_F1, true);
        key(VK_F1, false);
        check(!menu_open() && saw(WM_KEYDOWN, VK_F1) && saw(WM_KEYUP, VK_F1), "old F1 shortcut swallowed");
        key(VK_F6, true);
        key(VK_F6, true, true);
        key(VK_F6, false);
        check(menu_open() && transitions == 1 && !saw(WM_KEYDOWN, VK_F6) && !saw(WM_KEYUP, VK_F6),
              "custom menu shortcut or repeat");
        binding_capture(true);
        key(VK_F6, true);
        key(VK_F6, false);
        check(menu_open() && transitions == 1 && events.size() >= 2,
              "capture shortcut closed menu or did not reach widget");
        binding_capture(false);
        key(VK_F6, true);
        key(VK_F6, false);
        check(!menu_open() && transitions == 2, "custom key did not close");
        key('W', true);
        key('W', false);
        check(saw(WM_KEYDOWN, 'W') && saw(WM_KEYUP, 'W'), "gameplay forwarding after close");
        reset();
        key(VK_F6, true);
        set_menu_key(VK_F7);
        key(VK_F6, false);
        check(!saw(WM_KEYUP, VK_F6), "key changed before release leaked owned release");
        key(VK_F7, true);
        key(VK_F7, false);
        check(!menu_open(), "new binding did not take effect");
        reset();
        key(VK_CONTROL, true);
        key(VK_F6, true);
        key(VK_CONTROL, false);
        key(VK_F6, true, true);
        key(VK_F6, false);
        check(!menu_open() && saw(WM_KEYUP, VK_F6), "modifier release mid-repeat toggled or stranded key");
        for (auto binding : {VK_TAB, VK_F4, int('D')}) {
            reset();
            set_menu_key(binding);
            auto modifier = binding == VK_TAB ? VK_SHIFT : binding == VK_F4 ? VK_MENU : VK_LWIN;
            key(modifier, true);
            key(binding, true);
            check(hooked_async(binding) != 0, "system chord hidden from polling");
            key(binding, false);
            key(modifier, false);
            check(!menu_open() && saw(WM_KEYDOWN, binding) && saw(WM_KEYUP, binding),
                  "system shortcut captured while closed");
            transition(true);
            forwarded.clear();
            key(modifier, true);
            key(binding, true);
            key(binding, false);
            key(modifier, false);
            check(menu_open() && saw(WM_KEYDOWN, binding) && saw(WM_KEYUP, binding),
                  "system shortcut captured while open");
        }
        reset();
        physical[VK_F6] = SHORT(0x8000);
        BYTE state[256]{};
        hooked_keyboard(state);
        check(!hooked_async(VK_F6) && !state[VK_F6], "menu key leaked through polling");
        reset();
        key('W', true);
        key(VK_F6, true);
        key(VK_F6, false);
        check(saw(WM_KEYUP, 'W'), "held movement not released on menu open");
        key('W', false);
        key('A', true);
        key(VK_F6, true);
        key(VK_F6, false);
        check(!hooked_async('A'), "held UI key leaked on menu close");
        key('A', false);
        forwarded.clear();
        key('A', true);
        check(saw(WM_KEYDOWN, 'A'), "released UI key stayed blocked");
        reset();
        transition(true);
        binding_capture(true);
        foreground = nullptr;
        procedure(fixture_window, WM_KILLFOCUS, 0, 0);
        check(menu_open() && !capturing() && !binding_capture_flag, "focus loss did not suspend capture");
        key(VK_F6, true);
        key(VK_F6, false);
        check(menu_open(), "background key closed menu");
        foreground = fixture_window;
        procedure(fixture_window, WM_SETFOCUS, 0, 0);
        key(VK_F6, true);
        key(VK_F6, false);
        check(!menu_open(), "focus return could not close menu");
        reset();
        set_menu_key('K');
        transition(true);
        key('K', true);
        check(!menu_open(), "letter binding did not close menu");
        procedure(fixture_window, WM_CHAR, 'k', LPARAM(MapVirtualKeyW('K', MAPVK_VK_TO_VSC)) << 16);
        check(!saw(WM_CHAR, 'k'), "closing shortcut typed into game");
        key('K', false);
        procedure(fixture_window, WM_CHAR, 'z', LPARAM(MapVirtualKeyW('Z', MAPVK_VK_TO_VSC)) << 16);
        check(saw(WM_CHAR, 'z'), "unrelated text swallowed after close");
        reset();
        set_menu_key(0);
        check(menu_key == VK_F6, "invalid menu binding applied");
        std::cout << "PASS " << checks << " isolated input transition checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
