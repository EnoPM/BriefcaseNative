#pragma once

#include <filesystem>

namespace briefcase::deceive {

// Resolves the DLL containing symbol and returns its UE4SS mod directory.
// This keeps configuration beside the mod on Windows and Linux.
[[nodiscard]] std::filesystem::path module_directory(const void *symbol);
[[nodiscard]] std::filesystem::path data_file(const void *symbol, const char *name);
// During migration, prefer the existing Briefcase package configuration so
// ServerManager and automatic mod updates keep editing the authoritative file.
[[nodiscard]] std::filesystem::path configuration_file(const void *symbol,
                                                       const char *legacy_mod_id,
                                                       const char *name);

} // namespace briefcase::deceive
