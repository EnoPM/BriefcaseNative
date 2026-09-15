#pragma once
#include "Font.hpp"
#include "I18n.hpp"
#include "Icons.hpp"
#include "Keybind.hpp"
#include "Maps.hpp"
#include "Menu.hpp"
#include "Numbers.hpp"
#include <Windows.h>
#include <map>
#include <nlohmann/json.hpp>
#include <vector>
namespace bc::menu::administration {
using Json = nlohmann::json;
struct Draft {
    std::string revision;
    Json values;
};
inline void invalidate_page_session();
inline uint64_t selected{}, sequence{}, form_selected{};
inline std::string selected_name;
inline char endpoint[256]{}, fingerprint[65]{}, password[257]{};
inline bool remember_password{};
inline std::map<std::string, Draft> drafts;
inline Json state = Json::object();
inline std::vector<char> buffer;
inline char configuration_filter[160]{};
inline void select(const BcClientHostApi &host, const BcClientServerRow &server) {
    if (host.admin_select(server.id) != BC_OK)
        return;
    selected = server.id;
    selected_name = server.name;
    form_selected = 0;
    sequence = 0;
    SecureZeroMemory(password, sizeof(password));
    drafts.clear();
    configuration_filter[0] = 0;
}
inline std::string field_label(const std::string &id, const std::string &key, const Json &field) {
    if (field.contains("displayName"))
        return i18n::display(field, key);
    return i18n::tr((id == "server" ? "server" : "mods." + id) + ".settings." + key, locale::humanize(key));
}
inline void close(const BcClientHostApi &host) {
    selected = 0;
    form_selected = 0;
    SecureZeroMemory(password, sizeof(password));
    drafts.clear();
}
inline void poll(const BcClientHostApi &host) {
    if (buffer.empty())
        buffer.resize(1048576);
    buffer[0] = 0;
    if (host.admin_snapshot(buffer.data(), uint32_t(buffer.size()), &sequence) != BC_OK)
        return;
    if (buffer[0]) {
        state = Json::parse(buffer.data());
        if (state.value("state", std::string{}) != "ready")
            invalidate_page_session();
        i18n::set_remote(state.value("translations", Json::object()));
        if (!state.is_object())
            throw std::runtime_error("Invalid administration state.");
    }
    if (state.value("favoriteId", uint64_t{}) == selected && form_selected != selected) {
        auto address = state.value("endpoint", std::string{}),
             pin = state.value("fingerprint", std::string{});
        strncpy_s(endpoint, address.c_str(), _TRUNCATE);
        strncpy_s(fingerprint, pin.c_str(), _TRUNCATE);
        form_selected = selected;
        remember_password = state.value("passwordSaved", false);
    }
}
inline void config_card(const BcClientHostApi &host, const std::string &id, const Json &config, bool pending,
                        float dpi, const char *save_operation = "save", bool local = false) {
    auto &draft = drafts[(local ? "local:" : "") + id];
    const auto &saved = config.at("saved"), &active = config.at("active");
    auto revision = config.at("revision").get<std::string>();
    if (draft.revision.empty() || draft.values == saved) {
        draft.revision = revision;
        draft.values = saved;
    }
    const bool stale = draft.revision != revision;
    const auto &properties = config.at("schema").at("properties");
    const bool categorized =
        id == "server" || std::any_of(properties.begin(), properties.end(), [](const Json &p) {
            return p.value("category", "general") != "general";
        });
    if (id == "server" || local) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint(
            "##ConfigurationSearch",
            i18n::tr("ui.search_settings_hint", "Search by label, key or value…").c_str(),
            configuration_filter, sizeof(configuration_filter));
        ImGui::Spacing();
    }
    std::map<std::string, std::vector<std::string>> groups;
    std::map<std::string, std::string> group_labels;
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        auto caption = field_label(id, it.key(), it.value());
        if (it.key() == "MapRotation")
            caption += " " + map_rotation_names(draft.values.at(it.key()));
        if (it.value().contains("enum") && draft.values.at(it.key()).is_string())
            caption += " " + i18n::tr("server.values." + draft.values.at(it.key()).get<std::string>(),
                                      draft.values.at(it.key()).get<std::string>());
        if ((id == "server" || local) &&
            !i18n::matches(configuration_filter, caption, it.key(), draft.values.at(it.key()),
                           it.value().value("secret", false)))
            continue;
        auto category = categorized ? it.value().value("category", "general") : std::string{};
        groups[category].push_back(it.key());
        auto fallback = it.value().value("categoryLabel", locale::humanize(category));
        auto key =
            it.value().value("categoryKey", (local ? "mods." + id : "server") + ".categories." + category);
        group_labels[category] = key.empty() ? fallback : i18n::tr(key, fallback);
    }
    if (groups.empty())
        i18n::disabled("ui.no_matching_settings", "No settings match your search.");
    if (config.value("restartRequired", false))
        notice("PendingSettings", "warning",
               i18n::tr("ui.settings_pending_restart", "Some settings are waiting for the next restart."),
               dpi);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {10 * dpi, 7 * dpi});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6 * dpi, 7.5f * dpi});
    std::vector<std::string> category_order;
    for (auto &[category, keys] : groups)
        category_order.push_back(category);
    if (id == "server" || local) {
        const auto order =
            local ? config["schema"].value("categoryOrder", std::vector<std::string>{})
                  : std::vector<std::string>{"identity", "network", "gameplay", "bots", "maps", "heat"};
        std::stable_sort(category_order.begin(), category_order.end(), [&](auto &a, auto &b) {
            return std::find(order.begin(), order.end(), a) < std::find(order.begin(), order.end(), b);
        });
    }
    for (auto &category : category_order) {
        auto &keys = groups.at(category);
        if (local)
            std::stable_sort(keys.begin(), keys.end(), [&](const auto &a, const auto &b) {
                return properties.at(a).value("order", 10000) < properties.at(b).value("order", 10000);
            });
        ImGui::PushID(category.c_str());
        if (categorized) {
            ImGui::Spacing();
            BoldFont bold;
            i18n::colored({.78f, .65f, .94f, 1}, "ui.s", "%s", group_labels[category].c_str());
        }
        if (ImGui::BeginTable("Settings", 3,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupColumn(i18n::tr("ui.setting", "Setting").c_str(),
                                    ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn(i18n::tr("ui.saved", "Saved").c_str(), ImGuiTableColumnFlags_WidthStretch,
                                    1.f);
            ImGui::TableSetupColumn(i18n::tr("ui.active", "Active").c_str(),
                                    ImGuiTableColumnFlags_WidthStretch, .6f);
            table_headers();
            for (const auto &key : keys) {
                const auto &field = properties.at(key);
                const auto type = field.at("type").get<std::string>();
                ImGui::PushID(key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                i18n::wrapped("ui.s", "%s", field_label(id, key, field).c_str());
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    i18n::tooltip("ui.s", "%s", key.c_str());
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                ImGui::BeginDisabled(pending);
                if (field.contains("enum")) {
                    auto value = draft.values.at(key).get<std::string>();
                    if (ImGui::BeginCombo(
                            "##Value", i18n::tr((local ? "mods." + id : "server") + ".values." + value, value)
                                           .c_str())) {
                        for (auto &item : field["enum"]) {
                            auto text = item.get<std::string>();
                            if (ImGui::Selectable(
                                    (i18n::tr((local ? "mods." + id : "server") + ".values." + text, text) +
                                     "###" + text)
                                        .c_str(),
                                    text == value))
                                draft.values[key] = text;
                        }
                        ImGui::EndCombo();
                    }
                } else if (type == "string") {
                    char text[4097]{};
                    strncpy_s(text, draft.values.at(key).get_ref<const std::string &>().c_str(), _TRUNCATE);
                    if (ImGui::InputText("##Value", text, sizeof(text),
                                         field.value("secret", false) ? ImGuiInputTextFlags_Password : 0))
                        draft.values[key] = text;
                    SecureZeroMemory(text, sizeof(text));
                } else if (type == "array") {
                    auto values = draft.values.at(key);
                    for (const auto &map_info : map_names) {
                        auto map = map_info.id;
                        bool enabled = std::find(values.begin(), values.end(), Json(map)) != values.end();
                        if (table_checkbox((map_name(map) + "###" + map).c_str(), &enabled, dpi)) {
                            if (enabled)
                                values.push_back(map);
                            else
                                values.erase(std::remove(values.begin(), values.end(), Json(map)),
                                             values.end());
                            draft.values[key] = values;
                        }
                    }
                } else if (type == "boolean") {
                    bool value = draft.values.at(key).get<bool>();
                    if (table_checkbox("##Value", &value, dpi))
                        draft.values[key] = value;
                } else if (type == "integer" && local && field.value("control", "") == "keybind") {
                    auto value = draft.values.at(key).get<uint32_t>();
                    if (keybind::draw("##Value", value, field.value("allowMouse", false),
                                      field.value("minimum", 1) == 0))
                        draft.values[key] = value;
                } else if (type == "integer") {
                    int value = draft.values.at(key).get<int>();
                    if (numbers::input_integer("##Value", value))
                        draft.values[key] = value;
                } else if (type == "number") {
                    double value = draft.values.at(key).get<double>();
                    if (numbers::input_decimal("##Value", value))
                        draft.values[key] = value;
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    if (field.contains("minimum") && field.contains("maximum"))
                        i18n::tooltip("ui.allowed_value_range", "Allowed value: %s to %s",
                                      field["minimum"].dump().c_str(), field["maximum"].dump().c_str());
                }
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                const auto &value = active.at(key);
                auto text = value.is_boolean()
                                ? (value.get<bool>() ? i18n::tr("ui.yes", "Yes") : i18n::tr("ui.no", "No"))
                                : numbers::display(value, value.is_number_float() ? "%.3f" : "%.6g");
                if (local && type == "integer" && field.value("control", "") == "keybind")
                    text = keybind::name(value.get<uint32_t>());
                if (key == "MapRotation")
                    text = map_rotation_names(value);
                if (field.contains("enum") && value.is_string())
                    text = i18n::tr((local ? "mods." + id : "server") + ".values." + value.get<std::string>(),
                                    value.get<std::string>());
                i18n::wrapped("ui.s", "%s",
                              field.value("secret", false) ? (value == "" ? "—" : "••••••••") : text.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar(2);
    if (stale)
        notice("StaleSettings", "warning",
               i18n::tr("ui.settings_conflict",
                        "Another change was saved. Reload the values before editing this draft."),
               dpi);
    bool valid = true;
    for (auto it = config["schema"]["properties"].begin(); it != config["schema"]["properties"].end(); ++it) {
        const auto &v = draft.values[it.key()], &f = it.value();
        if (f.contains("minimum") && v < f["minimum"])
            valid = false;
        if (f.contains("maximum") && v > f["maximum"])
            valid = false;
    }
    if (!valid)
        notice("InvalidSettings", "danger",
               i18n::tr("ui.settings_out_of_range", "A value exceeds the allowed limits."), dpi);
    ImGui::Spacing();
    ImGui::BeginDisabled(pending || stale || !valid || draft.values == saved);
    if (action_button(ActionTone::success,
                      (local && config.value("live", false)
                           ? i18n::label("ui.save_apply", "Save and apply")
                           : i18n::label("ui.save_for_restart", "Save for the next restart"))
                          .c_str())) {
        Json payload{{"expectedRevision", draft.revision}, {"values", draft.values}};
        if (config.contains("modId"))
            payload["modId"] = id;
        if (local && host.save_mod_settings)
            host.save_mod_settings(id.c_str(), std::stoull(draft.revision), draft.values.dump().c_str());
        else
            host.admin_command(save_operation, payload.dump().c_str());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(pending);
    if (action_button(ActionTone::info, i18n::label("ui.reload", "Reload").c_str())) {
        draft = {revision, saved};
        if (!local)
            host.admin_command(config.contains("modId") ? "refresh" : "server.config.read", "{}");
    }
    ImGui::EndDisabled();
}
#include "AdministrationPages.inc"
inline void draw(const BcClientHostApi &host, float dpi) {
    poll(host);
    i18n::use_remote(true);
    const bool matching = state.value("favoriteId", uint64_t{}) == selected;
    const bool pending = state.value("pending", false) || !matching;
    const bool ready = matching && state.value("state", std::string{}) == "ready";
    ImGui::BeginGroup();
    ImGui::TextUnformatted(i18n::tr("ui.administration", "Administration").c_str());
    i18n::disabled("ui.s", "%s", selected_name.c_str());
    ImGui::EndGroup();
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 132 * dpi);
    if (icon_button("##AdminBack", Icon::back, i18n::tr("ui.back_to_servers", "Back to servers").c_str(),
                    dpi))
        close(host);
    ImGui::SameLine(0, 12 * dpi);
    ImGui::BeginDisabled(!ready || pending);
    if (icon_button("##AdminRefresh", Icon::refresh,
                    i18n::tr("ui.refresh_status_and_settings", "Refresh status and settings").c_str(), dpi,
                    ActionTone::info))
        host.admin_command("refresh", "{}");
    ImGui::EndDisabled();
    ImGui::SameLine(0, 12 * dpi);
    if (icon_button("##AdminDisconnect", Icon::disconnect,
                    i18n::tr("ui.disconnect_admin", "Disconnect administration").c_str(), dpi,
                    ActionTone::danger)) {
        invalidate_page_session();
        host.admin_disconnect();
    }
    ImGui::Spacing();
    if (!selected)
        return;
    if (matching && !state.value("message", std::string{}).empty()) {
        auto message = state.value("messageKey", std::string{});
        auto text = message.empty() ? state["message"].get<std::string>()
                                    : i18n::tr(message, state["message"].get<std::string>());
        notice("AdminMessage", state.value("messageKind", "info"), text, dpi);
    }
    if (!ready) {
        ImGui::BeginChild("AdminConnection", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::TextUnformatted(i18n::tr("ui.secure_connection", "Secure connection").c_str());
        i18n::wrapped(
            "ui.pairing_hint",
            "Copy the address and certificate fingerprint from the administrator's pairing.json file.");
        ImGui::BeginDisabled(pending || form_selected != selected);
        ImGui::TextUnformatted(i18n::tr("ui.admin_address", "Administration address").c_str());
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##AdminEndpoint", endpoint, sizeof(endpoint));
        i18n::disabled("ui.admin_port_hint", "The administration port is separate from the game port.");
        ImGui::TextUnformatted(
            i18n::tr("ui.certificate_fingerprint", "Certificate fingerprint (SHA-256)").c_str());
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##AdminFingerprint", fingerprint, sizeof(fingerprint),
                         ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::TextUnformatted(i18n::tr("ui.admin_password", "Administrator password").c_str());
        ImGui::SetNextItemWidth(-1);
        bool enter = ImGui::InputText("##AdminPassword", password, sizeof(password),
                                      ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
        const bool saved_password = state.value("passwordSaved", false) &&
                                    state.value("endpoint", std::string{}) == endpoint &&
                                    state.value("fingerprint", std::string{}) == fingerprint;
        ImGui::Checkbox(i18n::label("ui.remember_password", "Remember the password on this client").c_str(),
                        &remember_password);
        if (saved_password) {
            i18n::disabled("ui.saved_password_hint", "Password saved. Leave this field empty to use it.");
            ImGui::SameLine();
            if (ImGui::Button(i18n::label("ui.forget", "Forget").c_str())) {
                host.admin_command("forget", "{}");
                remember_password = false;
            }
        }
        const bool valid = endpoint[0] && strlen(fingerprint) == 64 &&
                           (strlen(password) >= 12 || (saved_password && !password[0]));
        ImGui::BeginDisabled(!valid);
        bool submit = ImGui::Button(i18n::label("ui.connect", "Connect").c_str());
        if ((submit || enter) && valid && !pending) {
            invalidate_page_session();
            auto result =
                host.admin_connect(selected, endpoint, fingerprint, password, remember_password ? 1u : 0u);
            SecureZeroMemory(password, sizeof(password));
            if (result != BC_OK)
                state["message"] = "Connection not sent. Check the fields.";
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::EndChild();
        return;
    }
    if (!ImGui::BeginTabBar("AdministrationTabs"))
        return;
    if (icon_tab(i18n::label("ui.mods", "Mods").c_str(), Icon::mods, dpi)) {
        ImGui::BeginChild("ModsPage", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
        draw_selection(host, pending, dpi);
        for (const auto &mod : state.at("mods")) {
            auto id = mod.at("id").get<std::string>();
            ImGui::PushID(id.c_str());
            ImGui::BeginChild("RemoteMod", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
            bold_text(i18n::tr("mods." + id + ".name", mod["name"].get<std::string>()).c_str());
            ImGui::SameLine();
            i18n::disabled("ui.s", "%s", mod.value("version", std::string{}).c_str());
            auto status = mod.value("state", 0);
            i18n::disabled("ui.s_s", "%s  /  %s",
                           status == 1   ? i18n::tr("ui.state.loaded", "Loaded").c_str()
                           : status == 2 ? i18n::tr("ui.state.disabled", "Disabled").c_str()
                           : status == 3 ? i18n::tr("ui.state.error", "Error").c_str()
                                         : i18n::tr("ui.state.waiting", "Waiting").c_str(),
                           mod.value("author", std::string{}).c_str());
            if (auto error = mod.value("error", std::string{}); !error.empty())
                i18n::wrapped("ui.s", "%s", error.c_str());
            if (mod.contains("dependencies") && !mod["dependencies"].empty()) {
                for (const auto &dep : mod["dependencies"])
                    i18n::disabled("ui.dependency", "Dependency: %s >= %s",
                                   dep.at("id").get_ref<const std::string &>().c_str(),
                                   dep.at("minimum").get_ref<const std::string &>().c_str());
            }
            if (state.contains("configs") && state["configs"].contains(id))
                config_card(host, id, state["configs"][id], pending, dpi);
            else
                i18n::disabled("ui.no_admin_settings", "This mod has no available administration settings.");
            ImGui::EndChild();
            ImGui::PopID();
            ImGui::Spacing();
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (icon_tab(i18n::label("ui.server_tab", "Server").c_str(), Icon::servers, dpi)) {
        ImGui::BeginChild("ServerPage", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
        const auto &server = state.at("server");
        ImGui::BeginChild("AdminStatus", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        i18n::colored({.63f, .85f, .68f, 1}, "ui.authenticated_identity", "Authenticated  /  %s",
                      state.value("tls", std::string{}).c_str());
        i18n::disabled("ui.s", "%s", state.value("endpoint", std::string{}).c_str());
        ImGui::Separator();
        i18n::text("ui.briefcasenative_s", "BriefcaseNative %s",
                   server.value("framework", std::string{}).c_str());
        i18n::text("ui.game_build", "Game build: %s", server.value("gameBuild", std::string{}).c_str());
        i18n::text("ui.unreal_s_s", "Unreal %s : %s", server.value("unreal", std::string{}).c_str(),
                   server.value("backendState", 0) == 2
                       ? i18n::tr("ui.state.ready", "Ready").c_str()
                       : i18n::tr("ui.state.unavailable", "Unavailable").c_str());
        auto seconds = server.value("uptimeSeconds", int64_t{});
        i18n::text("ui.server_uptime", "Running for %lld min %lld s", seconds / 60, seconds % 60);
        i18n::text("ui.server_mod_counts", "Mods: %u loaded / %u installed", server.value("loadedMods", 0u),
                   server.value("discoveredMods", 0u));
        i18n::disabled("ui.refresh_and_restart_hint",
                       "Status at the last refresh. Settings apply at the next restart.");
        ImGui::EndChild();
        ImGui::Spacing();

        draw_server_tools(host, pending, dpi);
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (icon_tab(i18n::label("ui.configuration", "Configuration").c_str(), Icon::settings, dpi)) {
        ImGui::BeginChild("ConfigurationPage", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::TextUnformatted(i18n::tr("ui.server_configuration", "Server configuration").c_str());
        i18n::disabled("ui.changes_apply_on_restart", "Changes take effect at the next restart.");
        ImGui::Spacing();
        if (state.contains("serverConfig"))
            config_card(host, "server", state["serverConfig"], pending, dpi, "server.config.write");
        else
            i18n::disabled("ui.server_configuration_unavailable",
                           "This server version does not provide this configuration.");
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (icon_tab(i18n::label("ui.balancing", "Balancing").c_str(), Icon::balance, dpi)) {
        ImGui::BeginChild("BalancePage", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
        draw_balance(host, pending, dpi);
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
} // namespace bc::menu::administration
