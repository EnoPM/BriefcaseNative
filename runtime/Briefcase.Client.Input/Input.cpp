#include "Input.hpp"
#include "InputPolicy.hpp"
#include "Keys.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
namespace bc::input {
static std::atomic<HWND> window;
static WNDPROC previous_proc;
static MenuChanged changed;
static Shutdown shutdown_callback;
static std::mutex mutex;
static Policy policy;
static std::atomic<uint32_t> menu_key{default_menu_key}, swallowed_menu_key{};
static std::atomic<bool> binding_capture_flag{};
static std::atomic<bool> open_flag{}, capture_flag{}, latch_flag{};
static std::array<bool, 256> down{}, pressed{}, released{};
static std::array<BcKeyState, 256> keys{};
static std::vector<Event> events;
static float wheel_x{}, wheel_y{};
static RECT saved_clip{};
static bool saved_clip_valid{}, clip_requested{}, requested_unclip{};
static RECT requested_clip{};
static POINT saved_cursor{};
static POINT game_cursor{};
static HCURSOR saved_cursor_shape{};
static bool cursor_saved{};
static constexpr UINT menu_message = WM_APP + 0x4BC;
static auto original_async = &GetAsyncKeyState;
static auto original_state = &GetKeyState;
static auto original_keyboard = &GetKeyboardState;
static auto original_clip = &ClipCursor;
static auto original_cursor = &SetCursorPos;
static auto original_get_cursor = &GetCursorPos;
static auto original_shape = &SetCursor;
static HCURSOR requested_shape{};
static bool shape_requested{};
struct Import {
    void **slot;
    void *original;
    void *replacement;
};
static std::vector<Import> imports;
static bool unmodified() {
    return !((original_async(VK_MENU) | original_async(VK_SHIFT) | original_async(VK_CONTROL) |
              original_async(VK_LWIN) | original_async(VK_RWIN)) &
             0x8000);
}
static bool any_latched() {
    // Only used during a transition or release, never in the ordinary closed fast path.
    for (unsigned k = 1; k < 256; ++k)
        if (policy.suppressed(k, (original_async(int(k)) & 0x8000) != 0))
            return true;
    return false;
}
static bool filtered(int key, bool physical) {
    if (key < 1 || key > 255)
        return false;
    if (uint32_t(key) == menu_key && unmodified())
        return true;
    if (system_key(unsigned(key)))
        return false;
    if (open_flag.load(std::memory_order_relaxed))
        return true;
    if (!latch_flag.load(std::memory_order_relaxed))
        return false;
    std::lock_guard lock(mutex);
    const bool result = policy.suppressed(unsigned(key), physical);
    if (!physical)
        latch_flag = any_latched();
    return result;
}
static SHORT WINAPI hooked_async(int key) {
    const auto value = original_async(key);
    return filtered(key, (value & 0x8000) != 0) ? 0 : value;
}
static SHORT WINAPI hooked_state(int key) {
    const auto value = original_state(key);
    if (!open_flag && !latch_flag && uint32_t(key) != menu_key)
        return value;
    return filtered(key, (original_async(key) & 0x8000) != 0) ? SHORT(value & 1) : value;
}
static BOOL WINAPI hooked_keyboard(PBYTE state) {
    const auto result = original_keyboard(state);
    if (result && state && !open_flag && !latch_flag) {
        const auto key = menu_key.load();
        if (filtered(int(key), (original_async(int(key)) & 0x8000) != 0))
            state[key] &= 1;
    }
    if (result && state && (open_flag || latch_flag))
        for (int k = 1; k < 256; ++k)
            if (filtered(k, (original_async(k) & 0x8000) != 0))
                state[k] &= 1;
    return result;
}
static BOOL WINAPI hooked_clip(const RECT *rect) {
    if (!open_flag.load(std::memory_order_relaxed))
        return original_clip(rect);
    {
        std::lock_guard lock(mutex);
        clip_requested = true;
        requested_unclip = rect == nullptr;
        if (rect)
            requested_clip = *rect;
    }
    // Suppress only the game's import, never calls by another application or overlay DLL.
    if (capture_flag)
        return original_clip(nullptr);
    return TRUE;
}
static BOOL WINAPI hooked_cursor(int x, int y) {
    if (!open_flag.load(std::memory_order_relaxed))
        return original_cursor(x, y);
    std::lock_guard lock(mutex);
    game_cursor = {x, y};
    return TRUE;
}
static BOOL WINAPI hooked_get_cursor(LPPOINT point) {
    if (!open_flag.load(std::memory_order_relaxed) || !point)
        return original_get_cursor(point);
    std::lock_guard lock(mutex);
    *point = game_cursor;
    return TRUE;
}
static HCURSOR WINAPI hooked_shape(HCURSOR shape) {
    if (!open_flag.load(std::memory_order_relaxed))
        return original_shape(shape);
    {
        std::lock_guard lock(mutex);
        requested_shape = shape;
        shape_requested = true;
    }
    return capture_flag ? original_shape(nullptr) : nullptr;
}
static void refresh_capture(bool focused) {
    policy.focus(focused);
    capture_flag = policy.capturing();
}
static void transition(bool value) {
    std::vector<unsigned> releases;
    RECT clip{};
    bool restore_clip = false, unclip = false;
    POINT cursor{};
    HCURSOR shape{};
    bool restore_cursor = false;
    {
        std::lock_guard lock(mutex);
        if (policy.open() == value)
            return;
        policy.menu(value);
        const bool focused = GetForegroundWindow() == window;
        refresh_capture(focused);
        open_flag = value;
        if (value) {
            saved_clip_valid = GetClipCursor(&saved_clip) != FALSE;
            clip_requested = false;
            shape_requested = false;
            cursor_saved = original_get_cursor(&saved_cursor) != FALSE;
            game_cursor = saved_cursor;
            CURSORINFO ci{sizeof(ci)};
            saved_cursor_shape = GetCursorInfo(&ci) ? ci.hCursor : nullptr;
            for (unsigned k = 1; k < 256; ++k) {
                const bool forwarded = policy.release_on_open(k);
                if (!system_key(k) &&
                    (forwarded || (k != menu_key.load() && ((original_async(int(k)) & 0x8000) != 0))))
                    releases.push_back(k);
            }
        } else {
            for (unsigned k = 1; k < 256; ++k)
                if (!system_key(k) && k != menu_key.load())
                    policy.suppress_until_release(k, (original_async(int(k)) & 0x8000) != 0);
            latch_flag = any_latched();
            if (focused) {
                restore_clip = saved_clip_valid || clip_requested;
                clip = clip_requested ? requested_clip : saved_clip;
                unclip = clip_requested && requested_unclip;
                restore_cursor = cursor_saved;
                cursor = game_cursor;
                shape = shape_requested ? requested_shape : saved_cursor_shape;
            }
            events.clear();
            pressed.fill(false);
            released.fill(false);
            down.fill(false);
        }
    }
    // Never hold the input mutex across the game's WndProc.
    if (value) {
        for (auto k : releases) {
            UINT msg = WM_KEYUP;
            if (k == VK_LBUTTON)
                msg = WM_LBUTTONUP;
            else if (k == VK_RBUTTON)
                msg = WM_RBUTTONUP;
            else if (k == VK_MBUTTON)
                msg = WM_MBUTTONUP;
            else if (k == VK_XBUTTON1 || k == VK_XBUTTON2)
                msg = WM_XBUTTONUP;
            if (msg == WM_KEYUP) {
                const LPARAM scan = LPARAM(MapVirtualKeyW(k, MAPVK_VK_TO_VSC));
                CallWindowProcW(previous_proc, window, msg, k,
                                1 | (scan << 16) | (LPARAM(1) << 30) | (LPARAM(1) << 31));
            } else
                CallWindowProcW(
                    previous_proc, window, msg,
                    msg == WM_XBUTTONUP ? MAKEWPARAM(0, k == VK_XBUTTON1 ? XBUTTON1 : XBUTTON2) : 0, 0);
        }
        if (capture_flag) {
            original_clip(nullptr);
            original_shape(nullptr);
        }
    } else {
        if (restore_clip)
            original_clip(unclip ? nullptr : &clip);
        if (restore_cursor)
            original_cursor(cursor.x, cursor.y);
        if (restore_cursor)
            original_shape(shape);
    }
    if (changed)
        changed(value);
}
static unsigned mouse_key(UINT msg, WPARAM w) {
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        return VK_LBUTTON;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        return VK_RBUTTON;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        return VK_MBUTTON;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        return HIWORD(w) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
    default:
        return 0;
    }
}
static LRESULT CALLBACK procedure(HWND hwnd, UINT msg, WPARAM w, LPARAM l) noexcept {
    try {
        if (msg == menu_message) {
            transition(w != 0);
            return 0;
        }
        if (msg == WM_CLOSE && shutdown_callback)
            shutdown_callback();
        if (msg == WM_NCDESTROY) {
            transition(false);
            if (shutdown_callback)
                shutdown_callback();
        }
        if (msg == WM_KILLFOCUS || msg == WM_ACTIVATEAPP || msg == WM_SETFOCUS) {
            const bool active = msg == WM_SETFOCUS || (msg == WM_ACTIVATEAPP && w != 0);
            {
                std::lock_guard lock(mutex);
                refresh_capture(active);
                if (!active) {
                    binding_capture_flag = false;
                    swallowed_menu_key = 0;
                }
                down.fill(false);
                pressed.fill(false);
                released.fill(false);
                events.clear();
            }
            if (open_flag && active)
                original_clip(nullptr);
        }
        // TranslateMessage can enqueue a character even when its keydown is consumed.
        // Match the scan code of the owned press, without hiding unrelated text/system chords.
        if ((msg == WM_CHAR || msg == WM_SYSCHAR) && swallowed_menu_key) {
            const auto scan = uint32_t((l >> 16) & 0xff);
            if (scan && scan == (MapVirtualKeyW(swallowed_menu_key, MAPVK_VK_TO_VSC) & 0xff))
                return 0;
        }
        const bool key_down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        const bool key_up = msg == WM_KEYUP || msg == WM_SYSKEYUP;
        const bool keyboard = key_down || key_up;
        const bool alt = keyboard && (original_async(VK_MENU) & 0x8000) != 0;
        const bool shift = keyboard && (original_async(VK_SHIFT) & 0x8000) != 0;
        const bool ctrl = keyboard && (original_async(VK_CONTROL) & 0x8000) != 0;
        const bool win =
            keyboard && ((original_async(VK_LWIN) & 0x8000) != 0 || (original_async(VK_RWIN) & 0x8000) != 0);
        const bool consumed_release = key_up && w == swallowed_menu_key.load();
        const bool menu_event =
            keyboard && menu_key_event(uint32_t(w), menu_key.load(), alt || shift || ctrl || win,
                                       GetForegroundWindow() == window, binding_capture_flag);
        const bool repeat = (l & (LPARAM(1) << 30)) != 0;
        if (consumed_release || (menu_event && key_down && (!repeat || w == swallowed_menu_key.load()))) {
            if (key_down) {
                swallowed_menu_key = uint32_t(w);
                if ((l & (LPARAM(1) << 30)) == 0)
                    transition(!open_flag);
            } else
                swallowed_menu_key = 0;
            {
                std::lock_guard lock(mutex);
                if (key_down && !down[w])
                    pressed[w] = true;
                if (key_up && down[w])
                    released[w] = true;
                down[w] = key_down;
            }
            return 0;
        }
        const bool system =
            (key_down || key_up) && (shortcut(unsigned(w), alt, shift, ctrl, win) || system_key(unsigned(w)));
        bool swallow = false;
        {
            std::lock_guard lock(mutex);
            unsigned key = (key_down || key_up) && w < 256 ? unsigned(w) : mouse_key(msg, w);
            bool is_down = key_down || msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN ||
                           msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN;
            if (key) {
                if (is_down && !down[key])
                    pressed[key] = true;
                if (!is_down && down[key])
                    released[key] = true;
                down[key] = is_down;
                if (!is_down && latch_flag) {
                    policy.suppressed(key, false);
                    latch_flag = any_latched();
                }
            }
            const bool mouse = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST);
            swallow = policy.capturing() && !system &&
                      (key_down || key_up || mouse || msg == WM_CHAR || msg == WM_SYSCHAR || msg == WM_INPUT);
            if (key && !system && latch_flag && policy.suppressed(key, is_down))
                swallow = true;
            if (key && !swallow)
                policy.forwarded(key, is_down);
            if (policy.capturing()) {
                if (msg == WM_MOUSEWHEEL)
                    wheel_y += float(SHORT(HIWORD(w))) / WHEEL_DELTA;
                if (msg == WM_MOUSEHWHEEL)
                    wheel_x += float(SHORT(HIWORD(w))) / WHEEL_DELTA;
                if ((key_down || key_up || msg == WM_CHAR) && events.size() < 512)
                    events.push_back({msg, w, l});
            }
        }
        if (msg == WM_SETCURSOR && capture_flag && LOWORD(l) == HTCLIENT) {
            original_shape(nullptr);
            return TRUE;
        }
        if (swallow) {
            if (msg == WM_INPUT)
                return DefWindowProcW(hwnd, msg, w, l);
            return 0;
        }
    } catch (...) {
    }
    return CallWindowProcW(previous_proc, hwnd, msg, w, l);
}
static void patch_import(const char *name, void *replacement, void **original) {
    auto *base = reinterpret_cast<unsigned char *>(GetModuleHandleW(nullptr));
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    const auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress)
        return;
    auto *descs = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress);
    for (size_t i = 0; i < dir.Size / sizeof(*descs) && descs[i].Name; ++i) {
        if (!descs[i].OriginalFirstThunk)
            continue;
        auto *names = reinterpret_cast<IMAGE_THUNK_DATA64 *>(base + descs[i].OriginalFirstThunk);
        auto *slots = reinterpret_cast<IMAGE_THUNK_DATA64 *>(base + descs[i].FirstThunk);
        for (size_t k = 0; names[k].u1.AddressOfData; ++k) {
            if (IMAGE_SNAP_BY_ORDINAL64(names[k].u1.Ordinal))
                continue;
            auto *entry = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names[k].u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<char *>(entry->Name), name))
                continue;
            auto **slot = reinterpret_cast<void **>(&slots[k].u1.Function);
            DWORD before{}, ignored{};
            if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &before))
                continue;
            if (*slot == replacement) {
                VirtualProtect(slot, sizeof(void *), before, &ignored);
                continue;
            }
            *original = *slot;
            InterlockedExchangePointer(slot, replacement);
            VirtualProtect(slot, sizeof(void *), before, &ignored);
            imports.push_back({slot, *original, replacement});
        }
    }
}
bool install(HWND hwnd, MenuChanged menu_changed, Shutdown shutdown) {
    if (window == hwnd && previous_proc)
        return true;
    if (window)
        stop();
    window = hwnd;
    changed = menu_changed;
    shutdown_callback = shutdown;
    SetLastError(0);
    previous_proc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
    auto prior = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(procedure)));
    if (prior)
        previous_proc = prior;
    else
        previous_proc = nullptr;
    if (!previous_proc) {
        window = nullptr;
        return false;
    }
    {
        std::lock_guard lock(mutex);
        refresh_capture(GetForegroundWindow() == window);
    }
    patch_import("GetAsyncKeyState", reinterpret_cast<void *>(hooked_async),
                 reinterpret_cast<void **>(&original_async));
    patch_import("GetKeyState", reinterpret_cast<void *>(hooked_state),
                 reinterpret_cast<void **>(&original_state));
    patch_import("GetKeyboardState", reinterpret_cast<void *>(hooked_keyboard),
                 reinterpret_cast<void **>(&original_keyboard));
    patch_import("ClipCursor", reinterpret_cast<void *>(hooked_clip),
                 reinterpret_cast<void **>(&original_clip));
    patch_import("SetCursor", reinterpret_cast<void *>(hooked_shape),
                 reinterpret_cast<void **>(&original_shape));
    patch_import("GetCursorPos", reinterpret_cast<void *>(hooked_get_cursor),
                 reinterpret_cast<void **>(&original_get_cursor));
    patch_import("SetCursorPos", reinterpret_cast<void *>(hooked_cursor),
                 reinterpret_cast<void **>(&original_cursor));
    return true;
}
void stop() noexcept {
    try {
        transition(false);
        capture_flag = false;
        binding_capture_flag = false;
        swallowed_menu_key = 0;
        open_flag = false;
        latch_flag = false;
        for (auto &entry : imports) {
            DWORD old{}, ignored{};
            if (*entry.slot == entry.replacement &&
                VirtualProtect(entry.slot, sizeof(void *), PAGE_READWRITE, &old)) {
                InterlockedExchangePointer(entry.slot, entry.original);
                VirtualProtect(entry.slot, sizeof(void *), old, &ignored);
            }
        }
        imports.clear();
        if (window && IsWindow(window) &&
            reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC)) == procedure)
            SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous_proc));
        window = nullptr;
    } catch (...) {
    }
}
void request_menu(bool value) noexcept {
    if (window)
        PostMessageW(window, menu_message, value, 0);
}
bool menu_open() noexcept {
    return open_flag.load(std::memory_order_relaxed);
}
bool capturing() noexcept {
    return capture_flag.load(std::memory_order_relaxed);
}
void set_menu_key(uint32_t key) noexcept {
    if (valid_menu_key(key))
        menu_key.store(key, std::memory_order_relaxed);
}
void binding_capture(bool value) noexcept {
    binding_capture_flag.store(value, std::memory_order_relaxed);
}
Frame frame(uint64_t number, uint32_t width, uint32_t height) {
    Frame out;
    POINT point{};
    RECT client{};
    if (window && capture_flag) {
        GetCursorPos(&point);
        ScreenToClient(window, &point);
        GetClientRect(window, &client);
        out.x = float(point.x) * float(width) / float(std::max<LONG>(1, client.right));
        out.y = float(point.y) * float(height) / float(std::max<LONG>(1, client.bottom));
    }
    std::lock_guard lock(mutex);
    out.menu = policy.open();
    out.focused = GetForegroundWindow() == window;
    refresh_capture(out.focused);
    out.wheel_x = wheel_x;
    out.wheel_y = wheel_y;
    wheel_x = wheel_y = 0;
    out.events.swap(events);
    const unsigned mouse[] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
    for (size_t i = 0; i < 5; ++i)
        out.mouse[i] = out.menu && out.focused && (original_async(mouse[i]) & 0x8000) != 0;
    for (unsigned k = 1; k < 256; ++k)
        keys[k] = {sizeof(BcKeyState), uint32_t(out.focused && down[k]), uint32_t(out.focused && pressed[k]),
                   uint32_t(out.focused && released[k]), number};
    pressed.fill(false);
    released.fill(false);
    return out;
}
BcResult key(uint32_t key, BcKeyState *out) noexcept {
    if (key < 1 || key > 255 || !out || out->size < sizeof(*out))
        return BC_INVALID_ARGUMENT;
    try {
        std::lock_guard lock(mutex);
        *out = keys[key];
        return out->frame_number ? BC_OK : BC_NOT_READY;
    } catch (...) {
        return BC_INTERNAL;
    }
}
} // namespace bc::input
