#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace bc::platform {
using Module = void*;
uint32_t thread_id() noexcept;
uint32_t process_id() noexcept;
std::filesystem::path executable();
Module open_library(const std::filesystem::path& path);
void* symbol(Module module, const char* name) noexcept;
void close_library(Module module) noexcept;
inline size_t bounded_length(const char* text, size_t limit) noexcept {
    if (!text) return 0;
    size_t size = 0;
    while (size < limit && text[size]) ++size;
    return size;
}
}
