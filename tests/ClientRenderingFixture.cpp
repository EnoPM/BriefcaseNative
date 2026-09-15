
#include "../runtime/Briefcase.Client.Rendering/ClientBridge.h"
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <d3d11.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
static const BcClientModuleApi *api;
static unsigned checks;
static std::atomic<unsigned> calls{}, removed_calls{}, unreal_requests{};
static BcHandle other{}, self{};
static void check(bool b, const char *name) {
    ++checks;
    if (!b)
        throw std::runtime_error(name);
}
static void BC_CALL log(const char *t) {
    std::cout << t << "\n";
}
static void BC_CALL unreal() {
    ++unreal_requests;
}
static void BC_CALL shutdown() {}
static BcResult BC_CALL servers(BcClientServerList *out) {
    *out = {};
    out->size = sizeof(*out);
    out->writable = 1;
    return BC_OK;
}
static BcResult BC_CALL add_server(const char *, const char *) {
    return BC_OK;
}
static BcResult BC_CALL server_action(uint64_t) {
    return BC_OK;
}
static bool startup_allowed{};
static uint32_t BC_CALL startup_ready() {
    return startup_allowed;
}
static BcResult BC_CALL home(BcClientHome *h) {
    *h = {};
    h->size = sizeof(*h);
    h->discovered = 1;
    h->loaded = 1;
    h->unreal_state = 2;
    h->build.size = sizeof(BcBuild);
    h->build.pe_timestamp = 0x6A96564B;
    h->build.image_size = 0x06283000;
    strcpy_s(h->build.framework_version, "0.3.0");
    h->build.engine_major = 4;
    h->build.engine_minor = 27;
    h->bootstrap_ms = 17.2;
    h->unreal_ms = 151.6;
    return BC_OK;
}
static BcResult BC_CALL mod(uint32_t n, BcClientModRow *m) {
    if (n)
        return BC_NOT_FOUND;
    *m = {};
    m->size = sizeof(*m);
    m->state = BC_UI_LOADED;
    strcpy_s(m->id, "briefcase.native-overlay-sample");
    strcpy_s(m->name, "Briefcase Native Overlay Sample");
    strcpy_s(m->author, "BriefcaseNative");
    strcpy_s(m->version, "0.1.0");
    strcpy_s(m->environment, "client");
    return BC_OK;
}
static void BC_CALL callback(const BcClientFrame *f, void *) {
    ++calls;
    if (other) {
        check(api->unsubscribe(2, other) == BC_OK, "remove queued callback");
        other = 0;
    }
    check(f->viewport.width > 0, "callback viewport");
    check(api->line(1, 30, 35, 90, 35, 0xFFFFFFFF, 2) == BC_OK, "public line");
    check(api->circle(1, 100, 100, 25, 0xFFFFFFFF, 2) == BC_OK, "public circle");
    check(api->circle(2, 100, 100, 25, 0xFFFFFFFF, 2) == BC_WRONG_THREAD, "draw owner isolation");
    check(api->circle(1, 100, 100, -1, 0xFFFFFFFF, 2) == BC_INVALID_ARGUMENT, "negative radius rejected");
    check(api->text(1, 20, 70, 0xFFFFFFFF, "Public API fixture", 18) == BC_OK, "public text in callback");
}
static void BC_CALL removed(const BcClientFrame *, void *) {
    ++removed_calls;
}
static void BC_CALL remove_self(const BcClientFrame *, void *) {
    check(api->unsubscribe(3, self) == BC_OK, "self unsubscribe");
}
static void BC_CALL exception(const BcClientFrame *, void *) {
    throw std::runtime_error("deliberate fixture exception");
}
static BcResult BC_CALL sub(void *, BcRenderCallback cb, void *u, BcHandle *h) {
    return api->subscribe(1000, cb, u, h);
}
static BcResult BC_CALL unsub(void *, BcHandle h) {
    return api->unsubscribe(1000, h);
}
static BcResult BC_CALL view(void *, BcViewport *v) {
    return api->viewport(v);
}
static BcResult BC_CALL pixels(void *, double x, double y, float *px, float *py) {
    BcViewport v{sizeof(v)};
    if (!px || !py || x < 0 || x > 1 || y < 0 || y > 1)
        return BC_INVALID_ARGUMENT;
    auto result = api->viewport(&v);
    *px = float(x * v.width);
    *py = float(y * v.height);
    return result;
}
static BcResult BC_CALL text(void *, float x, float y, uint32_t c, const char *t, uint32_t n) {
    return api->text(1000, x, y, c, t, n);
}
static BcResult BC_CALL rect(void *, float x, float y, float w, float h, uint32_t c) {
    return api->rectangle(1000, x, y, w, h, c);
}
static BcClientRenderApi render_api{sizeof(render_api), 1, sub, unsub, view, pixels, text, rect};
static BcResult BC_CALL service(void *, const char *name, uint32_t v, const void **out) {
    if (std::strcmp(name, BC_CLIENT_RENDER_SERVICE) || v != 1)
        return BC_NOT_FOUND;
    *out = &render_api;
    return BC_OK;
}
static BcResult BC_CALL sample_log(void *, uint32_t, const char *p, uint32_t n) {
    std::cout.write(p, n);
    std::cout << "\n";
    return BC_OK;
}
static LRESULT CALLBACK window_proc(HWND w, UINT m, WPARAM p, LPARAM l) {
    return DefWindowProcW(w, m, p, l);
}
struct Graphics {
    HWND window{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> chain;
    void create() {
        DXGI_SWAP_CHAIN_DESC d{};
        d.BufferDesc.Width = 1280;
        d.BufferDesc.Height = 800;
        d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.BufferCount = 1;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.OutputWindow = window;
        d.Windowed = TRUE;
        check(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                                      D3D11_SDK_VERSION, &d, &chain, &device, nullptr,
                                                      &context)),
              "create fixture WARP swap chain");
    }
    void frame() {
        MSG msg{};
        while (PeekMessageW(&msg, window, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        ComPtr<ID3D11Texture2D> buffer;
        ComPtr<ID3D11RenderTargetView> target;
        check(SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(&buffer))), "fixture backbuffer");
        check(SUCCEEDED(device->CreateRenderTargetView(buffer.Get(), nullptr, &target)), "fixture target");
        const float color[] = {.035f, .048f, .07f, 1.f};
        context->ClearRenderTargetView(target.Get(), color);
        check(SUCCEEDED(chain->Present(0, 0)), "fixture present");
    }
    void frames(unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            frame();
    }
    void stable() {
        const auto deadline = Clock::now() + std::chrono::milliseconds(2150);
        do {
            frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        } while (Clock::now() < deadline);
    }
    void verify_targets() {
        ComPtr<ID3D11Texture2D> buffer, aux;
        ComPtr<ID3D11RenderTargetView> first, second;
        check(SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(&buffer))), "state test buffer");
        D3D11_TEXTURE2D_DESC d{};
        buffer->GetDesc(&d);
        d.MiscFlags = 0;
        d.BindFlags = D3D11_BIND_RENDER_TARGET;
        check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &aux)), "state test auxiliary texture");
        check(SUCCEEDED(device->CreateRenderTargetView(buffer.Get(), nullptr, &first)) &&
                  SUCCEEDED(device->CreateRenderTargetView(aux.Get(), nullptr, &second)),
              "state test targets");
        ID3D11RenderTargetView *targets[] = {first.Get(), second.Get()};
        context->OMSetRenderTargets(2, targets, nullptr);
        frame();
        ID3D11RenderTargetView *restored[2]{};
        context->OMGetRenderTargets(2, restored, nullptr);
        check(restored[0] == first.Get() && restored[1] == second.Get(), "all game render targets restored");
        for (auto *t : restored)
            if (t)
                t->Release();
        context->ClearState();
    }
    void reset() {
        context->ClearState();
        context->Flush();
        chain.Reset();
        context.Reset();
        device.Reset();
    }
    void png(const wchar_t *path) {
        ComPtr<ID3D11Texture2D> buffer, staging;
        check(SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(&buffer))), "snapshot buffer");
        D3D11_TEXTURE2D_DESC d{};
        buffer->GetDesc(&d);
        d.Usage = D3D11_USAGE_STAGING;
        d.BindFlags = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        d.MiscFlags = 0;
        check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &staging)), "snapshot staging");
        context->CopyResource(staging.Get(), buffer.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "snapshot map");
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&factory))),
              "WIC factory");
        check(SUCCEEDED(factory->CreateStream(&stream)) &&
                  SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)),
              "PNG stream");
        check(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
                  SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)),
              "PNG encoder");
        check(SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)),
              "PNG frame");
        frame->SetSize(d.Width, d.Height);
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        check(SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGRA,
              "PNG RGBA");
        std::vector<BYTE> pixels(d.Width * d.Height * 4);
        for (UINT y = 0; y < d.Height; ++y)
            for (UINT x = 0; x < d.Width; ++x) {
                const auto *in = static_cast<BYTE *>(map.pData) + y * map.RowPitch + x * 4;
                auto *out = pixels.data() + (y * d.Width + x) * 4;
                out[0] = in[2];
                out[1] = in[1];
                out[2] = in[0];
                out[3] = in[3];
            }
        check(SUCCEEDED(frame->WritePixels(d.Height, d.Width * 4, UINT(pixels.size()), pixels.data())),
              "PNG pixels");
        frame->Commit();
        encoder->Commit();
        context->Unmap(staging.Get(), 0);
    }
};
int main() {
    try {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::filesystem::create_directories("client-fixture");
        WNDCLASSW cls{};
        cls.lpfnWndProc = window_proc;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"UnrealWindow";
        check(RegisterClassW(&cls) != 0, "fixture class");
        HWND window =
            CreateWindowExW(0, cls.lpszClassName, L"Briefcase isolated rendering fixture",
                            WS_OVERLAPPEDWINDOW, 0, 0, 1280, 800, nullptr, nullptr, cls.hInstance, nullptr);
        check(window != nullptr, "fixture hidden window");
        Graphics gfx;
        gfx.window = window;
        gfx.create();
        auto dll = LoadLibraryW(L"Briefcase.Client.Rendering.dll");
        check(dll != nullptr, "load client rendering");
        auto get = reinterpret_cast<BcGetClientModuleApi>(GetProcAddress(dll, "BriefcaseGetClientModuleApi"));
        check(get != nullptr, "private client bridge");
        api = get();
        static BcClientHostApi host{sizeof(host),
                                    7,
                                    log,
                                    home,
                                    mod,
                                    unreal,
                                    shutdown,
                                    startup_ready,
                                    servers,
                                    add_server,
                                    server_action,
                                    server_action,
                                    server_action,
                                    [](uint64_t, const char *, const char *, const char *,
                                       uint32_t) -> BcResult { return BC_NOT_READY; },
                                    [](const char *, const char *) -> BcResult { return BC_NOT_READY; },
                                    []() {},
                                    [](char *b, uint32_t, uint64_t *) -> BcResult {
                                        b[0] = 0;
                                        return BC_OK;
                                    }};
        check(api->start(&host) == BC_OK, "start graphics");
        BcClientMetrics m{sizeof(m)};
        for (unsigned i = 0; i < 500; ++i) {
            api->metrics(&m);
            if (m.graphics_state >= 2)
                break;
            Sleep(10);
        }
        check(m.graphics_state == 2, "hook installed");
        BcHandle handle{}, ignored{};
        check(api->subscribe(1, callback, nullptr, &handle) == BC_OK, "subscribe owner 1");
        check(api->subscribe(2, removed, nullptr, &other) == BC_OK, "subscribe owner 2");
        check(api->subscribe(3, remove_self, nullptr, &self) == BC_OK, "subscribe self");
        check(api->subscribe(4, exception, nullptr, &ignored) == BC_OK, "subscribe throwing callback");
        check(api->unsubscribe(2, handle) == BC_DENIED, "owner isolation");
        check(api->text(1, 0, 0, 0, "x", 1) == BC_WRONG_THREAD, "render thread boundary");
        auto sample = LoadLibraryW(L"Briefcase.NativeOverlaySample.dll");
        check(sample != nullptr, "sample DLL");
        auto load = reinterpret_cast<BcModLoad>(GetProcAddress(sample, "BriefcaseModLoad"));
        auto unload = reinterpret_cast<void(BC_CALL *)()>(GetProcAddress(sample, "BriefcaseModUnload"));
        static BcApi sample_api{};
        sample_api.size = sizeof(sample_api);
        sample_api.version = 1;
        sample_api.get_service = service;
        sample_api.log = sample_log;
        check(load && unload && load(&sample_api) == BC_OK, "sample public ABI load");
        gfx.stable();
        api->metrics(&m);
        check(m.context_created == 0 && m.context_ms == 0 && m.fonts_ms == 0 && !calls,
              "lazy context before menu");
        check(unreal_requests == 0, "Unreal deferred through startup");
        check(!api->capturing(), "closed input not captured");
        SendMessageW(window, WM_APP + 0x4BC, TRUE, 0);
        gfx.frames(5);
        api->metrics(&m);
        check(!m.context_created && !unreal_requests,
              "F1 cannot allocate ImGui resources during shader precompilation");
        check(reinterpret_cast<void *>(&SetCursorPos) !=
                  reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetCursorPos")),
              "only fixture game cursor import is redirected before testing a virtual warp");
        check(reinterpret_cast<void *>(&GetCursorPos) !=
                  reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetCursorPos")),
              "fixture game cursor polling import is redirected");
        check(SetCursorPos(321, 123) != FALSE, "virtual game cursor request");
        POINT virtual_position{};
        check(GetCursorPos(&virtual_position) && virtual_position.x == 321 && virtual_position.y == 123,
              "game sees its own requested position while the menu owns the physical cursor");
        startup_allowed = true;
        gfx.frames(20);
        api->metrics(&m);
        check(m.context_created == 1 && m.open_frames > 0 && calls > 0, "lazy ImGui at first opening");
        check(removed_calls == 0, "removed callback not dispatched from snapshot");
        check(unreal_requests == 1, "Unreal request once");
        check(m.callback_count == 2, "self-removal and exception cleanup");
        gfx.verify_targets();
        gfx.png(L"client-fixture/home.png");
        SendMessageW(window, WM_APP + 0x4BC, FALSE, 0);
        gfx.frames(400);
        api->metrics(&m);
        check(m.closed_frames >= 400, "closed sample render measured");
        check(SUCCEEDED(gfx.chain->ResizeBuffers(1, 960, 600, DXGI_FORMAT_UNKNOWN, 0)),
              "resize actual DXGI chain");
        gfx.frames(10);
        BcViewport v{sizeof(v)};
        api->viewport(&v);
        check(v.width == 960 && v.height == 600, "viewport updates after resize");
        SendMessageW(window, WM_APP + 0x4BC, TRUE, 0);
        gfx.frames(10);
        gfx.png(L"client-fixture/resized.png");
        api->cleanup_owner(1);
        const auto before = calls.load();
        gfx.frames(4);
        check(calls == before, "owner cleanup invalidates callbacks");
        unload();
        api->metrics(&m);
        check(m.callback_count == 0, "sample unload removes callback");
        gfx.reset();
        api->metrics(&m);
        check(m.context_created == 0, "swap-chain destruction releases ImGui");
        gfx.create();
        gfx.stable();
        gfx.frames(10);
        api->metrics(&m);
        check(m.context_created == 1 && m.width == 1280, "device and swap-chain recreation");
        std::ofstream report("client-fixture/metrics.json");
        report << "{\"backend\":\"D3D11 WARP isolated fixture\",\"checks\":" << checks
               << ",\"hook_ms\":" << m.hook_ms << ",\"context_ms\":" << m.context_ms
               << ",\"fonts_ms\":" << m.fonts_ms << ",\"resources_ms\":" << m.resources_ms
               << ",\"dormant_us\":" << m.dormant_average_us << ",\"closed_us\":" << m.closed_average_us
               << ",\"open_us\":" << m.open_average_us << ",\"closed_frames\":" << m.closed_frames
               << ",\"open_frames\":" << m.open_frames << "}";
        api->stop();
        api->metrics(&m);
        check(m.callback_count == 0 && !api->capturing(), "stopped input and rendering");
        gfx.frames(2);
        gfx.reset();
        DestroyWindow(window);
        std::cout << "PASS " << checks << " rendering checks (includes frame/resource checks)\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << "\n";
        if (api)
            api->stop();
        return 1;
    }
}
