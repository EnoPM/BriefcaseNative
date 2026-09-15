#pragma once
#include <imgui.h>
namespace bc::menu::keybind {
inline ImGuiID listening{};
inline ImGuiContext *context{};
inline int seen_frame = -1;
inline bool armed{};
inline void cancel() {
    listening = 0;
    context = nullptr;
    armed = false;
    seen_frame = -1;
}
inline bool active() {
    return listening != 0;
}
inline void begin_frame() {
    if (context && context != ImGui::GetCurrentContext())
        cancel();
    if (ImGui::GetIO().AppFocusLost)
        cancel();
}
inline void end_frame() {
    if (active() && seen_frame != ImGui::GetFrameCount())
        cancel();
}
} // namespace bc::menu::keybind
