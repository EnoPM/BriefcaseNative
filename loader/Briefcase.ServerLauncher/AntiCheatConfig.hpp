#pragma once
#include "Update.hpp"
#include <regex>
#include <string_view>

namespace bc::launcher {
inline bool ensure_eac_disabled(const fs::path &binary_root, std::string_view config_platform) {
    require(config_platform == "WindowsServer" || config_platform == "LinuxServer",
            "Invalid server configuration platform");
    const auto path = plain(binary_root.parent_path().parent_path() / "Saved/Config" /
                            fs::path(config_platform) / "Engine.ini");
    plain(path.parent_path());
    fs::create_directories(path.parent_path());
    plain(path.parent_path());
    std::string text;
    unsigned mode = 0644;
    if (fs::exists(path)) {
        text = read(path, 2 * 1024 * 1024);
        mode = file_mode(path);
    }
    const std::regex setting("(^|\\r?\\n)[ \\t]*sb\\.DisableEAC[ \\t]*=[^\\r\\n]*",
                             std::regex::icase);
    const std::string replacement = "$1sb.DisableEAC=1";
    std::string updated;
    if (std::regex_search(text, setting))
        updated = std::regex_replace(text, setting, replacement);
    else {
        updated = text;
        if (!updated.empty() && updated.back() != '\n')
            updated += "\n";
        updated += "\n[ConsoleVariables]\nsb.DisableEAC=1\n";
    }
    if (updated == text)
        return false;
    atomic(path, updated, mode);
    return true;
}
} // namespace bc::launcher
