#pragma once
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <imgui.h>
namespace bc::menu {
// Static lifetime: ImGui retains these ranges until the lazy atlas is built.
inline constexpr ImWchar menu_glyph_ranges[]{0x0020, 0x024F, 0x0370, 0x052F, 0x2000, 0x206F, 0};
inline void load_fonts(const char *regular, const char *bold, float pixels) {
    auto &io = ImGui::GetIO();
    io.FontDefault = io.Fonts->AddFontFromFileTTF(regular, pixels, nullptr, menu_glyph_ranges);
    if (!io.FontDefault)
        io.FontDefault = io.Fonts->AddFontDefault();
    if (std::filesystem::is_regular_file(std::filesystem::u8path(bold))) {
        ImFontConfig config;
        std::strcpy(config.Name, "Briefcase.Bold");
        io.Fonts->AddFontFromFileTTF(bold, pixels, &config, menu_glyph_ranges);
    }
}
inline ImFont *bold_font() {
    // Resolve from the current context: no stale pointer after a device/context rebuild.
    for (auto *font : ImGui::GetIO().Fonts->Fonts)
        if (std::strcmp(font->GetDebugName(), "Briefcase.Bold") == 0)
            return font;
    return ImGui::GetFont();
}
class BoldFont {
  public:
    BoldFont() { ImGui::PushFont(bold_font()); }
    ~BoldFont() { ImGui::PopFont(); }
    BoldFont(const BoldFont &) = delete;
    BoldFont &operator=(const BoldFont &) = delete;
};
inline void bold_text(const char *text) {
    BoldFont font;
    ImGui::TextUnformatted(text);
}
inline void table_headers() {
    BoldFont font;
    const auto background = ImGui::GetStyleColorVec4(ImGuiCol_TableHeaderBg);
    ImGui::PushStyleColor(ImGuiCol_Header, background);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, background);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, background);
    ImGui::TableHeadersRow();
    ImGui::PopStyleColor(3);
}
inline bool table_checkbox(const char *label, bool *value, float dpi) {
    const auto padding = ImGui::GetStyle().FramePadding;
    const float inset = std::max(0.f, padding.y - 2 * dpi);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + inset);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {padding.x, 2 * dpi});
    const bool changed = ImGui::Checkbox(label, value);
    ImGui::PopStyleVar();
    return changed;
}
} // namespace bc::menu
