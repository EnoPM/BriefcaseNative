#pragma once
#include <array>
#include <cstdint>
namespace bc::input {
inline bool can_toggle(bool open, bool repeat, bool modified, bool focused) {
    return focused && !repeat && (open || !modified);
}
class Policy {
    bool open_{}, focused_ = true;
    std::array<bool, 256> forwarded_{}, blocked_{};

  public:
    bool open() const { return open_; }
    bool capturing() const { return open_ && focused_; }
    void focus(bool active) { focused_ = active; }
    void menu(bool value) { open_ = value; }
    bool toggle(bool repeat, bool modified) {
        if (!can_toggle(open_, repeat, modified, focused_))
            return false;
        open_ = !open_;
        return true;
    }
    void forwarded(unsigned key, bool down) {
        if (key < 256)
            forwarded_[key] = down;
    }
    bool release_on_open(unsigned key) {
        if (key >= 256 || !forwarded_[key])
            return false;
        forwarded_[key] = false;
        return true;
    }
    void suppress_until_release(unsigned key, bool physical_down) {
        if (key < 256)
            blocked_[key] = physical_down;
    }
    bool suppressed(unsigned key, bool physical_down) {
        if (key >= 256)
            return false;
        if (!physical_down)
            blocked_[key] = false;
        return blocked_[key];
    }
};
inline bool system_key(unsigned key) {
    return key == 0x12 || key == 0xA4 || key == 0xA5 || key == 0x10 || key == 0xA0 || key == 0xA1 ||
           key == 0x11 || key == 0xA2 || key == 0xA3 || key == 0x09 || key == 0x73 || key == 0x5B ||
           key == 0x5C;
}
inline bool shortcut(unsigned key, bool alt, bool shift, bool control, bool windows) {
    return windows || (alt && (key == 0x09 || key == 0x73 || key == 0x1B || key == 0x20 || key == 0x0D)) ||
           (shift && key == 0x09) || (control && alt);
}
} // namespace bc::input
