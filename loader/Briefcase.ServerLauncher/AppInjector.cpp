#include "LaunchWindows.hpp"
#include <algorithm>
#include <cwctype>
#include <cstdio>
#include <string_view>

// ServerApp performs every update and configuration step before calling this helper.
// The helper only starts Shipping with Briefcase's early bootstrap injected.
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 3 || std::wstring_view(argv[1]) != L"--server")
            throw std::runtime_error("Usage: Briefcase.ServerInjector --server <Shipping.exe> [server arguments]");
        wchar_t self[32768]{};
        if (!GetModuleFileNameW(nullptr, self, 32768))
            throw std::runtime_error("Cannot resolve injector path");
        const auto tools = std::filesystem::path(self).parent_path();
        const auto game = std::filesystem::absolute(argv[2]).lexically_normal();
        const auto root = game.parent_path();
        if (game.filename() != L"DeceiveIncServer-Win64-Shipping.exe" ||
            root.filename() != L"Win64" || root.parent_path().filename() != L"Binaries" ||
            root.parent_path().parent_path().filename() != L"DeceiveInc")
            throw std::runtime_error("Invalid dedicated server executable path");
        const auto bootstrap = tools / L"Briefcase.ServerBootstrap.dll";
        std::wstring arguments = L"-unattended -NoSplash -NOCONSOLE -nullrhi -nosound";
        std::wstring instance_root, instance_id;
        for (int index = 3; index < argc; ++index) {
            std::wstring value(argv[index]), lower(value);
            for (auto& ch : lower) ch = std::towlower(ch);
            if (lower == L"--instance-root" || lower == L"--instance-id") {
                if (++index == argc) throw std::runtime_error("Missing instance argument value");
                (lower == L"--instance-root" ? instance_root : instance_id) = argv[index];
                continue;
            }
            if (lower == L"-log" || lower == L"-console" || lower.starts_with(L"-servergui"))
                throw std::runtime_error("Graphical server arguments are unsupported");
            if (lower == L"-unattended" || lower == L"-nosplash" || lower == L"-noconsole" ||
                lower == L"-nullrhi" || lower == L"-nosound") continue;
            arguments += L" " + briefcase::launcher::quote(value);
        }
        // An older proxy may still be present. It must forward version APIs only;
        // the injected bootstrap alone owns Briefcase initialization.
        if (!SetEnvironmentVariableW(L"BRIEFCASE_EXTERNAL_BOOTSTRAP", L"1"))
            throw std::runtime_error("Cannot set bootstrap marker");
        if (instance_root.empty() != instance_id.empty())
            throw std::runtime_error("Instance root and ID must be supplied together");
        if (!instance_root.empty()) {
            if (instance_id.size() != 32 ||
                !std::all_of(instance_id.begin(), instance_id.end(), [](wchar_t ch) {
                    return (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f');
                }))
                throw std::runtime_error("Invalid instance ID");
            const auto instance = std::filesystem::absolute(instance_root).lexically_normal();
            if (!std::filesystem::is_directory(instance))
                throw std::runtime_error("Instance directory is missing");
            if (!SetEnvironmentVariableW(L"BRIEFCASE_INSTANCE_ROOT", instance.c_str()) ||
                !SetEnvironmentVariableW(L"BRIEFCASE_INSTANCE_ID", instance_id.c_str()))
                throw std::runtime_error("Cannot set instance context");
        }
        const auto pid = briefcase::launcher::start(game, bootstrap, arguments);
        std::printf("%lu\n", pid);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
