// Offline compatibility probe. Reports candidates only; never loads or patches the game.
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

// C boundary from the pinned Linux patternsleuth_bind dependency.
struct PsEngineVersion { uint16_t major{}, minor{}; };
struct PsFileResolutionResults {
    PsEngineVersion engine_version{};
    uint64_t guobject_array{}, fname_tostring{}, fname_ctor_wchar{}, gmalloc{};
    uint64_t static_construct_object_internal{}, ftext_fstring{}, fuobject_hash_tables_get{};
    uint64_t gnatives{}, console_manager_singleton{}, gameengine_tick{};
};
static_assert(offsetof(PsFileResolutionResults, guobject_array) == 8);
static_assert(sizeof(PsFileResolutionResults) == 88);
extern "C" bool ps_scan_file_ue4ss(const char *, PsFileResolutionResults *);

int main(int argc, char **argv) {
    if (argc != 2) { std::cerr << "Usage: Briefcase.LinuxUnrealScan <server ELF>\n"; return 2; }
    try {
        std::ifstream file(argv[1], std::ios::binary);
        std::array<unsigned char, 64> header{};
        if (!file.read(reinterpret_cast<char *>(header.data()), header.size()) ||
            header[0] != 0x7f || header[1] != 'E' || header[2] != 'L' || header[3] != 'F' ||
            header[4] != 2 || header[5] != 1 || header[6] != 1 ||
            (header[16] != 2 && header[16] != 3) || header[17] != 0 ||
            header[18] != 62 || header[19] != 0 ||
            header[20] != 1 || header[21] != 0 || header[22] != 0 || header[23] != 0 ||
            header[52] != 64 || header[53] != 0)
            throw std::runtime_error("Expected an ELF64 little-endian x86_64 executable");
        PsFileResolutionResults result;
        auto start = std::chrono::steady_clock::now();
        if (!ps_scan_file_ue4ss(argv[1], &result))
            throw std::runtime_error("Scanner could not inspect the ELF image");
        const std::array entries{
            std::pair{"GUObjectArray", result.guobject_array},
            std::pair{"FNameToString", result.fname_tostring},
            std::pair{"FNameCtorWchar", result.fname_ctor_wchar},
            std::pair{"GMalloc", result.gmalloc},
            std::pair{"StaticConstructObjectInternal", result.static_construct_object_internal},
            std::pair{"FTextFString", result.ftext_fstring},
            std::pair{"GNatives", result.gnatives},
            std::pair{"ConsoleManagerSingleton", result.console_manager_singleton},
            std::pair{"UGameEngineTick", result.gameengine_tick}};
        nlohmann::json report{{"engineMajor", result.engine_version.major},
                              {"engineMinor", result.engine_version.minor},
                              {"runtimeValidated", false},
                              {"missing", nlohmann::json::array()}};
        for (const auto &[name, address] : entries) {
            report["candidates"][name] = address;
            if (!address) report["missing"].push_back(name);
        }
        report["optional"]["FUObjectHashTablesGet"] = result.fuobject_hash_tables_get;
        report["scanMilliseconds"] = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        bool complete = result.engine_version.major == 4 && result.engine_version.minor == 27 &&
                        report["missing"].empty();
        report["readyForRuntimeValidation"] = complete;
        std::cout << report.dump(2) << '\n';
        return complete ? 0 : 3;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
