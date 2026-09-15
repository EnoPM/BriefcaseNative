#pragma once
#include "Actions.hpp"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <string>
// Original vector icons, drawn in the framework context; no font, atlas or texture dependency.
namespace bc::menu {
enum class Icon {
    home,
    mods,
    servers,
    add,
    remove,
    join,
    close,
    admin,
    back,
    refresh,
    settings,
    disconnect,
    balance
};
inline void icon(Icon glyph, ImVec2 origin, float size, ImU32 color) {
    auto *draw = ImGui::GetWindowDrawList();
    const float scale = size / 24.f, stroke = 1.7f * scale;
    auto point = [&](float x, float y) { return ImVec2{origin.x + x * scale, origin.y + y * scale}; };
    auto line = [&](float x, float y, float a, float b) {
        draw->AddLine(point(x, y), point(a, b), color, stroke);
    };
    auto box = [&](float x, float y, float a, float b, float rounding = 1.5f) {
        draw->AddRect(point(x, y), point(a, b), color, rounding * scale, 0, stroke);
    };
    switch (glyph) {
    case Icon::disconnect:
        line(10, 4, 3, 4);
        line(3, 4, 3, 20);
        line(3, 20, 10, 20);
        line(9, 12, 22, 12);
        line(17, 7, 22, 12);
        line(22, 12, 17, 17);
        break;
    case Icon::balance:
        line(12, 3, 12, 21);
        line(7, 21, 17, 21);
        line(3, 7, 21, 7);
        line(6, 7, 2, 15);
        line(6, 7, 10, 15);
        line(2, 15, 10, 15);
        line(18, 7, 14, 15);
        line(18, 7, 22, 15);
        line(14, 15, 22, 15);
        break;
    case Icon::settings:
        line(3, 6, 21, 6);
        line(3, 12, 21, 12);
        line(3, 18, 21, 18);
        box(7, 3, 11, 9);
        box(14, 9, 18, 15);
        box(5, 15, 9, 21);
        break;
    case Icon::home:
        line(3, 11, 12, 3);
        line(12, 3, 21, 11);
        line(5, 10, 5, 21);
        line(5, 21, 19, 21);
        line(19, 21, 19, 10);
        line(9, 21, 9, 14);
        line(9, 14, 15, 14);
        line(15, 14, 15, 21);
        break;
    case Icon::mods:
        box(3, 3, 10, 10);
        box(14, 3, 21, 10);
        box(3, 14, 10, 21);
        box(14, 14, 21, 21);
        break;
    case Icon::servers:
        box(3, 3, 21, 10);
        box(3, 14, 21, 21);
        draw->AddCircleFilled(point(7, 6.5f), scale, color);
        draw->AddCircleFilled(point(7, 17.5f), scale, color);
        line(12, 6.5f, 17, 6.5f);
        line(12, 17.5f, 17, 17.5f);
        break;
    case Icon::add:
        line(12, 4, 12, 20);
        line(4, 12, 20, 12);
        break;
    case Icon::remove:
        line(4, 6, 20, 6);
        line(9, 3, 15, 3);
        line(7, 6, 8, 21);
        line(8, 21, 16, 21);
        line(16, 21, 17, 6);
        line(10, 10, 10.5f, 17);
        line(14, 10, 13.5f, 17);
        break;
    case Icon::join:
        line(13, 4, 21, 4);
        line(21, 4, 21, 20);
        line(21, 20, 13, 20);
        line(3, 12, 16, 12);
        line(11, 7, 16, 12);
        line(16, 12, 11, 17);
        break;
    case Icon::admin:
        line(12, 3, 21, 7);
        line(21, 7, 19, 16);
        line(19, 16, 12, 22);
        line(12, 22, 5, 16);
        line(5, 16, 3, 7);
        line(3, 7, 12, 3);
        draw->AddCircle(point(12, 11), 2 * scale, color, 0, stroke);
        line(12, 13, 12, 17);
        break;
    case Icon::back:
        line(20, 12, 4, 12);
        line(4, 12, 10, 6);
        line(4, 12, 10, 18);
        break;
    case Icon::refresh:
        draw->PathArcTo(point(12, 12), 8 * scale, .35f, 5.1f, 22);
        draw->PathStroke(color, 0, stroke);
        line(15, 3, 16, 8);
        line(16, 8, 21, 7);
        break;
    case Icon::close:
        line(6, 6, 18, 18);
        line(6, 18, 18, 6);
        break;
    }
}
inline bool icon_button(const char *id, Icon glyph, const char *tooltip, float dpi,
                        ActionTone tone = ActionTone::neutral) {
    ActionStyle style(tone);
    const float edge = 36 * dpi, symbol = 20 * dpi;
    const auto position = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::Button(id, {edge, edge});
    icon(glyph, {position.x + (edge - symbol) / 2, position.y + (edge - symbol) / 2}, symbol,
         ImGui::GetColorU32(ImGuiCol_Text));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tooltip);
    return pressed;
}
inline bool icon_tab(const char *label, Icon glyph, float dpi) {
    const float symbol = 18 * dpi, gap = 7 * dpi;
    // Reserve label space using the current font, while keeping the original ### ID.
    const auto spaces = std::max(1, int(std::ceil((symbol + gap) / ImGui::CalcTextSize(" ").x)));
    const auto padded = std::string(spaces, ' ') + label;
    const bool selected = ImGui::BeginTabItem(padded.c_str());
    if (ImGui::IsItemVisible()) {
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        auto *draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(min, max, true);
        icon(glyph, {min.x + ImGui::GetStyle().FramePadding.x, min.y + (max.y - min.y - symbol) / 2}, symbol,
             ImGui::GetColorU32(ImGuiCol_Text));
        draw->PopClipRect();
    }
    return selected;
}
inline bool navigation(const char *id, const char *label, Icon glyph, bool selected, float dpi) {
    const auto &style = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? style.Colors[ImGuiCol_Header] : ImVec4{0, 0, 0, 0});
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float height = 44 * dpi, symbol = 20 * dpi, inset = 12 * dpi;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    const bool pressed = ImGui::Button(id, {ImGui::GetContentRegionAvail().x, height});
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    const auto color = ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    icon(glyph, {position.x + inset, position.y + (height - symbol) / 2}, symbol, color);
    const auto text = ImGui::CalcTextSize(label);
    ImGui::GetWindowDrawList()->AddText(
        {position.x + inset + symbol + 10 * dpi, position.y + (height - text.y) / 2}, color, label);
    return pressed;
}
} // namespace bc::menu
