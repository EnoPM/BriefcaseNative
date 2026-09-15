#include "../Briefcase.Client.Input/Input.hpp"
#include "../Briefcase.Client.Menu/Font.hpp"
#include "../Briefcase.Client.Menu/KeyMap.hpp"
#include "../Briefcase.Client.Menu/KeybindState.hpp"
#include "../Briefcase.Client.Menu/Menu.hpp"
#include "ClientBridge.h"
#include <MinHook.h>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <d3d11.h>
#include <dxgi.h>
#include <format>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace {
using Clock = std::chrono::steady_clock;
using Present = HRESULT(__stdcall *)(IDXGISwapChain *, UINT, UINT);
using Resize = HRESULT(__stdcall *)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using Release = ULONG(__stdcall *)(IUnknown *);
static Present original_present;
static Resize original_resize;
static Release original_release;
static void *present_address{}, *resize_address{}, *release_address{};
static std::recursive_mutex mutex;
static const BcClientHostApi *host{};
static std::atomic<bool> running{}, stopping{};
static std::atomic<uint32_t> state{};
static IDXGISwapChain *active_chain{};
static HWND active_window{};
static ID3D11Device *device{};
static ID3D11DeviceContext *device_context{};
static ID3D11RenderTargetView *target{};
static ImGuiContext *imgui{};
static bool dx11_initialized{};
static std::thread *discovery_worker{};
static unsigned frame_errors{};
static thread_local unsigned draw_commands{};
static BcViewport viewport{sizeof(BcViewport)};
static BcClientMetrics metrics{sizeof(BcClientMetrics)};
static double closed_total{}, open_total{}, dormant_total{};
static Clock::time_point first_present{}, last_present{};
static uint64_t stable_frames{};
static thread_local uint64_t callback_owner{};
static thread_local bool in_present{};
struct Callback {
    uint64_t owner;
    BcHandle id;
    BcRenderCallback callback;
    void *user;
    bool alive = true;
};
static std::array<std::shared_ptr<Callback>, 128> callbacks;
static BcHandle next_callback = 1;
static void log(const std::string &message) noexcept {
    if (host && host->log)
        host->log(message.c_str());
}
static double ms(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
static void release_target() {
    if (target) {
        target->Release();
        target = nullptr;
    }
}
static void release_resources() {
    bc::menu::keybind::cancel();
    bc::input::binding_capture(false);
    active_chain = nullptr;
    if (imgui) {
        ImGui::SetCurrentContext(imgui);
        if (dx11_initialized) {
            ImGui_ImplDX11_Shutdown();
            dx11_initialized = false;
        }
        ImGui::DestroyContext(imgui);
        imgui = nullptr;
    }
    release_target();
    if (device_context) {
        device_context->Release();
        device_context = nullptr;
    }
    if (device) {
        device->Release();
        device = nullptr;
    }
    metrics.context_created = 0;
}
static bool game_chain(IDXGISwapChain *chain, DXGI_SWAP_CHAIN_DESC &desc) {
    if (FAILED(chain->GetDesc(&desc)) || !desc.OutputWindow)
        return false;
    DWORD process{};
    GetWindowThreadProcessId(desc.OutputWindow, &process);
    if (process != GetCurrentProcessId())
        return false;
    wchar_t name[64]{};
    return GetClassNameW(desc.OutputWindow, name, 64) && wcscmp(name, L"UnrealWindow") == 0;
}
static bool render_target(IDXGISwapChain *chain) {
    ID3D11Texture2D *buffer{};
    if (FAILED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&buffer))))
        return false;
    D3D11_TEXTURE2D_DESC desc{};
    buffer->GetDesc(&desc);
    viewport.width = desc.Width;
    viewport.height = desc.Height;
    const auto hr = device->CreateRenderTargetView(buffer, nullptr, &target);
    buffer->Release();
    return SUCCEEDED(hr);
}
static bool initialize_imgui(IDXGISwapChain *chain) {
    const auto resources_start = Clock::now();
    if (FAILED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void **>(&device))))
        return false;
    device->GetImmediateContext(&device_context);
    if (!device_context || !render_target(chain)) {
        release_resources();
        return false;
    }
    auto started = Clock::now();
    IMGUI_CHECKVERSION();
    imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui);
    metrics.context_ms = ms(started);
    metrics.context_created = 1;
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    started = Clock::now();
    const float dpi = std::max(1.f, float(GetDpiForWindow(active_window)) / 96.f);
    viewport.dpi_scale = dpi;
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    std::wstring font = std::wstring(windows) + L"\\Fonts\\segoeui.ttf";
    const int required = WideCharToMultiByte(CP_UTF8, 0, font.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string path(size_t(required), 0);
    WideCharToMultiByte(CP_UTF8, 0, font.c_str(), -1, path.data(), required, nullptr, nullptr);
    auto bold_path = path;
    bold_path.replace(bold_path.rfind("segoeui.ttf"), std::string("segoeui.ttf").size(), "segoeuib.ttf");
    bc::menu::load_fonts(path.c_str(), bold_path.c_str(), 17 * dpi);
    if (!io.Fonts->Build()) {
        release_resources();
        return false;
    }
    metrics.fonts_ms = ms(started);
    bc::menu::style(dpi);
    started = Clock::now();
    dx11_initialized = ImGui_ImplDX11_Init(device, device_context);
    if (!dx11_initialized || !ImGui_ImplDX11_CreateDeviceObjects()) {
        release_resources();
        return false;
    }
    metrics.resources_ms = ms(started);
    log(std::format(
        "ImGui context created lazily: {:.3f} ms; fonts {:.3f} ms; GPU resources {:.3f} ms; total {:.3f} ms",
        metrics.context_ms, metrics.fonts_ms, metrics.resources_ms, ms(resources_start)));
    active_chain = chain;
    if (host->request_unreal)
        host->request_unreal();
    return true;
}
static void feed_input(const bc::input::Frame &input) {
    auto &io = ImGui::GetIO();
    static bool was_open = false, was_focused = false;
    if (was_open != input.menu || was_focused != input.focused) {
        io.ClearInputKeys();
        io.ClearInputMouse();
    }
    was_open = input.menu;
    was_focused = input.focused;
    io.AddFocusEvent(input.focused);
    io.MouseDrawCursor = input.menu && input.focused;
    io.AddMousePosEvent(input.menu && input.focused ? input.x : -FLT_MAX,
                        input.menu && input.focused ? input.y : -FLT_MAX);
    for (int i = 0; i < 5; ++i)
        io.AddMouseButtonEvent(i, input.menu && input.mouse[i]);
    io.AddMouseWheelEvent(input.wheel_x, input.wheel_y);
    for (const auto &e : input.events) {
        if (e.message == WM_CHAR) {
            io.AddInputCharacterUTF16(ImWchar16(e.w));
            continue;
        }
        const auto key = bc::menu::keybind::to_imgui(unsigned(e.w));
        if (key != ImGuiKey_None) {
            const bool down = e.message == WM_KEYDOWN || e.message == WM_SYSKEYDOWN;
            io.AddKeyEvent(key, down);
            if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl)
                io.AddKeyEvent(ImGuiMod_Ctrl, down);
            if (key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift)
                io.AddKeyEvent(ImGuiMod_Shift, down);
            if (key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt)
                io.AddKeyEvent(ImGuiMod_Alt, down);
            if (key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper)
                io.AddKeyEvent(ImGuiMod_Super, down);
        }
    }
}
static void menu_changed(bool open) {
    log(open ? "Menu opened" : "Menu closed; game input restored");
}
static void window_closed() {
    if (host && host->request_shutdown)
        host->request_shutdown();
}
static void draw_frame(const bc::input::Frame &input) {
    ImGui::SetCurrentContext(imgui);
    auto &io = ImGui::GetIO();
    io.DisplaySize = {float(viewport.width), float(viewport.height)};
    io.DeltaTime = float(std::clamp(viewport.delta_seconds, 0.001, 0.25));
    feed_input(input);
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    const BcClientFrame frame{sizeof(BcClientFrame), uint32_t(input.menu), uint32_t(bc::input::capturing()),
                              uint32_t(input.focused), viewport};
    const auto snapshot = callbacks;
    for (const auto &entry : snapshot)
        if (entry && entry->alive) {
            callback_owner = entry->owner;
            draw_commands = 0;
            try {
                entry->callback(&frame, entry->user);
            } catch (...) {
                entry->alive = false;
                for (auto &slot : callbacks)
                    if (slot == entry) {
                        slot.reset();
                        --metrics.callback_count;
                        break;
                    }
                log("Render callback disabled after exception");
            }
            callback_owner = 0;
        }
    bool open = input.menu;
    if (open)
        bc::menu::draw(*host, metrics, open);
    else
        bc::menu::keybind::cancel();
    bc::input::binding_capture(open && input.focused && bc::menu::keybind::active());
    if (open != input.menu)
        bc::input::request_menu(open);
    ImGui::Render();
    ID3D11RenderTargetView *old_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView *old_depth{};
    device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old_targets, &old_depth);
    device_context->OMSetRenderTargets(1, &target, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old_targets, old_depth);
    for (auto *saved : old_targets)
        if (saved)
            saved->Release();
    if (old_depth)
        old_depth->Release();
}
static HRESULT __stdcall hooked_present(IDXGISwapChain *chain, UINT sync, UINT flags) noexcept {
    if (!running || in_present || (flags & DXGI_PRESENT_TEST))
        return original_present(chain, sync, flags);
    in_present = true;
    struct Guard {
        ~Guard() { in_present = false; }
    } guard;
    try {
        const auto started = Clock::now();
        if (host->menu_key)
            bc::input::set_menu_key(host->menu_key());
        std::lock_guard lock(mutex);
        DXGI_SWAP_CHAIN_DESC desc{};
        if ((active_chain == chain && active_window) || game_chain(chain, desc)) {
            if (active_chain != chain) {
                release_resources();
                active_chain = chain;
                if (!desc.OutputWindow)
                    chain->GetDesc(&desc);
                active_window = desc.OutputWindow;
                if (!bc::input::install(active_window, menu_changed, window_closed))
                    throw std::runtime_error("Window input installation failed");
                viewport.width = desc.BufferDesc.Width;
                viewport.height = desc.BufferDesc.Height;
                if (!viewport.width || !viewport.height) {
                    RECT client{};
                    GetClientRect(active_window, &client);
                    viewport.width = std::max<LONG>(1, client.right);
                    viewport.height = std::max<LONG>(1, client.bottom);
                }
                viewport.dpi_scale = std::max(1.f, float(GetDpiForWindow(active_window)) / 96.f);
                first_present = Clock::now();
                stable_frames = 0;
                state = 2;
                metrics.graphics_state = 2;
                log("Direct3D 11 game swap chain discovered; menu resources deferred.");
            }
            ++stable_frames;
            ++viewport.frame_number;
            ++metrics.frames;
            const auto now = Clock::now();
            viewport.delta_seconds = last_present.time_since_epoch().count()
                                         ? std::chrono::duration<double>(now - last_present).count()
                                         : 1. / 60.;
            last_present = now;
            auto input = bc::input::frame(viewport.frame_number, viewport.width, viewport.height);
            const bool stable = host->startup_ready() && stable_frames >= 30 &&
                                std::chrono::duration<double>(now - first_present).count() >= 2.;
            const bool initializing = !imgui && input.menu && stable;
            if (initializing) {
                if (!initialize_imgui(chain)) {
                    ++metrics.device_errors;
                    state = 3;
                    log("ImGui initialization failed; will retry on a later menu opening.");
                    bc::input::request_menu(false);
                }
            }
            if (imgui && !target && !render_target(chain)) {
                ++metrics.device_errors;
                release_resources();
            }
            if (imgui && target)
                draw_frame(input);
            metrics.width = viewport.width;
            metrics.height = viewport.height;
            metrics.menu_open = input.menu;
            const auto cost = std::chrono::duration<double, std::micro>(Clock::now() - started).count();
            if (!imgui) {
                ++metrics.dormant_frames;
                dormant_total += cost;
                metrics.dormant_average_us = dormant_total / metrics.dormant_frames;
            } else if (input.menu && !initializing) {
                ++metrics.open_frames;
                open_total += cost;
                metrics.open_average_us = open_total / metrics.open_frames;
            } else if (!initializing) {
                ++metrics.closed_frames;
                closed_total += cost;
                metrics.closed_average_us = closed_total / metrics.closed_frames;
            }
        }
    } catch (const std::exception &e) {
        if (frame_errors++ < 8)
            log(std::string("Frame skipped: ") + e.what());
    } catch (...) {
        if (frame_errors++ < 8)
            log("Frame skipped after unexpected exception");
    }
    const auto result = original_present(chain, sync, flags);
    if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) {
        try {
            std::lock_guard lock(mutex);
            if (chain == active_chain) {
                if (metrics.device_errors++ < 16)
                    log(std::format("D3D11 device unavailable: 0x{:08X}", uint32_t(result)));
                release_resources();
                state = 3;
            }
        } catch (...) {
        }
    }
    return result;
}
static HRESULT __stdcall hooked_resize(IDXGISwapChain *chain, UINT count, UINT w, UINT h, DXGI_FORMAT format,
                                       UINT flags) noexcept {
    std::lock_guard lock(mutex);
    try {
        if (chain == active_chain) {
            release_target();
            log(std::format("ResizeBuffers: {}x{}; render target released", w, h));
        }
    } catch (...) {
    }
    const auto result = original_resize(chain, count, w, h, format, flags);
    try {
        if (FAILED(result)) {
            ++metrics.device_errors;
            log(std::format("ResizeBuffers failed: 0x{:08X}", uint32_t(result)));
        }
    } catch (...) {
    }
    return result;
}
static ULONG __stdcall hooked_release(IUnknown *object) noexcept {
    std::lock_guard lock(mutex);
    const bool was_active = reinterpret_cast<void *>(object) == reinterpret_cast<void *>(active_chain);
    const auto count = original_release(object);
    if (was_active && count == 0) {
        // Never dereference the destroyed swap chain.
        try {
            release_resources();
            log("Game swap chain destroyed; awaiting recreation.");
        } catch (...) {
        }
    }
    return count;
}
static bool install_hooks() {
    auto started = Clock::now();
    constexpr wchar_t name[] = L"Briefcase.Client.D3D11Probe";
    WNDCLASSW cls{};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = name;
    RegisterClassW(&cls);
    HWND window =
        CreateWindowExW(0, name, L"", WS_OVERLAPPED, 0, 0, 2, 2, nullptr, nullptr, cls.hInstance, nullptr);
    if (!window)
        return false;
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = 2;
    desc.BufferDesc.Height = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    ID3D11Device *probe_device{};
    ID3D11DeviceContext *probe_context{};
    IDXGISwapChain *probe_chain{};
    const auto created = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                                       D3D11_SDK_VERSION, &desc, &probe_chain, &probe_device,
                                                       nullptr, &probe_context);
    bool ok = SUCCEEDED(created) && probe_chain;
    if (ok) {
        auto **vtable = *reinterpret_cast<void ***>(probe_chain);
        present_address = vtable[8];
        resize_address = vtable[13];
        release_address = vtable[2];
        auto result = MH_Initialize();
        ok = result == MH_OK || result == MH_ERROR_ALREADY_INITIALIZED;
        if (ok)
            ok = MH_CreateHook(present_address, reinterpret_cast<void *>(hooked_present),
                               reinterpret_cast<void **>(&original_present)) == MH_OK;
        if (ok)
            ok = MH_CreateHook(resize_address, reinterpret_cast<void *>(hooked_resize),
                               reinterpret_cast<void **>(&original_resize)) == MH_OK;
        if (ok)
            ok = MH_CreateHook(release_address, reinterpret_cast<void *>(hooked_release),
                               reinterpret_cast<void **>(&original_release)) == MH_OK;
        if (ok && !running)
            ok = false;
        if (ok)
            ok = MH_QueueEnableHook(present_address) == MH_OK &&
                 MH_QueueEnableHook(resize_address) == MH_OK &&
                 MH_QueueEnableHook(release_address) == MH_OK && MH_ApplyQueued() == MH_OK;
    }
    if (probe_chain)
        probe_chain->Release();
    if (probe_context)
        probe_context->Release();
    if (probe_device)
        probe_device->Release();
    DestroyWindow(window);
    UnregisterClassW(name, cls.hInstance);
    if (!ok) {
        if (present_address)
            MH_DisableHook(present_address);
        if (resize_address)
            MH_DisableHook(resize_address);
        if (release_address)
            MH_DisableHook(release_address);
    }
    {
        std::lock_guard lock(mutex);
        metrics.hook_ms = ms(started);
    }
    log(ok ? std::format(
                 "D3D11 Present/ResizeBuffers/Release hooks installed in {:.3f} ms; no ImGui resources yet.",
                 ms(started))
           : std::format("D3D11 hook installation failed: 0x{:08X}", uint32_t(created)));
    return ok;
}
static BcResult BC_CALL start(const BcClientHostApi *provided) noexcept {
    if (!provided || provided->size < sizeof(*provided) || provided->version != 7 ||
        !provided->startup_ready || !provided->home || !provided->mod || !provided->servers ||
        !provided->add_server || !provided->remove_server || !provided->join_server ||
        !provided->admin_select || !provided->admin_connect || !provided->admin_command ||
        !provided->admin_disconnect || !provided->admin_snapshot)
        return BC_VERSION_MISMATCH;
    if (running.exchange(true))
        return BC_OK;
    host = provided;
    if (host->menu_key)
        bc::input::set_menu_key(host->menu_key());
    state = 1;
    stopping = false;
    try {
        discovery_worker = new std::thread([] {
            try {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                for (unsigned i = 0; i < 600 && running; ++i) {
                    if (GetModuleHandleW(L"d3d11.dll")) {
                        if (install_hooks())
                            state = 2;
                        else
                            state = 3;
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (running) {
                    state = 3;
                    log("D3D11 was not loaded within discovery timeout.");
                }
            } catch (...) {
                state = 3;
                log("Graphics discovery failed.");
            }
        });
        return BC_OK;
    } catch (...) {
        running = false;
        return BC_INTERNAL;
    }
}
static void BC_CALL stop() noexcept {
    if (stopping.exchange(true))
        return;
    running = false;
    try {
        if (discovery_worker) {
            discovery_worker->join();
            delete discovery_worker;
            discovery_worker = nullptr;
        }
        {
            std::lock_guard lock(mutex);
            for (auto &entry : callbacks)
                if (entry) {
                    entry->alive = false;
                    entry.reset();
                }
            metrics.callback_count = 0;
            bc::input::stop();
            release_resources();
            log(std::format("Client renderer stopped: dormant {:.3f} us ({} frames), closed {:.3f} us ({}), "
                            "open {:.3f} us ({})",
                            metrics.dormant_average_us, metrics.dormant_frames, metrics.closed_average_us,
                            metrics.closed_frames, metrics.open_average_us, metrics.open_frames));
        }
        if (present_address)
            MH_DisableHook(present_address);
        if (resize_address)
            MH_DisableHook(resize_address);
        if (release_address)
            MH_DisableHook(release_address);
    } catch (...) {
    }
}
static void BC_CALL cleanup(uint64_t owner) noexcept {
    try {
        std::lock_guard lock(mutex);
        for (auto &entry : callbacks)
            if (entry && entry->owner == owner) {
                entry->alive = false;
                entry.reset();
                --metrics.callback_count;
            }
    } catch (...) {
    }
}
static BcResult BC_CALL subscribe(uint64_t owner, BcRenderCallback callback, void *user,
                                  BcHandle *out) noexcept {
    if (!owner || !callback || !out)
        return BC_INVALID_ARGUMENT;
    *out = 0;
    try {
        std::lock_guard lock(mutex);
        if (!running)
            return BC_NOT_READY;
        unsigned owned = 0;
        for (const auto &entry : callbacks)
            if (entry && entry->owner == owner)
                ++owned;
        if (owned >= 16)
            return BC_LIMIT;
        for (auto &entry : callbacks)
            if (!entry) {
                const auto id = next_callback++;
                entry = std::make_shared<Callback>(Callback{owner, id, callback, user, true});
                *out = id;
                ++metrics.callback_count;
                return BC_OK;
            }
        return BC_LIMIT;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL unsubscribe(uint64_t owner, BcHandle id) noexcept {
    try {
        std::lock_guard lock(mutex);
        for (auto &entry : callbacks)
            if (entry && entry->id == id) {
                if (entry->owner != owner)
                    return BC_DENIED;
                entry->alive = false;
                entry.reset();
                --metrics.callback_count;
                return BC_OK;
            }
        return BC_STALE_HANDLE;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL get_viewport(BcViewport *out) noexcept {
    if (!out || out->size < sizeof(*out))
        return BC_INVALID_ARGUMENT;
    try {
        std::lock_guard lock(mutex);
        *out = viewport;
        return viewport.width && viewport.height ? BC_OK : BC_NOT_READY;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL text(uint64_t owner, float x, float y, uint32_t color, const char *text,
                             uint32_t length) noexcept {
    if (callback_owner != owner || !owner)
        return BC_WRONG_THREAD;
    if (++draw_commands > 1024)
        return BC_LIMIT;
    if (!std::isfinite(x) || !std::isfinite(y) || !text || length > 4096)
        return BC_INVALID_ARGUMENT;
    try {
        ImGui::GetBackgroundDrawList()->AddText({x, y}, color, text, text + length);
        return BC_OK;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL rectangle(uint64_t owner, float x, float y, float w, float h,
                                  uint32_t color) noexcept {
    if (callback_owner != owner || !owner)
        return BC_WRONG_THREAD;
    if (++draw_commands > 1024)
        return BC_LIMIT;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) || w < 0 || h < 0)
        return BC_INVALID_ARGUMENT;
    try {
        ImGui::GetBackgroundDrawList()->AddRectFilled({x, y}, {x + w, y + h}, color);
        return BC_OK;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL line(uint64_t owner, float x, float y, float x2, float y2, uint32_t color,
                             float thickness) noexcept {
    if (callback_owner != owner || !owner)
        return BC_WRONG_THREAD;
    if (++draw_commands > 1024)
        return BC_LIMIT;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(x2) || !std::isfinite(y2) ||
        !std::isfinite(thickness) || thickness <= 0 || thickness > 64)
        return BC_INVALID_ARGUMENT;
    try {
        ImGui::GetBackgroundDrawList()->AddLine({x, y}, {x2, y2}, color, thickness);
        return BC_OK;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL circle(uint64_t owner, float x, float y, float radius, uint32_t color,
                               float thickness) noexcept {
    if (callback_owner != owner || !owner)
        return BC_WRONG_THREAD;
    if (++draw_commands > 1024)
        return BC_LIMIT;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) || radius <= 0 || radius > 32768 ||
        !std::isfinite(thickness) || thickness <= 0 || thickness > 64)
        return BC_INVALID_ARGUMENT;
    try {
        ImGui::GetBackgroundDrawList()->AddCircle({x, y}, radius, color, 64, thickness);
        return BC_OK;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL key(uint32_t code, BcKeyState *out) noexcept {
    return bc::input::key(code, out);
}
static uint32_t BC_CALL capturing() noexcept {
    return bc::input::capturing();
}
static BcResult BC_CALL get_metrics(BcClientMetrics *out) noexcept {
    if (!out || out->size < sizeof(*out))
        return BC_INVALID_ARGUMENT;
    try {
        std::lock_guard lock(mutex);
        *out = metrics;
        out->graphics_state = state;
        return BC_OK;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static const BcClientModuleApi api{sizeof(BcClientModuleApi),
                                   1,
                                   start,
                                   stop,
                                   cleanup,
                                   subscribe,
                                   unsubscribe,
                                   get_viewport,
                                   text,
                                   rectangle,
                                   key,
                                   capturing,
                                   get_metrics,
                                   line,
                                   circle};
} // namespace
extern "C" __declspec(dllexport) const BcClientModuleApi *BC_CALL BriefcaseGetClientModuleApi() noexcept {
    return &api;
}
