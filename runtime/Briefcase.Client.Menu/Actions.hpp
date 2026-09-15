#pragma once
#include <imgui.h>
namespace bc::menu {
enum class ActionTone { neutral, success, info, danger };
struct ActionColors {
    ImVec4 normal, hovered, active;
};
inline ActionColors action_colors(ActionTone tone) {
    switch (tone) {
    case ActionTone::success:
        return {{.10f, .38f, .24f, 1}, {.13f, .48f, .30f, 1}, {.09f, .31f, .19f, 1}};
    case ActionTone::info:
        return {{.12f, .31f, .57f, 1}, {.16f, .40f, .70f, 1}, {.09f, .25f, .46f, 1}};
    case ActionTone::danger:
        return {{.56f, .16f, .22f, 1}, {.69f, .21f, .28f, 1}, {.45f, .11f, .17f, 1}};
    default:
        return {ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered),
                ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)};
    }
}
// Scoped to one control: semantic colors must never leak into inputs or navigation.
class ActionStyle {
  public:
    explicit ActionStyle(ActionTone tone) {
        auto colors = action_colors(tone);
        ImGui::PushStyleColor(ImGuiCol_Button, colors.normal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.hovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors.active);
    }
    ~ActionStyle() { ImGui::PopStyleColor(3); }
    ActionStyle(const ActionStyle &) = delete;
    ActionStyle &operator=(const ActionStyle &) = delete;
};
inline bool action_button(ActionTone tone, const char *label, ImVec2 size = {0, 0}) {
    ActionStyle style(tone);
    return ImGui::Button(label, size);
}
} // namespace bc::menu
