#pragma once
#include <Briefcase/NativeHookApi.h>
#include <Briefcase/ClientModApi.h>
#include <Briefcase/StartupApi.h>
#include <Briefcase/UnrealApi.h>
#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace bc {
using Version = std::array<uint32_t, 3>;
Version version(const std::string &value);
struct Dependency {
    std::string id;
    Version minimum;
};
struct Manifest {
    std::string id, name, author, entry, environment;
    Version version;
    std::string phase = "ready";
    std::map<std::string, std::filesystem::path> ini_bindings;
    uint64_t capabilities{};
    std::vector<Dependency> dependencies;
    std::filesystem::path directory;
};
Manifest parse_manifest(const std::string &json);
std::vector<size_t> dependency_order(const std::vector<Manifest> &mods);
} // namespace bc
