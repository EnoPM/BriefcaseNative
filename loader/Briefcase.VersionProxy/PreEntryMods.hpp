#pragma once

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace briefcase::loader {
inline std::string_view trim_ascii(std::string_view text) {
    const auto spaces = " \t\r\n";
    const auto first = text.find_first_not_of(spaces);
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(spaces) - first + 1);
}

inline bool safe_mod_name(std::string_view name) {
    if (name.empty() || name.size() > 64) return false;
    for (const unsigned char ch : name)
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) return false;
    return true;
}

// Both Windows entry paths must run enabled pre-entry mods before UE4SS and
// before the game's entry point reads its dedicated-server configuration.
inline bool load_preentry_mods(const std::filesystem::path &win64) {
    try {
        const auto mods = win64 / L"ue4ss" / L"Mods";
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
} // namespace briefcase::loader
