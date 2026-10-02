#include "EntryGate.hpp"
#include <Windows.h>
#include <shellapi.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <cwchar>
#include <string>
#include <string_view>
extern "C" {
uintptr_t briefcase_forwards[17]{};
}
static DWORD game_thread;
static LARGE_INTEGER proxy_started{}, proxy_finished{}, counter_frequency{};
static HMODULE proxy_module;
static uint32_t(__cdecl *run_host)();
static bool client_process;
static bool server_process;
static std::string_view trim_ascii(std::string_view text) {
    const auto spaces = " \t\r\n";
    const auto first = text.find_first_not_of(spaces);
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(spaces);
    return text.substr(first, last - first + 1);
}
static bool safe_mod_name(std::string_view name) {
    if (name.empty() || name.size() > 64) return false;
    for (const unsigned char ch : name)
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) return false;
    return true;
}
static bool load_preentry_mods(const wchar_t *win64) {
    try {
        const auto mods = std::filesystem::path(win64) / L"ue4ss" / L"Mods";
        const auto list = mods / L"mods.txt";
        if (!std::filesystem::is_regular_file(list)) return true;
        if (std::filesystem::file_size(list) > 65536) return false;
        std::ifstream input(list, std::ios::binary);
        if (!input) return false;
        std::string line;
        while (std::getline(input, line)) {
            const auto record = trim_ascii(line);
            if (record.empty() || record.front() == '#' || record.front() == ';') continue;
            const auto colon = record.find(':');
            if (colon == std::string_view::npos || trim_ascii(record.substr(colon + 1)) != "1") continue;
            const auto name = trim_ascii(record.substr(0, colon));
            if (!safe_mod_name(name)) continue;
            const auto directory = mods / std::wstring(name.begin(), name.end());
            const auto early = directory / L"BriefcasePreEntry.dll";
            const auto folder_attributes = GetFileAttributesW(directory.c_str());
            const auto early_attributes = GetFileAttributesW(early.c_str());
            if (early_attributes == INVALID_FILE_ATTRIBUTES) continue;
            if (folder_attributes == INVALID_FILE_ATTRIBUTES ||
                (folder_attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                (early_attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                return false;
            const auto module = LoadLibraryExW(early.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!module) return false;
            const auto apply = reinterpret_cast<unsigned(__cdecl *)()>(
                GetProcAddress(module, "BriefcasePreEntry"));
            if (!apply || apply() != 0) {
                if (const auto explain = reinterpret_cast<const char *(__cdecl *)()>(
                        GetProcAddress(module, "BriefcasePreEntryError")))
                    OutputDebugStringA(explain());
                return false;
            }
            OutputDebugStringW(L"BriefcaseNative: pre-entry mod applied");
        }
        return input.eof();
    } catch (...) {
        return false;
    }
}
static std::wstring quote(const std::wstring &value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const auto character : value) {
        if (character == L'\\') { ++slashes; continue; }
        result.append(slashes * (character == L'\"' ? 2 : 1), L'\\');
        slashes = 0;
        if (character == L'\"') result += L'\\';
        result += character;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}
static bool handoff_updates(const wchar_t *root) {
    constexpr auto marker = L"BRIEFCASE_PROXY_BOOTSTRAPPED";
    wchar_t value[8]{};
    if (GetEnvironmentVariableW(marker, value, 8)) {
        SetEnvironmentVariableW(marker, nullptr);
        return false;
    }
    std::wstring directory(root);
    while (directory.size() > 3 && (directory.back() == L'\\' || directory.back() == L'/')) directory.pop_back();
    std::wstring updater = directory + L"\\Briefcase\\Core\\Tools\\Briefcase.ServerUpdater.exe";
    if (GetFileAttributesW(updater.c_str()) == INVALID_FILE_ATTRIBUTES) {
        OutputDebugStringW(L"BriefcaseNative: update coordinator missing; continuing without an update check");
        return false;
    }
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION probe{};
    auto probe_command = quote(updater) + L" --probe --root " + quote(directory);
    if (!CreateProcessW(updater.c_str(), probe_command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &probe)) {
        OutputDebugStringW(L"BriefcaseNative: update probe could not start; continuing in the original process");
        return false;
    }
    CloseHandle(probe.hThread);
    const auto waited = WaitForSingleObject(probe.hProcess, 120000);
    DWORD probe_status = 78;
    if (waited == WAIT_OBJECT_0) GetExitCodeProcess(probe.hProcess, &probe_status);
    else if (waited == WAIT_TIMEOUT) TerminateProcess(probe.hProcess, 78);
    CloseHandle(probe.hProcess);
    if (waited != WAIT_OBJECT_0 || probe_status != 10) {
        if (probe_status != 0)
            OutputDebugStringW(L"BriefcaseNative: update probe failed; continuing in the original process");
        return false;
    }
    int count{};
    auto **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments || count < 1)
        return false;
    std::wstring command = quote(updater) + L" --root " + quote(directory) + L" --parent " +
                           std::to_wstring(GetCurrentProcessId()) + L" --";
    for (int index = 1; index < count; ++index) command += L" " + quote(arguments[index]);
    LocalFree(arguments);
    PROCESS_INFORMATION process{};
    const auto started = CreateProcessW(updater.c_str(), command.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process);
    if (!started) {
        OutputDebugStringW(L"BriefcaseNative: update coordinator could not start; continuing without an update check");
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
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
    if (!client_process && handoff_updates(root)) {
        ExitProcess(0);
        return 0;
    }
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
        if (!load_preentry_mods(root)) {
            OutputDebugStringW(L"BriefcaseNative: pre-entry mod failed");
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
    // No host/mod loading or waiting under loader lock. The gate is removed at EXE entry.
    return entry_gate::install(prepare) ? TRUE : FALSE;
}
