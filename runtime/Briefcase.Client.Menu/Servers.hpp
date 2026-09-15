#pragma once
#include "Administration.hpp"
#include "Icons.hpp"
#include "Menu.hpp"
#include <array>
namespace bc::menu {
inline void servers_page(const BcClientHostApi &host, float dpi, bool &open) {
    if (administration::selected) {
        administration::draw(host, dpi);
        return;
    }
    BcClientServerList list{sizeof(list)};
    if (host.servers(&list) != BC_OK) {
        ImGui::TextUnformatted(
            i18n::tr("ui.server_list_unavailable", "Unable to read the server list.").c_str());
        return;
    }
    static bool awaiting_join = false;
    if (awaiting_join && list.join_state == 2) {
        open = false;
        awaiting_join = false;
    }
    if (list.join_state == 3)
        awaiting_join = false;
    BcClientHome home{sizeof(home)};
    host.home(&home);
    static char name[96]{}, endpoint[256]{};
    static bool add_failed = false;
    // Keep the same title/description rhythm as Home and Mods, independent of the action button.
    ImGui::BeginGroup();
    i18n::text("ui.servers_count", "Servers  (%u)", list.count);
    i18n::disabled("ui.saved_servers", "Your saved servers");
    ImGui::EndGroup();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 36 * dpi);
    ImGui::BeginDisabled(!list.writable || list.pending || list.count >= 64);
    if (icon_button("##AddServer", Icon::add, i18n::tr("ui.add_server", "Add a server").c_str(), dpi,
                    ActionTone::success)) {
        name[0] = endpoint[0] = 0;
        add_failed = false;
        ImGui::OpenPopup(i18n::label("ui.add_server", "Add a server").c_str());
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (list.join_message[0]) {
        auto kind = list.join_state == 3 ? "danger" : list.join_state == 2 ? "success" : "info";
        auto key = list.join_state == 1   ? "ui.servers.connecting"
                   : list.join_state == 3 ? "ui.message.connection_failed"
                                          : "ui.message.connected";
        notice("JoinResult", kind, i18n::tr(key, list.join_message), dpi);
    }
    if (list.message[0]) {
        std::string message = list.message, key = "ui.servers.invalid", kind = "danger";
        if (message == "Server saved.") {
            key = "ui.servers.saved";
            kind = "success";
        } else if (message == "Server removed.") {
            key = "ui.servers.removed";
            kind = "success";
        } else if (message == "Saving...") {
            key = "ui.servers.saving";
            kind = "info";
        } else if (message == "Removing...") {
            key = "ui.servers.removing";
            kind = "info";
        }
        notice("DirectoryResult", kind, i18n::tr(key, message), dpi);
    }
    if (home.unreal_state != 2)
        i18n::disabled("ui.joining_requires_engine", "Joining will be available when the engine is ready.");
    ImGui::Spacing();
    for (uint32_t i = 0; i < list.count; ++i) {
        const auto &entry = list.entries[i];
        ImGui::PushID(int(entry.id));
        ImGui::PushID(int(entry.id >> 32));
        ImGui::BeginChild("Server", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        if (ImGui::BeginTable("Row", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupColumn(i18n::tr("ui.server", "Server").c_str(),
                                    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(i18n::tr("ui.actions", "Actions").c_str(),
                                    ImGuiTableColumnFlags_WidthFixed, 132 * dpi);
            ImGui::TableNextColumn();
            i18n::wrapped("ui.s", "%s", entry.name);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            i18n::wrapped("ui.s", "%s", entry.endpoint);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            if (icon_button("##Admin", Icon::admin,
                            i18n::tr("ui.administer_server", "Administer this server").c_str(), dpi,
                            ActionTone::info))
                administration::select(host, entry);
            ImGui::SameLine(0, 12 * dpi);
            ImGui::BeginDisabled(home.unreal_state != 2 || list.join_state == 1 || list.pending);
            if (icon_button("##Join", Icon::join, i18n::tr("ui.join_server", "Join this server").c_str(), dpi,
                            ActionTone::success)) {
                awaiting_join = host.join_server(entry.id) == BC_OK;
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 12 * dpi);
            ImGui::BeginDisabled(!list.writable || list.pending || list.join_state == 1);
            if (icon_button("##Remove", Icon::remove,
                            i18n::tr("ui.remove_server", "Remove from the list").c_str(), dpi,
                            ActionTone::danger))
                host.remove_server(entry.id);
            ImGui::EndDisabled();
            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::Spacing();
        ImGui::PopID();
        ImGui::PopID();
    }
    if (!list.count && list.writable) {
        ImGui::BeginChild("EmptyServers", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::TextUnformatted(i18n::tr("ui.no_saved_servers", "No saved servers").c_str());
        i18n::wrapped("ui.add_server_hint", "Use the + button to add a name and address.");
        ImGui::EndChild();
    }
    // Pin width on every frame: full-width fields cannot drive horizontal auto-fit.
    // Leave height automatic so validation messages can expand the form.
    ImGui::SetNextWindowSize({440 * dpi, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {.5f, .5f});
    if (ImGui::BeginPopupModal(i18n::label("ui.add_server", "Add a server").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool first = ImGui::IsWindowAppearing();
        ImGui::TextUnformatted(i18n::tr("ui.name", "Name").c_str());
        ImGui::SetNextItemWidth(-1);
        if (first)
            ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##ServerName", i18n::tr("ui.server_name_placeholder", "My server").c_str(),
                                 name, sizeof(name));
        ImGui::TextUnformatted(i18n::tr("ui.address", "Address").c_str());
        ImGui::SetNextItemWidth(-1);
        const bool enter = ImGui::InputTextWithHint(
            "##ServerAddress", i18n::tr("ui.127_0_0_1_7777", "127.0.0.1:7777").c_str(), endpoint,
            sizeof(endpoint), ImGuiInputTextFlags_EnterReturnsTrue);
        i18n::disabled("ui.endpoint_hint", "Host:port or [IPv6]:port");
        if (add_failed) {
            notice("AddError", "danger", i18n::tr("ui.servers.invalid", "Check the server name and address."),
                   dpi);
        }
        ImGui::Spacing();
        if (ImGui::Button(i18n::label("ui.cancel", "Cancel").c_str()))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        ImGui::BeginDisabled(!name[0] || !endpoint[0] || list.pending);
        const bool submit = action_button(ActionTone::success, i18n::label("ui.add", "Add").c_str());
        if ((submit || enter) && name[0] && endpoint[0] && !list.pending) {
            add_failed = host.add_server(name, endpoint) != BC_OK;
            if (!add_failed)
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}
} // namespace bc::menu
