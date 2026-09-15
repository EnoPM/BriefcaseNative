#pragma once
#include <cstdint>
namespace bc::input {
inline constexpr uint32_t default_menu_key = 0x70;
inline bool keyboard_key(uint32_t k) {
    return (k >= '0' && k <= '9') || (k >= 'A' && k <= 'Z') || (k >= 0x70 && k <= 0x87) ||
           (k >= 0x60 && k <= 0x6f && k != 0x6c) || (k >= 0x21 && k <= 0x28) || (k >= 0xba && k <= 0xc0) ||
           (k >= 0xdb && k <= 0xde) || k == 0x08 || k == 0x09 || k == 0x0d || k == 0x13 || k == 0x14 ||
           k == 0x1b || k == 0x20 || k == 0x2c || k == 0x2d || k == 0x2e || k == 0x90 || k == 0x91 ||
           k == 0xe2;
}
inline bool mouse_key_code(uint32_t k) {
    return k == 1 || k == 2 || k == 4 || k == 5 || k == 6;
}
inline bool valid_menu_key(uint32_t k) {
    return keyboard_key(k) && k != 0x1b;
}
inline bool menu_key_event(uint32_t incoming, uint32_t binding, bool modified, bool focused,
                           bool binding_capture) {
    return incoming == binding && valid_menu_key(binding) && !modified && focused && !binding_capture;
}
} // namespace bc::input
