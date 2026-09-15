#include "Platform.hpp"
#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace bc::platform {
uint32_t thread_id() noexcept {
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return static_cast<uint32_t>(gettid());
#endif
}
uint32_t process_id() noexcept {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<uint32_t>(getpid());
#endif
}
std::filesystem::path executable() {
#ifdef _WIN32
    wchar_t path[32768]{};
    auto size = GetModuleFileNameW(nullptr, path, 32768);
    if (!size || size == 32768) throw std::runtime_error("Cannot resolve executable path");
    return std::filesystem::path(path);
#else
    return std::filesystem::read_symlink("/proc/self/exe");
#endif
}
Module open_library(const std::filesystem::path& path) {
    if (!path.is_absolute()) throw std::runtime_error("Library path must be absolute");
#ifdef _WIN32
    auto module = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) throw std::runtime_error("Cannot load library: " + std::to_string(GetLastError()));
#else
    auto module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        const auto* error = dlerror();
        throw std::runtime_error(error ? error : "Cannot load library");
    }
#endif
    return module;
}
void* symbol(Module module, const char* name) noexcept {
    if (!module || !name) return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
#else
    return dlsym(module, name);
#endif
}
void close_library(Module module) noexcept {
    if (!module) return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(module));
#else
    dlclose(module);
#endif
}
}
