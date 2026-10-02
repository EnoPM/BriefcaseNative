#include <Briefcase/DeceiveInc/Paths.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
#include <stdexcept>

namespace briefcase::deceive {

std::filesystem::path module_directory(const void *symbol) {
    if (!symbol)
        throw std::invalid_argument("Module symbol is null");
#ifdef _WIN32
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(symbol), &module))
        throw std::runtime_error("Cannot resolve mod module");
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length == path.size())
        throw std::runtime_error("Cannot resolve mod path");
    path.resize(length);
    return std::filesystem::path(path).parent_path().parent_path();
#else
    Dl_info info{};
    if (!dladdr(symbol, &info) || !info.dli_fname)
        throw std::runtime_error("Cannot resolve mod path");
    return std::filesystem::canonical(info.dli_fname).parent_path().parent_path();
#endif
}

std::filesystem::path data_file(const void *symbol, const char *name) {
    if (!name || !*name)
        throw std::invalid_argument("Data filename is empty");
    return module_directory(symbol) / "Data" / name;
}

std::filesystem::path configuration_file(const void *symbol, const char *legacy_mod_id,
                                         const char *name) {
    if (!legacy_mod_id || !*legacy_mod_id)
        throw std::invalid_argument("Legacy mod ID is empty");
    const auto local = data_file(symbol, name);
    const auto mod = module_directory(symbol);
    const auto win64 = mod.parent_path().parent_path().parent_path();
    if (win64.filename() == "Win64") {
        const auto legacy = win64 / "Briefcase" / "Mods" / legacy_mod_id / "Data" / name;
        if (std::filesystem::is_regular_file(legacy))
            return legacy;
    }
    return local;
}

} // namespace briefcase::deceive
