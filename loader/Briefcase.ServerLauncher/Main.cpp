#include "LaunchWindows.hpp"
#include <fstream>
#include <vector>
int wmain(int argc, wchar_t** argv) {
    std::filesystem::path root;
    try {
        wchar_t self[32768]{};
        if (!GetModuleFileNameW(nullptr, self, 32768)) return 1;
        root = std::filesystem::path(self).parent_path();
        if (root.filename() != L"Win64" || root.parent_path().filename() != L"Binaries" ||
            root.parent_path().parent_path().filename() != L"DeceiveInc")
            throw std::runtime_error("Launcher must be installed in DeceiveInc/Binaries/Win64");
        if (argc > 1 && std::wstring(argv[1]) == L"--launch-child") {
            if (std::filesystem::exists(root / L"version.dll"))
                throw std::runtime_error("Remove the old Briefcase version.dll before using the server launcher");
            std::wstring args = L"-unattended -NoSplash -NOCONSOLE -nullrhi -nosound";
            for (int i = 2; i < argc; ++i) {
                std::wstring a(argv[i]), lower(a);
                for (auto& c : lower) c = towlower(c);
                if (lower == L"-log" || lower == L"-console" || lower.starts_with(L"-servergui"))
                    throw std::runtime_error("Graphical console arguments are not supported");
                if (lower == L"-unattended" || lower == L"-nosplash" || lower == L"-noconsole" || lower == L"-nullrhi" || lower == L"-nosound") continue;
                args += L" " + briefcase::launcher::quote(a);
            }
            auto pid = briefcase::launcher::start(root / L"DeceiveIncServer-Win64-Shipping.exe",
                                                  root / L"Briefcase/Runtime/Briefcase.ServerBootstrap.dll", args);
            printf("%lu\n", pid); return 0;
        }
        // Exit before updates: the coordinator waits for this process, allowing its EXE to be replaced.
        wchar_t system[32768]{}; GetSystemDirectoryW(system, 32768);
        auto powershell = std::filesystem::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
        auto script = root / L"Briefcase/Updater/Launch-Server.ps1";
        if (!std::filesystem::is_regular_file(script)) throw std::runtime_error("Missing update coordinator");
        std::wstring cmd = briefcase::launcher::quote(powershell.wstring()) +
            L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
            briefcase::launcher::quote(script.wstring()) + L" -LauncherProcessId " + std::to_wstring(GetCurrentProcessId());
        for (int i = 1; i < argc; ++i) cmd += L" " + briefcase::launcher::quote(argv[i]);
        STARTUPINFOW si{sizeof(si)}; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(powershell.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                            root.c_str(), &si, &pi)) throw std::runtime_error("Cannot start update coordinator");
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread); return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "%s\n", e.what());
        if (!root.empty()) {
            std::error_code ec; std::filesystem::create_directories(root / L"Briefcase/Logs", ec);
            std::ofstream(root / L"Briefcase/Logs/launcher-error.log", std::ios::app) << e.what() << '\n';
        }
        return 1;
    } catch (...) { return 1; }
}
