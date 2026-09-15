#pragma once
#include "../Briefcase.Client.Input/Keys.hpp"
#include "I18n.hpp"
#include "KeyMap.hpp"
#include "KeybindState.hpp"
namespace bc::menu::keybind {
inline const char *mouse_translation(uint32_t key) {
    switch (key) {
    case 1:
        return "ui.keybind.mouse_left";
    case 2:
        return "ui.keybind.mouse_right";
    case 4:
        return "ui.keybind.mouse_middle";
    case 5:
        return "ui.keybind.mouse_4";
    case 6:
        return "ui.keybind.mouse_5";
    }
    return "";
}
inline std::string name(uint32_t key) {
    if (!key)
        return i18n::tr("ui.keybind.none", "None");
    if (input::mouse_key_code(key))
        return i18n::tr(mouse_translation(key), key == 1   ? "Mouse 1"
                                                : key == 2 ? "Mouse 2"
                                                : key == 4 ? "Mouse 3"
                                                : key == 5 ? "Mouse 4"
                                                           : "Mouse 5");
    UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    if (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN || key == VK_PRIOR ||
        key == VK_NEXT || key == VK_END || key == VK_HOME || key == VK_INSERT || key == VK_DELETE ||
        key == VK_DIVIDE || key == VK_NUMLOCK)
        scan |= 0x100;
    wchar_t wide[128]{};
    if (scan && GetKeyNameTextW(LONG(scan << 16), wide, 128) > 0) {
        char text[512]{};
        if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, sizeof(text), nullptr, nullptr) > 0)
            return text;
    }
    auto mapped = to_imgui(key);
    if (mapped != ImGuiKey_None)
        return ImGui::GetKeyName(mapped);
    char text[32]{};
    snprintf(text, sizeof(text), "VK %u", key);
    return text;
}
inline bool any_down() {
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
        if (ImGui::IsKeyDown(ImGuiKey(k)))
            return true;
    return false;
}
// One framework-owned ImGui context. Value is a Windows VK code, compatible with ClientModApi.
// Call begin_frame/end_frame around the owning menu. No hook, file IO or OS key injection.
inline bool draw(const char *id, uint32_t &value, bool allow_mouse = false, bool allow_none = false) {
    ImGui::PushID(id);
    const auto owner = ImGui::GetID("###Key");
    bool changed = false;
    bool mine = listening == owner && context == ImGui::GetCurrentContext();
    const float height = ImGui::GetFrameHeight(), gap = ImGui::GetStyle().ItemInnerSpacing.x;
    const float width = ImGui::CalcItemWidth();
    auto label = (mine ? i18n::tr("ui.keybind.listening", "Press a key…") : name(value)) + "###Key";
    const bool clicked =
        ImGui::Button(label.c_str(), {std::max(1.f, width - (allow_none ? height + gap : 0)), height});
    if (clicked && !mine) {
        listening = owner;
        context = ImGui::GetCurrentContext();
        seen_frame = ImGui::GetFrameCount();
        armed = !any_down();
        mine = true;
    } else if (mine) {
        seen_frame = ImGui::GetFrameCount();
        const auto &io = ImGui::GetIO();
        if (io.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            cancel();
        else if (!armed) {
            if (!any_down())
                armed = true;
        } else if (!io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper) {
            uint32_t candidate{};
            for (uint32_t k = 1; k < 256; ++k) {
                if (!input::keyboard_key(k) || k == VK_ESCAPE)
                    continue;
                auto key = to_imgui(k);
                if (key != ImGuiKey_None && ImGui::IsKeyPressed(key, false)) {
                    candidate = k;
                    break;
                }
            }
            if (allow_mouse && !candidate) {
                constexpr uint32_t keys[] = {1, 2, 4, 5, 6};
                for (int i = 0; i < 5; ++i)
                    if (ImGui::IsMouseClicked(i, false)) {
                        candidate = keys[i];
                        break;
                    }
            }
            if (candidate) {
                cancel();
                changed = value != candidate;
                value = candidate;
            }
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip(
            "%s", i18n::tr("ui.keybind.help", "Click, then press a key without modifiers. Escape cancels.")
                      .c_str());
    if (allow_none) {
        ImGui::SameLine(0, gap);
        if (ImGui::Button("×###Clear", {height, height})) {
            changed = value != 0;
            value = 0;
            if (mine)
                cancel();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", i18n::tr("ui.keybind.clear", "Clear shortcut").c_str());
    }
    ImGui::PopID();
    return changed;
}
} // namespace bc::menu::keybind
