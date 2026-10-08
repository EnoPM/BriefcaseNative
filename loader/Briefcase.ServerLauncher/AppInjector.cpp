#include "LaunchWindows.hpp"
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
        for (int index = 3; index < argc; ++index) {
            std::wstring value(argv[index]), lower(value);
            for (auto& ch : lower) ch = std::towlower(ch);
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
        const auto pid = briefcase::launcher::start(game, bootstrap, arguments);
        std::printf("%lu\n", pid);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
