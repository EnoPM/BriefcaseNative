#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <limits>
#include <nlohmann/json.hpp>
namespace bc::menu::numbers {
// The decimal editor exposes three fractional digits. Quantize only edited
// values, so step subtraction cannot persist binary residuals near zero.
inline double decimal_value(double value) {
    if (!std::isfinite(value) || std::abs(value) > 1e12)
        return value;
    auto rounded = std::round(value * 1000.) / 1000.;
    return rounded == 0 ? 0. : rounded;
}
inline bool step_button(const char *id, bool plus, float edge) {
    const bool pressed = ImGui::Button(id, {edge, edge});
    if (ImGui::IsItemVisible()) {
        const auto min = ImGui::GetItemRectMin();
        const ImVec2 center{min.x + edge * .5f, min.y + edge * .5f};
        const float radius = edge * .25f, stroke = edge / 16.f;
        const auto color = ImGui::GetColorU32(ImGuiCol_Text);
        auto *draw = ImGui::GetWindowDrawList();
        draw->AddLine({center.x - radius, center.y}, {center.x + radius, center.y}, color, stroke);
        if (plus)
            draw->AddLine({center.x, center.y - radius}, {center.x, center.y + radius}, color, stroke);
    }
    return pressed;
}
template <class Input, class Step> bool input_with_steps(const char *id, Input input, Step step) {
    const float edge = ImGui::GetFrameHeight(), spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float width = ImGui::CalcItemWidth();
    ImGui::BeginGroup();
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(std::max(1.f, width - 2 * (edge + spacing)));
    bool changed = input();
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    ImGui::SameLine(0, spacing);
    if (step_button("##Minus", false, edge)) {
        step(false);
        changed = true;
    }
    ImGui::SameLine(0, spacing);
    if (step_button("##Plus", true, edge)) {
        step(true);
        changed = true;
    }
    ImGui::PopItemFlag();
    ImGui::PopID();
    ImGui::EndGroup();
    return changed;
}
inline bool input_integer(const char *id, int &value) {
    return input_with_steps(
        id, [&] { return ImGui::InputInt("", &value, 0, 0); },
        [&](bool plus) {
            const int step = ImGui::GetIO().KeyCtrl ? 100 : 1;
            const auto next = static_cast<long long>(value) + (plus ? step : -step);
            value = int(std::clamp(next, static_cast<long long>(std::numeric_limits<int>::min()),
                                   static_cast<long long>(std::numeric_limits<int>::max())));
        });
}
inline bool input_decimal(const char *id, double &value) {
    const bool changed = input_with_steps(
        id, [&] { return ImGui::InputDouble("", &value, 0, 0, "%.3f"); },
        [&](bool plus) {
            const double step = ImGui::GetIO().KeyCtrl ? 1. : .1;
            value += plus ? step : -step;
        });
    if (changed)
        value = decimal_value(value);
    return changed;
}
inline std::string display(const nlohmann::json &value, const char *format = "%.6g") {
    if (value.is_number_float()) {
        char text[96]{};
        double n = value.get<double>();
        if (std::string_view(format) == "%.3f")
            n = decimal_value(n);
        std::snprintf(text, sizeof(text), format, n);
        return text;
    }
    if (value.is_string())
        return value.get<std::string>();
    return value.dump();
}
} // namespace bc::menu::numbers
