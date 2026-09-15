inline std::string balance_group, logs_source = "framework";
inline char balance_filter[128]{};
inline std::map<std::string, Draft> balance_drafts;
inline uint64_t pages_selected{};
inline bool balance_load_requested{};
inline void invalidate_page_session() {
    balance_load_requested = false;
    balance_drafts.clear();
}
inline void reset_pages() {
    if (pages_selected == selected)
        return;
    pages_selected = selected;
    balance_group.clear();
    balance_filter[0] = 0;
    invalidate_page_session();
}
inline void draw_selection(const BcClientHostApi &host, bool pending, float dpi) {
    reset_pages();
    if (!state.contains("selection"))
        return;
    auto &selection = state["selection"];
    auto &draft = drafts["mod-selection"];
    auto saved = selection["saved"];
    auto revision = selection["revision"].get<std::string>();
    if (draft.revision.empty() || draft.values == saved)
        draft = {revision, saved};
    ImGui::TextUnformatted(
        i18n::tr("ui.enabled_mods_on_restart", "Mods enabled at the next restart").c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6 * dpi, 2 * dpi});
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {10 * dpi, 7 * dpi});
    ImGui::BeginDisabled(pending);
    if (ImGui::BeginTable("ModSelection", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn(i18n::tr("ui.mod", "Mod").c_str(), ImGuiTableColumnFlags_WidthStretch, 2);
        ImGui::TableSetupColumn(i18n::tr("ui.enable", "Enable").c_str(), ImGuiTableColumnFlags_WidthStretch,
                                1);
        table_headers();
        for (auto &mod : state["mods"]) {
            auto id = mod["id"].get<std::string>();
            bool enabled =
                std::find(draft.values.begin(), draft.values.end(), Json(id)) != draft.values.end();
            ImGui::PushID(id.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            bold_text(i18n::tr("mods." + id + ".name", mod["name"].get<std::string>()).c_str());
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(mod.value("environment", std::string{}) == "client");
            if (ImGui::Checkbox("##Enabled", &enabled)) {
                if (enabled)
                    draft.values.push_back(id);
                else
                    draft.values.erase(std::remove(draft.values.begin(), draft.values.end(), Json(id)),
                                       draft.values.end());
                std::sort(draft.values.begin(), draft.values.end());
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar(2);
    if (selection.value("restartRequired", false))
        i18n::colored({.95f, .72f, .34f, 1}, "ui.selection_pending_restart",
                      "The selection is waiting for the next restart.");
    if (draft.revision != revision)
        i18n::wrapped("ui.selection_conflict",
                      "Another administrator changed the selection. Reload before saving.");
    ImGui::BeginDisabled(pending || draft.values == saved || draft.revision != revision);
    if (action_button(ActionTone::success, i18n::label("ui.save_selection", "Save selection").c_str()))
        host.admin_command(
            "mods.selection.write",
            Json{{"expectedRevision", draft.revision}, {"enabledMods", draft.values}}.dump().c_str());
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(pending);
    if (action_button(ActionTone::info, i18n::label("ui.reload_selection", "Reload selection").c_str())) {
        draft = {revision, saved};
        host.admin_command("mods.selection.read", "{}");
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::Spacing();
}
inline void draw_server_tools(const BcClientHostApi &host, bool pending, float dpi) {
    if (state.at("server").value("administrationVersion", 0) < 2) {
        i18n::disabled("ui.server_tools_update_required",
                       "Update the server to access logs and restart controls.");
        return;
    }
    ImGui::BeginDisabled(pending);
    if (ImGui::Button(i18n::label("ui.restart_server", "Restart server").c_str()))
        ImGui::OpenPopup(i18n::label("ui.confirm_restart", "Confirm restart").c_str());
    ImGui::EndDisabled();
    ImGui::SetNextWindowSize({520 * dpi, 0}, ImGuiCond_Always);
    if (ImGui::BeginPopupModal(i18n::label("ui.confirm_restart", "Confirm restart").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize)) {
        i18n::wrapped("ui.restart_warning", "Players will be disconnected. The server will restart with the "
                                            "saved mods, configuration and balancing.");
        ImGui::Spacing();
        ImGui::BeginDisabled(pending);
        if (ImGui::Button(i18n::label("ui.confirm", "Confirm").c_str())) {
            host.admin_command("server.restart", "{}");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(i18n::label("ui.cancel", "Cancel").c_str()))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted(i18n::tr("ui.logs", "Logs").c_str());
    ImGui::BeginDisabled(pending);
    if (ImGui::RadioButton(i18n::label("ui.framework_log", "Briefcase").c_str(), logs_source == "framework"))
        logs_source = "framework";
    ImGui::SameLine();
    if (ImGui::RadioButton(i18n::label("ui.game_log", "Game").c_str(), logs_source == "game"))
        logs_source = "game";
    ImGui::SameLine();
    if (action_button(ActionTone::info, i18n::label("ui.refresh_logs", "Refresh logs").c_str()))
        host.admin_command("server.logs", Json{{"source", logs_source}}.dump().c_str());
    ImGui::EndDisabled();
    i18n::disabled("ui.logs_refresh_hint", "Latest 32 KiB. Refresh on demand.");
    ImGui::BeginChild("LogOutput", {0, 260 * dpi}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (state.contains("logs") && state["logs"].value("source", std::string{}) == logs_source)
        ImGui::TextUnformatted(state["logs"]["text"].get_ref<const std::string &>().c_str());
    else
        i18n::disabled("ui.logs_not_loaded", "Refresh to load this log.");
    ImGui::EndChild();
}
inline void draw_balance(const BcClientHostApi &host, bool pending, float dpi) {
    reset_pages();
    ImGui::TextUnformatted(i18n::tr("ui.balancing_by_character", "Balancing by character").c_str());
    i18n::disabled("ui.balancing_description",
                   "Weapons, projectiles, expertises and passives. Applied at the next restart.");
    if (!state.contains("balance")) {
        i18n::disabled("ui.balancing_unavailable", "Balancing is unavailable on this server version.");
        return;
    }
    const auto &balance = state["balance"];
    if (!balance.value("available", false)) {
        i18n::wrapped("ui.no_balancing_profile",
                      "No balancing profile or catalogue is available on this server.");
        return;
    }
    if (!balance_group.empty() && !balance["groups"].contains(balance_group)) {
        balance_group.clear();
        invalidate_page_session();
    }
    auto group_name = [&](const std::string &group) {
        const auto label_id = locale::balance_group_label_id(group);
        const auto key = "server.balance.groups." + label_id;
        const auto fallback = locale::humanize(label_id);
        if (balance.contains("groupLabels") && balance["groupLabels"].contains(group)) {
            auto metadata = balance["groupLabels"][group];
            // Older servers derive translation keys directly from the original group ID.
            if (metadata.value("displayNameKey", "") == "server.balance.groups." + group) {
                metadata["displayNameKey"] = key;
                if (metadata.value("displayName", "") == locale::humanize(group))
                    metadata["displayName"] = fallback;
            }
            return i18n::display(metadata, fallback);
        }
        return i18n::tr(key, fallback);
    };
    ImGui::SetNextItemWidth(240 * dpi);
    ImGui::BeginDisabled(pending);
    if (ImGui::BeginCombo(i18n::label("ui.character_group", "Character / group").c_str(),
                          balance_group.empty() ? i18n::tr("ui.choose", "Choose…").c_str()
                                                : group_name(balance_group).c_str())) {
        for (auto it = balance["groups"].begin(); it != balance["groups"].end(); ++it) {
            auto text = group_name(it.key()) + " (" + it.value().dump() + ")";
            if (ImGui::Selectable(text.c_str(), it.key() == balance_group)) {
                balance_group = it.key();
                balance_load_requested =
                    host.admin_command("balance.read", Json{{"group", balance_group}}.dump().c_str()) ==
                    BC_OK;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(320 * dpi);
    ImGui::InputTextWithHint("##BalanceFilter",
                             i18n::tr("ui.filter_balancing_hint", "Filter a weapon or setting…").c_str(),
                             balance_filter, sizeof(balance_filter));
    if (balance_group.empty())
        return;
    if (balance.value("group", std::string{}) != balance_group) {
        bool started = false;
        if (!pending && !balance_load_requested) {
            started =
                host.admin_command("balance.read", Json{{"group", balance_group}}.dump().c_str()) == BC_OK;
            balance_load_requested = true;
        }
        // One request restores the selected group after reconnect/refresh. Failures
        // stay visible and require the reload button; never retry each frame.
        i18n::disabled(pending || started ? "ui.balance.loading" : "ui.balance.reload_required",
                       pending || started ? "Loading group…" : "Reload this group to display its settings.");
        ImGui::BeginDisabled(pending || started);
        if (action_button(ActionTone::info, i18n::label("ui.reload_group", "Reload group").c_str()))
            balance_load_requested =
                host.admin_command("balance.read", Json{{"group", balance_group}}.dump().c_str()) == BC_OK;
        ImGui::EndDisabled();
        return;
    }
    balance_load_requested = false;
    auto &draft = balance_drafts[balance_group];
    Json saved = Json::object();
    for (auto &x : balance["entries"])
        saved[x["id"].get<std::string>()] = x["saved"];
    auto revision = balance["revision"].get<std::string>();
    if (draft.revision.empty() || draft.values == saved)
        draft = {revision, saved};
    bool stale = draft.revision != revision, valid = true;
    Json changes = Json::array();
    for (auto &x : balance["entries"]) {
        auto id = x["id"].get<std::string>();
        if (!draft.values.contains(id))
            draft.values[id] = x["saved"];
        auto v = draft.values[id];
        if (v != x["saved"])
            changes.push_back({{"id", id}, {"value", v}});
        if (v.is_number() && x.contains("allowedRange")) {
            double low{}, high{};
            char to[8]{};
            if (sscanf_s(x["allowedRange"].get_ref<const std::string &>().c_str(), "%lf %7s %lf", &low, to,
                         unsigned(sizeof(to)), &high) == 3)
                valid = valid && v >= low && v <= high;
        }
    }
    if (balance.value("restartRequired", false))
        i18n::colored({.95f, .72f, .34f, 1}, "ui.changes_pending_restart",
                      "Some changes are waiting for a restart.");
    if (stale)
        i18n::wrapped("ui.balancing_conflict",
                      "The server profile changed. Reload this group before saving.");
    if (!valid)
        i18n::colored({1, .5f, .5f, 1}, "ui.balancing_out_of_range", "A value exceeds its allowed range.");
    ImGui::BeginDisabled(pending || stale || !valid || changes.empty());
    if (action_button(ActionTone::success, i18n::label("ui.save_group", "Save this group").c_str()))
        host.admin_command(
            "balance.write",
            Json{{"group", balance_group}, {"expectedRevision", draft.revision}, {"changes", changes}}
                .dump()
                .c_str());
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(pending);
    if (action_button(ActionTone::info, i18n::label("ui.reload_group", "Reload group").c_str())) {
        draft = {revision, saved};
        host.admin_command("balance.read", Json{{"group", balance_group}}.dump().c_str());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    i18n::disabled("ui.changes_count", "%zu change(s)", changes.size());
    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6 * dpi, 7.5f * dpi});
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {10 * dpi, 7 * dpi});
    if (ImGui::BeginTable("BalanceSettings", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollY,
                          {0, ImGui::GetContentRegionAvail().y})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(i18n::tr("ui.setting", "Setting").c_str(), ImGuiTableColumnFlags_WidthStretch,
                                2.3f);
        ImGui::TableSetupColumn(i18n::tr("ui.saved", "Saved").c_str(), ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupColumn(i18n::tr("ui.at_startup", "At startup").c_str(),
                                ImGuiTableColumnFlags_WidthStretch, .6f);
        ImGui::TableSetupColumn(i18n::tr("ui.default", "Default").c_str(), ImGuiTableColumnFlags_WidthStretch,
                                .6f);
        table_headers();
        std::string previous;
        // Group within a character by row, then table and field; hitscan/projectiles stay with the owner.
        std::vector<const Json *> rows;
        for (auto &x : balance["entries"])
            rows.push_back(&x);
        std::sort(rows.begin(), rows.end(), [](auto a, auto b) {
            return std::tie(a->at("row"), a->at("table"), a->at("field")) <
                   std::tie(b->at("row"), b->at("table"), b->at("field"));
        });
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            return s;
        };
        auto filter = i18n::fold(balance_filter);
        for (auto ptr : rows) {
            auto &x = *ptr;
            auto id = x["id"].get<std::string>();
            auto row = x["row"].get<std::string>(), field = x["field"].get<std::string>();
            auto row_label = i18n::display(x.value("rowPresentation", Json::object()), row);
            auto field_caption = i18n::display(x.value("presentation", Json::object()), field);
            if (!filter.empty() &&
                i18n::fold(row_label + " " + field_caption + " " + row + " " + field + " " +
                           x["table"].get<std::string>() + " " + draft.values[id].dump())
                        .find(filter) == std::string::npos)
                continue;
            if (previous != row) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                BoldFont bold;
                i18n::colored({.78f, .65f, .94f, 1}, "ui.s", "%s", row_label.c_str());
                previous = row;
            }
            ImGui::PushID(id.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            i18n::wrapped("ui.s", "%s", field_caption.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                i18n::tooltip("ui.value_range_detail", "%s\n%s\nRange: %s",
                              x["table"].get_ref<const std::string &>().c_str(),
                              x.value("info", std::string{}).c_str(),
                              x.value("allowedRange", i18n::tr("ui.boolean", "boolean")).c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            ImGui::BeginDisabled(pending || !x.value("editable", false));
            if (draft.values[id].is_boolean()) {
                bool v = draft.values[id].get<bool>();
                if (table_checkbox("##Value", &v, dpi))
                    draft.values[id] = v;
            } else {
                double v = draft.values[id].get<double>();
                if (ImGui::InputDouble("##Value", &v, 0, 0, "%.6g"))
                    draft.values[id] = v;
            }
            ImGui::EndDisabled();
            for (auto key : {"active", "default"}) {
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted((x[key].is_boolean()
                                            ? i18n::tr(x[key].get<bool>() ? "ui.yes" : "ui.no",
                                                       x[key].get<bool>() ? "Yes" : "No")
                                            : numbers::display(x[key]))
                                           .c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);
}
