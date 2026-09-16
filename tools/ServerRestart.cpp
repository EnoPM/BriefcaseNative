#include "../runtime/Briefcase.Admin/Service.hpp"
#include "../runtime/Briefcase.NativeHost/Configuration.hpp"
#include "../runtime/Briefcase.NativeHost/Startup.hpp"
#include <Windows.h>
#include <fstream>
using namespace bc::admin;
namespace {
struct Handle {
    HANDLE h{};
    ~Handle() {
        if (h && h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
};
uint64_t value(FILETIME t) {
    return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}
std::wstring quote(const std::wstring &s) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (auto c : s) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"')
            out += L'\\';
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    fs::path root;
    try {
        if (argc != 2 || wcslen(argv[1]) != 32)
            throw std::runtime_error("Invalid restart ticket");
        wchar_t module[32768]{};
        GetModuleFileNameW(nullptr, module, 32768);
        root = fs::path(module).parent_path();
        while (!root.empty() && root.filename() != L"Briefcase" && root != root.root_path())
            root = root.parent_path();
        if (root.filename() != L"Briefcase")
            throw std::runtime_error("Restart helper is outside Briefcase");
        const auto win64 = root.parent_path(), exe = win64 / "DeceiveIncServer-Win64-Shipping.exe";
        bc::assert_plain_path(exe);
        bc::assert_plain_path(root / "Admin");
        if (win64.filename() != L"Win64" || win64.parent_path().filename() != L"Binaries" ||
            win64.parent_path().parent_path().filename() != L"DeceiveInc")
            throw std::runtime_error("Invalid server directory");
        auto record = bc::strict_json(read_file(root / "Admin" / "restart-request.json"));
        auto id = record.at("requestId").get<std::string>();
        if (fs::path(id).wstring() != argv[1] || unhex(id).size() != 16)
            throw std::runtime_error("Restart ticket mismatch");
        auto pid = record.at("pid").get<DWORD>();
        Handle process{
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid)};
        if (!process.h)
            throw std::runtime_error("Server process unavailable");
        wchar_t path[32768]{};
        DWORD size = 32768;
        FILETIME created{}, exit{}, kernel{}, user{};
        if (!QueryFullProcessImageNameW(process.h, 0, path, &size) || _wcsicmp(path, exe.c_str()) != 0 ||
            !GetProcessTimes(process.h, &created, &exit, &kernel, &user) ||
            value(created) != record.at("created").get<uint64_t>())
            throw std::runtime_error("Server process identity changed");
        auto arguments = record.at("arguments");
        if (!arguments.is_array() || arguments.size() > 64)
            throw std::runtime_error("Invalid arguments");
        auto base = L"Local\\BriefcaseNative.Restart." + fs::path(id).wstring();
        Handle ready{OpenEventW(EVENT_MODIFY_STATE, FALSE, (base + L".Ready").c_str())},
            commit{OpenEventW(SYNCHRONIZE, FALSE, (base + L".Commit").c_str())};
        if (!ready.h || !commit.h)
            throw std::runtime_error("Restart requester absent");
        for (auto &arg : arguments) {
            const auto a = arg.get<std::string>();
            if (a.size() > 4096 || a.find('\0') != a.npos || a.find_first_of(" \t\r\n\"") != a.npos)
                throw std::runtime_error("Invalid launcher argument");
        }
        std::wstring command;
        const auto launcher = win64 / "Briefcase.ServerLauncher.exe";
        bc::assert_plain_path(launcher);
        if (!fs::is_regular_file(launcher))
            throw std::runtime_error("Updated server launcher is missing");
        bc::assert_plain_path(exe);
        SetEvent(ready.h);
        // No process is stopped until the TLS acknowledgement has been sent.
        if (WaitForSingleObject(commit.h, 15000) != WAIT_OBJECT_0)
            throw std::runtime_error("Restart was not acknowledged");
        auto suffix = std::to_wstring(pid);
        Handle stop{
            OpenEventW(EVENT_MODIFY_STATE, FALSE, (L"Local\\BriefcaseNative.Stop." + suffix).c_str())},
            stopped{OpenEventW(SYNCHRONIZE, FALSE, (L"Local\\BriefcaseNative.Stopped." + suffix).c_str())};
        if (stop.h)
            SetEvent(stop.h);
        if (stopped.h)
            WaitForSingleObject(stopped.h, 5000);
        if (WaitForSingleObject(process.h, 500) != WAIT_OBJECT_0) {
            if (!TerminateProcess(process.h, 0))
                throw std::runtime_error("Server stop failed");
            if (WaitForSingleObject(process.h, 10000) != WAIT_OBJECT_0)
                throw std::runtime_error("Server has not exited");
        }
        command = quote(launcher.wstring()) + L" --restart " + fs::path(id).wstring() +
                  L" --wait-parent " + std::to_wstring(GetCurrentProcessId());
        for (const auto &argument : arguments)
            command += L" " + quote(fs::path(argument.get<std::string>()).wstring());
        // Write first: the child waits for our exit and then records the actual new server PID.
        write_file(root / "Admin" / "restart-result.json",
                   Json{{"requestId", id},
                        {"previousPid", pid},
                        {"workingDirectory", win64.string()},
                        {"state", "starting"}}
                           .dump(2) +
                       "\n",
                   true, true);
        STARTUPINFOW si{sizeof(si)};
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(launcher.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, win64.c_str(), &si, &pi))
            throw std::runtime_error("Server launcher failed");
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 0;
    } catch (const std::exception &e) {
        if (!root.empty())
            try {
                write_file(root / "Admin" / "restart-result.json",
                           Json{{"state", "error"}, {"message", e.what()}}.dump(), true, true);
            } catch (...) {
            }
        return 1;
    }
}
