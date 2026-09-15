#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <detours.h>
namespace briefcase::launcher {
inline std::wstring quote(const std::wstring& value) {
    std::wstring out = L"\""; size_t slashes = 0;
    for (auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\'); slashes = 0;
        if (c == L'"') out += L'\\';
        out += c;
    }
    out.append(slashes * 2, L'\\'); return out + L'"';
}
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
};
inline DWORD start(const std::filesystem::path& exe, const std::filesystem::path& bootstrap,
                   const std::wstring& arguments, DWORD timeout = 90000) {
    const auto win64 = exe.parent_path();
    if (!std::filesystem::is_regular_file(exe) || !std::filesystem::is_regular_file(bootstrap))
        throw std::runtime_error("Missing server executable or bootstrap library");
    // Detours' import table uses an ANSI path. Refuse lossy conversion, including best-fit mappings.
    BOOL lossy = FALSE;
    int size = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, bootstrap.c_str(), -1, nullptr, 0, nullptr, &lossy);
    std::string dll(size, '\0');
    if (!size || !WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, bootstrap.c_str(), -1, dll.data(), size, nullptr, &lossy) || lossy)
        throw std::runtime_error("Bootstrap path cannot be represented by the Windows loader");
    auto cmd = quote(exe.wstring()) + L" " + arguments;
    STARTUPINFOW si{sizeof(si)}; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!DetourCreateProcessWithDllExW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, win64.c_str(), &si, &pi, dll.c_str(), nullptr))
        throw std::runtime_error("Cannot create injected server: " + std::to_string(GetLastError()));
    Handle process(pi.hProcess), thread(pi.hThread);
    try {
        auto name = L"Local\\BriefcaseNative.Prepared." + std::to_wstring(pi.dwProcessId);
        Handle ready(CreateEventW(nullptr, TRUE, FALSE, name.c_str()));
        if (!ready.value || GetLastError() == ERROR_ALREADY_EXISTS)
            throw std::runtime_error("Cannot create unique startup handshake");
        if (ResumeThread(thread.value) == DWORD(-1))
            throw std::runtime_error("Cannot resume server");
        HANDLE waits[] = {ready.value, process.value};
        auto result = WaitForMultipleObjects(2, waits, FALSE, timeout);
        if (result != WAIT_OBJECT_0)
            throw std::runtime_error("Server preparation failed or timed out; see Briefcase logs");
        if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0)
            throw std::runtime_error("Server exited after preparation");
        return pi.dwProcessId;
    } catch (...) {
        TerminateProcess(process.value, 119);
        WaitForSingleObject(process.value, 5000);
        throw;
    }
}
}
