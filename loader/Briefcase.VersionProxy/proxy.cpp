#include "EntryGate.hpp"
#include "PreEntryMods.hpp"
#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <cwchar>
#include <string>
extern "C" {
uintptr_t briefcase_forwards[17]{};
}
static DWORD game_thread;
static LARGE_INTEGER proxy_started{}, proxy_finished{}, counter_frequency{};
static HMODULE proxy_module;
static uint32_t(__cdecl *run_host)();
static bool client_process;
static bool server_process;
static BOOL CALLBACK find_client_unreal_window(HWND window, LPARAM context) {
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId())
        return TRUE;
    wchar_t name[64]{};
    if (GetClassNameW(window, name, 64) && wcscmp(name, L"UnrealWindow") == 0) {
        *reinterpret_cast<HWND *>(context) = window;
        return FALSE;
    }
    return TRUE;
}
static bool wait_for_client_unreal_window() {
    // The client proxy is loaded before CoreUObject has its default object.
    // UE4SS needs that object to find ProcessEvent, so defer only its load on
    // this worker thread until Unreal has created its own game window.
    for (unsigned attempt = 0; attempt != 1500; ++attempt) {
        HWND window{};
        EnumWindows(find_client_unreal_window, reinterpret_cast<LPARAM>(&window));
        if (window) {
            Sleep(2000);
            return true;
        }
        Sleep(200);
    }
    return false;
}
static DWORD WINAPI worker(void *) {
    return run_host ? run_host() : 1;
}
static uint32_t bootstrap(void *self) {
    LARGE_INTEGER begin{};
    QueryPerformanceCounter(&begin);
    wchar_t path[32768]{};
    auto length = GetModuleFileNameW(static_cast<HMODULE>(self), path, 32768);
    if (!length || length >= 32768)
        return 1;
    auto *end = wcsrchr(path, L'\\');
    if (!end)
        return 1;
    end[1] = 0;
    wchar_t root[32768]{};
    wcscpy_s(root, path);
    wchar_t core[32768]{};
    wcscpy_s(core, path);
    if (wcscat_s(core, L"Briefcase\\Core\\Briefcase.NativeHost.dll"))
        return 1;
    if (GetFileAttributesW(core) != INVALID_FILE_ATTRIBUTES)
        wcscpy_s(path, core);
    else
        return 1;
    auto module =
        LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        OutputDebugStringW(L"BriefcaseNative: NativeHost load failed");
        return 1;
    }
    auto start = reinterpret_cast<uint32_t(__cdecl *)(uint32_t)>(GetProcAddress(module, "BriefcasePrepare"));
    if (!start || start(game_thread) != 0)
        return 1;
    if (!client_process) {
        if (!briefcase::loader::load_preentry_mods(root)) {
            OutputDebugStringW(L"BriefcaseNative: pre-entry mod failed");
            return 1;
        }
    } else if (!wait_for_client_unreal_window()) {
        OutputDebugStringW(L"BriefcaseNative: client Unreal window not ready; UE4SS not loaded");
        return 1;
    }
    if (wcscat_s(root, L"ue4ss\\UE4SS.dll"))
        return 1;
    if (GetFileAttributesW(root) == INVALID_FILE_ATTRIBUTES ||
        !LoadLibraryExW(root, nullptr,
                        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        OutputDebugStringW(L"BriefcaseNative: UE4SS load failed");
        return 1;
    }
    run_host = reinterpret_cast<uint32_t(__cdecl *)()>(GetProcAddress(module, "BriefcaseRun"));
    if (!run_host)
        return 1;
    if (auto record = reinterpret_cast<void(__cdecl *)(double, double)>(
            GetProcAddress(module, "BriefcaseRecordBootstrap"))) {
        LARGE_INTEGER end{};
        QueryPerformanceCounter(&end);
        record(double(proxy_finished.QuadPart - proxy_started.QuadPart) * 1000000. /
                   counter_frequency.QuadPart,
               double(end.QuadPart - begin.QuadPart) * 1000. / counter_frequency.QuadPart);
    }
    if (auto t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr)) {
        CloseHandle(t);
        return 0;
    }
    return 1;
}
static DWORD WINAPI client_bootstrap(void *) {
    if (bootstrap(proxy_module) != 0)
        OutputDebugStringW(L"BriefcaseNative client bootstrap failed; vanilla process remains available.");
    return 0;
}
static void prepare() {
    if (bootstrap(proxy_module) != 0) {
        OutputDebugStringW(L"BriefcaseNative startup rejected; see Briefcase/Logs/BriefcaseNative.log");
        ExitProcess(119);
    }
}
BOOL WINAPI DllMain(HMODULE self, DWORD reason, void *) {
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    QueryPerformanceFrequency(&counter_frequency);
    QueryPerformanceCounter(&proxy_started);
    DisableThreadLibraryCalls(self);
    game_thread = GetCurrentThreadId();
    wchar_t path[MAX_PATH]{};
    if (!GetSystemDirectoryW(path, MAX_PATH) || wcscat_s(path, L"\\version.dll"))
        return FALSE;
    auto original = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!original)
        return FALSE;
    const char *names[] = {"GetFileVersionInfoA",
                           "GetFileVersionInfoByHandle",
                           "GetFileVersionInfoExA",
                           "GetFileVersionInfoExW",
                           "GetFileVersionInfoSizeA",
                           "GetFileVersionInfoSizeExA",
                           "GetFileVersionInfoSizeExW",
                           "GetFileVersionInfoSizeW",
                           "GetFileVersionInfoW",
                           "VerFindFileA",
                           "VerFindFileW",
                           "VerInstallFileA",
                           "VerInstallFileW",
                           "VerLanguageNameA",
                           "VerLanguageNameW",
                           "VerQueryValueA",
                           "VerQueryValueW"};
    for (size_t i = 0; i < 17; ++i) {
        briefcase_forwards[i] = reinterpret_cast<uintptr_t>(GetProcAddress(original, names[i]));
        if (!briefcase_forwards[i])
            return FALSE;
    }
    proxy_module = self;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    const auto *filename = wcsrchr(executable, L'\\');
    client_process = filename && _wcsicmp(filename + 1, L"DeceiveInc-Win64-Shipping.exe") == 0;
    server_process = filename && _wcsicmp(filename + 1, L"DeceiveIncServer-Win64-Shipping.exe") == 0;
    if (client_process) {
        // Client has no startup patches: do not gate EXE entry or load the host under loader lock.
        QueryPerformanceCounter(&proxy_finished);
        if (auto thread = CreateThread(nullptr, 0, client_bootstrap, nullptr, 0, nullptr))
            CloseHandle(thread);
        return TRUE;
    }
    if (!server_process)
        return TRUE;
    QueryPerformanceCounter(&proxy_finished);
    wchar_t external_bootstrap[2]{};
    if (GetEnvironmentVariableW(L"BRIEFCASE_EXTERNAL_BOOTSTRAP", external_bootstrap, 2) &&
        external_bootstrap[0] == L'1')
        return TRUE;
    // No host/mod loading or waiting under loader lock. The gate is removed at EXE entry.
    return entry_gate::install(prepare) ? TRUE : FALSE;
}
