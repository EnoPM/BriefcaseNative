#include "../Briefcase.NativeHost/Startup.hpp"
#include "Management.hpp"
#include <Windows.h>
#include <shellapi.h>
namespace bc::admin {
namespace {
std::mutex restart_mutex;
HANDLE commit_event{};
HANDLE shutdown_thread{};
std::string restart_id;
uint64_t filetime(FILETIME t) {
    return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}
DWORD WINAPI shutdown_server(void *) {
    const auto suffix = std::to_wstring(GetCurrentProcessId());
    HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE,
                             (L"Local\\BriefcaseNative.Stop." + suffix).c_str());
    HANDLE stopped = OpenEventW(SYNCHRONIZE, FALSE,
                                (L"Local\\BriefcaseNative.Stopped." + suffix).c_str());
    if (stop)
        SetEvent(stop);
    if (stopped)
        WaitForSingleObject(stopped, 5000);
    if (stop)
        CloseHandle(stop);
    if (stopped)
        CloseHandle(stopped);
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
} // namespace
fs::path restart_helper_path(const fs::path &briefcase_root) {
    return briefcase_root / "Core" / "Tools" / "Briefcase.ServerRestart.exe";
}
Json schedule_restart(const fs::path &root) {
    std::lock_guard lock(restart_mutex);
    if (shutdown_thread)
        throw Error("conflict", "A server shutdown is already scheduled.");
    if (commit_event)
        return {{"scheduled", true}, {"requestId", restart_id}};
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, 32768);
    auto expected = root.parent_path() / "DeceiveIncServer-Win64-Shipping.exe";
    assert_plain_path(expected);
    if (_wcsicmp(expected.c_str(), exe) != 0)
        throw Error("unavailable", "Restart is only available for the dedicated server.");
    const auto helper = restart_helper_path(root);
    assert_plain_path(helper);
    if (!fs::is_regular_file(helper))
        throw Error("unavailable", "Restart helper is missing from the server package.");
    FILETIME created{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exit, &kernel, &user))
        throw Error("unavailable", "Process identity unavailable.");
    restart_id = hex(random_bytes(16));
    Json args = Json::array();
    int count{};
    auto argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!argv)
        throw Error("unavailable", "Server arguments unavailable.");
    for (int i = 1; i < count; ++i)
        args.push_back(fs::path(argv[i]).string());
    LocalFree(argv);
    auto record = Json{{"pid", GetCurrentProcessId()},
                       {"created", filetime(created)},
                       {"requestId", restart_id},
                       {"arguments", args}};
    write_file(root / "Admin" / "restart-request.json", record.dump(), true, true);
    auto base = L"Local\\BriefcaseNative.Restart." + fs::path(restart_id).wstring();
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, (base + L".Ready").c_str());
    commit_event = CreateEventW(nullptr, TRUE, FALSE, (base + L".Commit").c_str());
    if (!ready || !commit_event) {
        if (ready)
            CloseHandle(ready);
        if (commit_event)
            CloseHandle(commit_event);
        commit_event = nullptr;
        throw Error("unavailable", "Restart events unavailable.");
    }
    std::wstring command = L"\"" + helper.wstring() + L"\" " + fs::path(restart_id).wstring();
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    bool launched = CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                   nullptr, root.parent_path().c_str(), &si, &pi) != 0;
    bool ok = false;
    if (launched) {
        HANDLE handles[]{ready, pi.hProcess};
        ok = WaitForMultipleObjects(2, handles, FALSE, 5000) == WAIT_OBJECT_0;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    CloseHandle(ready);
    if (!ok) {
        CloseHandle(commit_event);
        commit_event = nullptr;
        throw Error("unavailable", "Could not prepare the restart. The server is still running.");
    }
    return {{"scheduled", true}, {"requestId", restart_id}};
}
Json schedule_shutdown(const fs::path &root) {
    std::lock_guard lock(restart_mutex);
    if (commit_event)
        throw Error("conflict", "A server restart is already scheduled.");
    if (shutdown_thread)
        return {{"scheduled", true}};
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, 32768);
    const auto expected = root.parent_path() / "DeceiveIncServer-Win64-Shipping.exe";
    assert_plain_path(expected);
    if (_wcsicmp(expected.c_str(), exe) != 0)
        throw Error("unavailable", "Shutdown is only available for the dedicated server.");
    const auto suffix = std::to_wstring(GetCurrentProcessId());
    HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE,
                             (L"Local\\BriefcaseNative.Stop." + suffix).c_str());
    HANDLE stopped = OpenEventW(SYNCHRONIZE, FALSE,
                                (L"Local\\BriefcaseNative.Stopped." + suffix).c_str());
    if (!stop || !stopped) {
        if (stop)
            CloseHandle(stop);
        if (stopped)
            CloseHandle(stopped);
        throw Error("unavailable", "Shutdown control is not ready.");
    }
    CloseHandle(stop);
    CloseHandle(stopped);
    shutdown_thread = CreateThread(nullptr, 0, shutdown_server, nullptr, CREATE_SUSPENDED, nullptr);
    if (!shutdown_thread)
        throw Error("unavailable", "Could not prepare the server shutdown.");
    return {{"scheduled", true}};
}
void commit_restart() noexcept {
    std::lock_guard lock(restart_mutex);
    if (commit_event)
        SetEvent(commit_event);
    else if (shutdown_thread) {
        ResumeThread(shutdown_thread);
        CloseHandle(shutdown_thread);
        shutdown_thread = nullptr;
    }
}
} // namespace bc::admin
