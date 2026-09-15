#pragma once
#include <cstdint>
namespace bc {
struct ExecutableImage { uint8_t* base{}; uint32_t size{}; };
ExecutableImage executable_image();
bool executable_range(const uint8_t* image, uint32_t size, uint32_t rva, uint32_t length, bool text_only = false);
}
