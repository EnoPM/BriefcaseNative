#pragma once
#include "../Briefcase.Localization/Catalog.hpp"
#include "Menu.hpp"
#include <Windows.h>
#include <cstdio>
#include <imgui.h>
#include <map>
#include <vector>
namespace bc::menu::i18n {
using Json = nlohmann::json;
inline Json state = {{"language", "fr"}, {"catalogues", Json::object()}, {"languages", Json::object()}};
inline Json remote = Json::object();
inline uint64_t sequence{};
inline bool remote_active{};
inline std::vector<char> buffer;
inline std::map<std::string, std::string> cache;
inline std::string language() {
    return state.value("language", "fr");
}
inline void poll(const BcClientHostApi &host) {
    if (!host.locale_snapshot)
        return;
    if (buffer.empty())
        buffer.resize(1048576);
    buffer[0] = 0;
    if (host.locale_snapshot(buffer.data(), uint32_t(buffer.size()), &sequence) == BC_OK && buffer[0]) {
        auto next = Json::parse(buffer.data());
        locale::validate_catalogues(next.at("catalogues"), false);
        state = std::move(next);
        cache.clear();
    }
}
inline void set_remote(const Json &next) {
    if (next != remote) {
        remote = next;
        cache.clear();
    }
}
inline void use_remote(bool active) {
    remote_active = active;
}
inline const std::string &tr(const std::string &key, const std::string &fallback) {
    auto cache_key = std::string(remote_active ? "remote\n" : "local\n") + key + "\n" + fallback;
    auto it = cache.find(cache_key);
    if (it != cache.end())
        return it->second;
    auto find = [&](const Json &cat, const std::string &lang) -> std::string {
        if ((&cat != &remote || remote_active) && cat.contains(lang) && cat[lang].contains(key))
            return cat[lang][key].get<std::string>();
        return {};
    };
    auto lang = language();
    std::string value = find(remote, lang);
    if (value.empty())
        value = find(state["catalogues"], lang);
    if (value.empty() && lang.find('-') != std::string::npos) {
        auto base = lang.substr(0, lang.find('-'));
        value = find(remote, base);
        if (value.empty())
            value = find(state["catalogues"], base);
    }
    if (value.empty())
        value = find(remote, "en");
    if (value.empty())
        value = find(state["catalogues"], "en");
    if (value.empty())
        value = fallback;
    return cache.emplace(std::move(cache_key), std::move(value)).first->second;
}
inline std::string label(const std::string &key, const std::string &fallback) {
    return tr(key, fallback) + "###" + key;
}
inline std::vector<std::string> tokens(std::string_view s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == '%') {
            auto start = i++;
            if (i < s.size() && s[i] == '%')
                continue;
            while (i < s.size() &&
                   std::string_view("-+ #0'123456789.hlztjL").find(s[i]) != std::string_view::npos)
                ++i;
            if (i >= s.size() || std::string_view("diuoxXfFeEgGaAcsp").find(s[i]) == std::string_view::npos)
                throw std::runtime_error("Invalid translation format");
            out.emplace_back(s.substr(start, i - start + 1));
        }
    return out;
}
template <class... Args> std::string format(const char *key, const char *fallback, Args... args) {
    auto &value = tr(key, fallback);
    const char *selected = value.c_str();
    try {
        if (tokens(value) != tokens(fallback))
            selected = fallback;
    } catch (...) {
        selected = fallback;
    }
    int n = std::snprintf(nullptr, 0, selected, args...);
    if (n < 0 || n > 32768)
        return fallback;
    std::string text(size_t(n) + 1, 0);
    std::snprintf(text.data(), text.size(), selected, args...);
    text.resize(n);
    return text;
}
template <class... Args> void text(const char *k, const char *f, Args... a) {
    auto s = format(k, f, a...);
    ImGui::TextUnformatted(s.c_str());
}
template <class... Args> void disabled(const char *k, const char *f, Args... a) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    text(k, f, a...);
    ImGui::PopStyleColor();
}
template <class... Args> void wrapped(const char *k, const char *f, Args... a) {
    ImGui::PushTextWrapPos(0);
    text(k, f, a...);
    ImGui::PopTextWrapPos();
}
template <class... Args> void colored(ImVec4 c, const char *k, const char *f, Args... a) {
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    text(k, f, a...);
    ImGui::PopStyleColor();
}
template <class... Args> void tooltip(const char *k, const char *f, Args... a) {
    if (ImGui::BeginTooltip()) {
        text(k, f, a...);
        ImGui::EndTooltip();
    }
}
inline std::string display(const Json &metadata, const std::string &fallback) {
    auto label = metadata.value("displayName", locale::humanize(fallback));
    auto key = metadata.value("displayNameKey", std::string{});
    return key.empty() ? label : tr(key, label);
}
inline std::string fold(const std::string &s) {
    if (s.empty())
        return {};
    auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!count)
        return s;
    std::wstring in(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), in.data(), count);
    auto n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, in.data(), count, nullptr, 0, nullptr,
                           nullptr, 0);
    std::wstring out(n, 0);
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, in.data(), count, out.data(), n, nullptr, nullptr,
                  0);
    auto bytes = WideCharToMultiByte(CP_UTF8, 0, out.data(), n, nullptr, 0, nullptr, nullptr);
    std::string result(bytes, 0);
    WideCharToMultiByte(CP_UTF8, 0, out.data(), n, result.data(), bytes, nullptr, nullptr);
    return result;
}
inline bool matches(const std::string &filter, const std::string &label, const std::string &key,
                    const Json &value, bool secret = false) {
    if (filter.empty())
        return true;
    auto haystack = label + " " + key;
    if (!secret) {
        if (value.is_boolean())
            haystack += " " + tr(value.get<bool>() ? "ui.yes" : "ui.no", value.get<bool>() ? "Yes" : "No") +
                        " " + value.dump();
        else
            haystack += " " + (value.is_string() ? value.get<std::string>() : value.dump());
    }
    return fold(haystack).find(fold(filter)) != std::string::npos;
}
} // namespace bc::menu::i18n
namespace bc::menu {
inline void notice(const char *id, const std::string &kind, const std::string &message, float dpi) {
    if (message.empty())
        return;
    ImVec4 color = kind == "danger"    ? ImVec4{.94f, .36f, .4f, 1}
                   : kind == "success" ? ImVec4{.36f, .78f, .54f, 1}
                   : kind == "warning" ? ImVec4{.96f, .70f, .30f, 1}
                                       : ImVec4{.48f, .65f, .94f, 1};
    ImGui::PushStyleColor(ImGuiCol_Border, color);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {color.x * .16f, color.y * .16f, color.z * .16f, 1});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14 * dpi, 10 * dpi});
    ImGui::BeginChild(id, {0, 0},
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", message.c_str());
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::Spacing();
}
} // namespace bc::menu
