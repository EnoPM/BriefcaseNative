#include "Menu.hpp"
#include "I18n.hpp"
#include "Icons.hpp"
#include "Keybind.hpp"
#include "Servers.hpp"
#include <algorithm>
#include <imgui.h>
namespace bc::menu {
void style(float dpi) {
    ImGui::StyleColorsDark();
    auto &s = ImGui::GetStyle();
    s.WindowRounding = 12;
    s.ChildRounding = 8;
    s.FrameRounding = 6;
    s.FrameBorderSize = 1;
    s.PopupRounding = 8;
    s.WindowPadding = {22, 20};
    s.FramePadding = {14, 9};
    s.ButtonTextAlign = {0.5f, 0.5f};
    s.ItemSpacing = {12, 10};
    s.ScrollbarSize = 11;
    auto *c = s.Colors;
    c[ImGuiCol_WindowBg] = {0.065f, 0.052f, 0.08f, 0.98f};
    c[ImGuiCol_ChildBg] = {0.095f, 0.08f, 0.12f, 1};
    c[ImGuiCol_Border] = {0.36f, 0.31f, 0.43f, 1};
    c[ImGuiCol_Text] = {0.94f, 0.92f, 0.97f, 1};
    c[ImGuiCol_TextDisabled] = {0.59f, 0.54f, 0.65f, 1};
    c[ImGuiCol_FrameBg] = {0.055f, 0.044f, 0.075f, 1};
    c[ImGuiCol_FrameBgHovered] = {0.13f, 0.09f, 0.18f, 1};
    c[ImGuiCol_FrameBgActive] = {0.18f, 0.12f, 0.24f, 1};
    c[ImGuiCol_TitleBg] = {0.10f, 0.075f, 0.14f, 1};
    c[ImGuiCol_TitleBgActive] = {0.24f, 0.14f, 0.32f, 1};
    c[ImGuiCol_PopupBg] = {0.08f, 0.06f, 0.11f, 1};
    c[ImGuiCol_ModalWindowDimBg] = {0.02f, 0.01f, 0.04f, 0.65f};
    c[ImGuiCol_TextSelectedBg] = {0.58f, 0.31f, 0.72f, 0.65f};
    c[ImGuiCol_Button] = {0.24f, 0.14f, 0.32f, 1};
    c[ImGuiCol_ButtonHovered] = {0.39f, 0.22f, 0.52f, 1};
    c[ImGuiCol_ButtonActive] = {0.58f, 0.31f, 0.72f, 1};
    c[ImGuiCol_Header] = c[ImGuiCol_Button];
    c[ImGuiCol_HeaderHovered] = c[ImGuiCol_ButtonHovered];
    c[ImGuiCol_HeaderActive] = c[ImGuiCol_ButtonActive];
    c[ImGuiCol_Tab] = {0.16f, 0.11f, 0.21f, 1};
    c[ImGuiCol_TabHovered] = c[ImGuiCol_ButtonHovered];
    c[ImGuiCol_TabSelected] = c[ImGuiCol_Button];
    c[ImGuiCol_TabSelectedOverline] = {0.77f, 0.46f, 0.97f, 1};
    c[ImGuiCol_TabDimmed] = c[ImGuiCol_Tab];
    c[ImGuiCol_TabDimmedSelected] = c[ImGuiCol_TabSelected];
    c[ImGuiCol_TabDimmedSelectedOverline] = c[ImGuiCol_TabSelectedOverline];
    c[ImGuiCol_CheckMark] = {0.77f, 0.46f, 0.97f, 1};
    c[ImGuiCol_TableHeaderBg] = {0.20f, 0.17f, 0.25f, 1};
    c[ImGuiCol_TableRowBg] = {0.10f, 0.085f, 0.13f, 1};
    c[ImGuiCol_TableRowBgAlt] = {0.17f, 0.145f, 0.205f, 1};
    c[ImGuiCol_Separator] = c[ImGuiCol_Border];
    s.ScaleAllSizes(dpi);
}
static void pair(const char *name, const char *value) {
    i18n::disabled("ui.s", "%s", name);
    ImGui::SameLine(205 * std::max(1.f, ImGui::GetFontSize() / 17.f));
    ImGui::TextUnformatted(value);
}
void draw(const BcClientHostApi &host, const BcClientMetrics &metrics, bool &open) {
    keybind::begin_frame();
    i18n::poll(host);
    i18n::use_remote(false);
    auto &io = ImGui::GetIO();
    ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, io.DisplaySize, IM_COL32(7, 4, 12, 115));
    const float dpi = std::max(1.f, io.FontDefault ? io.FontDefault->FontSize / 17.f : 1.f);
    // Fixed margins relative to the game viewport, recomputed after any resolution change.
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.05f, io.DisplaySize.y * 0.10f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({io.DisplaySize.x * 0.90f, io.DisplaySize.y * 0.80f}, ImGuiCond_Always);
    if (!ImGui::Begin("Briefcase##Framework", &open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        keybind::end_frame();
        return;
    }
    const float header_y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(header_y + (36 * dpi - ImGui::GetTextLineHeight()) / 2);
    i18n::colored({0.78f, 0.5f, 0.96f, 1}, "ui.briefcase", "BRIEFCASE");
    ImGui::SameLine();
    i18n::disabled("ui.native_client", "NATIVE  /  CLIENT");
    ImGui::SameLine();
    ImGui::SetCursorPos({ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - 36 * dpi, header_y});
    if (icon_button(
            "##CloseMenu", Icon::close,
            i18n::format("ui.close_menu_key", "Close menu (%s)",
                         keybind::name(host.menu_key ? host.menu_key() : input::default_menu_key).c_str())
                .c_str(),
            dpi, ActionTone::danger))
        open = false;
    i18n::disabled("ui.your_native_mods_in_one_place", "Your native mods, in one place");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    static int page;
    ImGui::BeginChild("Navigation", {160 * dpi, 0}, false);
    if (navigation("##Home", i18n::tr("ui.home", "Home").c_str(), Icon::home, page == 0, dpi))
        page = 0;
    if (navigation("##Mods", i18n::tr("ui.mods", "Mods").c_str(), Icon::mods, page == 1, dpi))
        page = 1;
    if (navigation("##Servers", i18n::tr("ui.servers", "Servers").c_str(), Icon::servers, page == 2, dpi))
        page = 2;
    if (navigation("##Settings", i18n::tr("ui.settings", "Settings").c_str(), Icon::settings, page == 3, dpi))
        page = 3;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Content", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
    if (page == 0) {
        BcClientHome home{};
        home.size = sizeof(home);
        host.home(&home);
        ImGui::TextUnformatted(i18n::tr("ui.home", "Home").c_str());
        i18n::disabled("ui.runtime_status", "Runtime status");
        ImGui::Spacing();
        ImGui::BeginChild("Runtime", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        pair(i18n::tr("ui.briefcasenative", "BriefcaseNative").c_str(), home.build.framework_version);
        pair(i18n::tr("ui.game", "Game").c_str(),
             i18n::tr("ui.deceive_inc_windows_client", "Deceive Inc. / Windows client").c_str());
        i18n::disabled("ui.build", "Build");
        ImGui::SameLine(205 * dpi);
        i18n::text("ui.08x_08x", "%08X  /  %08X", home.build.pe_timestamp, home.build.image_size);
        pair(i18n::tr("ui.unreal", "Unreal").c_str(),
             home.unreal_state == 2 ? i18n::tr("ui.unreal_detected", "4.27 (detected)").c_str()
                                    : i18n::tr("ui.unreal_game_build", "4.27.2 (game build)").c_str());
        pair(i18n::tr("ui.unreal_backend", "Unreal backend").c_str(),
             home.unreal_state == 2   ? i18n::tr("ui.state.ready", "Ready").c_str()
             : home.unreal_state == 3 ? i18n::tr("ui.state.error", "Error").c_str()
             : home.unreal_state == 1 ? i18n::tr("ui.state.starting", "Starting").c_str()
                                      : i18n::tr("ui.state.deferred", "Deferred / waiting").c_str());
        pair(i18n::tr("ui.graphics", "Graphics").c_str(),
             metrics.graphics_state == 3
                 ? i18n::tr("ui.graphics_recovering", "Direct3D 11 / recovering").c_str()
                 : i18n::tr("ui.graphics_ready", "Direct3D 11 / ready").c_str());
        i18n::disabled("ui.native_mods", "Native mods");
        ImGui::SameLine(205 * dpi);
        i18n::text("ui.u_loaded_u_installed", "%u loaded / %u installed", home.loaded, home.discovered);
        ImGui::EndChild();
        ImGui::Spacing();
        ImGui::BeginChild("Timings", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::TextUnformatted(i18n::tr("ui.initialization", "Initialization").c_str());
        ImGui::Separator();
        i18n::text("ui.bootstrap_2f_ms_unreal_2f_ms", "Bootstrap %.2f ms  /  Unreal %.2f ms",
                   home.bootstrap_ms, home.unreal_ms);
        i18n::text("ui.graphics_hook_2f_ms", "Graphics hook %.2f ms", metrics.hook_ms);
        i18n::text("ui.imgui_2f_ms_fonts_2f_ms_gpu_resources_2f_ms",
                   "ImGui %.2f ms  /  Fonts %.2f ms  /  GPU resources %.2f ms", metrics.context_ms,
                   metrics.fonts_ms, metrics.resources_ms);
        ImGui::Spacing();
        i18n::disabled("ui.framework_cpu_time_per_frame_present_wait_excluded",
                       "Framework CPU time per frame (Present wait excluded)");
        i18n::text("ui.closed_2f_us_open_2f_us", "Closed %.2f us  /  Open %.2f us", metrics.closed_average_us,
                   metrics.open_average_us);
        i18n::disabled("ui.menu_key_returns", "%s returns to the game. Alt+Tab keeps this menu open.",
                       keybind::name(host.menu_key ? host.menu_key() : input::default_menu_key).c_str());
        ImGui::EndChild();
    } else if (page == 1) {
        BcClientHome home{};
        home.size = sizeof(home);
        host.home(&home);
        i18n::text("ui.mods_u", "Mods  (%u)", home.discovered);
        i18n::disabled("ui.installed_native_packages", "Installed native packages");
        ImGui::Spacing();
        for (uint32_t i = 0; i < home.discovered; ++i) {
            BcClientModRow mod{};
            mod.size = sizeof(mod);
            if (host.mod(i, &mod) != BC_OK)
                continue;
            ImGui::PushID(int(i));
            ImGui::BeginChild("Package", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
            ImGui::TextUnformatted(i18n::tr(std::string("mods.") + mod.id + ".name", mod.name).c_str());
            ImGui::SameLine();
            i18n::disabled("ui.s", "%s", mod.version);
            const char *state = mod.state == BC_UI_LOADED ? i18n::tr("ui.state.loaded", "Loaded").c_str()
                                : mod.state == BC_UI_DISABLED
                                    ? i18n::tr("ui.state.disabled", "Disabled").c_str()
                                : mod.state == BC_UI_ERROR ? i18n::tr("ui.state.error", "Error").c_str()
                                                           : i18n::tr("ui.state.waiting", "Waiting").c_str();
            i18n::colored(mod.state == BC_UI_ERROR ? ImVec4{1, .45f, .45f, 1} : ImVec4{.78f, .5f, .96f, 1},
                          "ui.s", "%s", state);
            ImGui::SameLine();
            i18n::disabled("ui.s_s", "%s  /  %s", mod.environment, mod.author);
            i18n::disabled("ui.s", "%s", mod.id);
            i18n::wrapped("ui.dependencies_s", "Dependencies: %s",
                          mod.dependencies[0] ? mod.dependencies : i18n::tr("ui.none", "None").c_str());
            if (mod.error[0])
                i18n::wrapped("ui.s", "%s", mod.error);
            if (host.mod_settings && mod.state == BC_UI_LOADED) {
                static std::vector<char> settings_buffer(262144);
                if (host.mod_settings(mod.id, settings_buffer.data(), uint32_t(settings_buffer.size())) ==
                    BC_OK) {
                    auto config = nlohmann::json::parse(settings_buffer.data());
                    if (!config.value("runtimeText", std::string{}).empty())
                        notice("ModRuntime", config.value("runtimeTone", "info"),
                               i18n::tr(config.value("runtimeKey", ""), config.value("runtimeText", "")),
                               dpi);
                    if (ImGui::CollapsingHeader(
                            i18n::label("ui.mod_configuration", "Configuration").c_str())) {
                        const auto message = config.value("message", std::string{});
                        if (!message.empty())
                            notice("ClientSettingsNotice", config.value("tone", "info"),
                                   message == "saved"
                                       ? i18n::tr("ui.client_settings_saved", "Settings saved.")
                                       : message,
                                   dpi);
                        if (config.value("awaitingApply", false))
                            i18n::disabled("ui.client_settings_pending", "Applying on the game thread…");
                        administration::config_card(host, mod.id, config, config.value("pending", false), dpi,
                                                    "save", true);
                    }
                }
            }
            ImGui::EndChild();
            ImGui::PopID();
            ImGui::Spacing();
        }
        if (!home.discovered)
            i18n::disabled("ui.no_native_packages_installed", "No native packages installed.");
    }
    if (page == 2)
        servers_page(host, dpi, open);
    if (page == 3) {
        ImGui::TextUnformatted(i18n::tr("ui.settings", "Settings").c_str());
        i18n::disabled("ui.menu_preferences", "Menu language and preferences");
        ImGui::Spacing();
        ImGui::BeginChild("LanguageSettings", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::TextUnformatted(i18n::tr("ui.menu_language", "Menu language").c_str());
        ImGui::BeginDisabled(i18n::state.value("pending", false));
        ImGui::SetNextItemWidth(300 * dpi);
        auto current = i18n::language();
        auto name = i18n::state["languages"].value(current, current);
        if (ImGui::BeginCombo("##Language", name.c_str())) {
            for (auto it = i18n::state["languages"].begin(); it != i18n::state["languages"].end(); ++it)
                if (ImGui::Selectable(it.value().get_ref<const std::string &>().c_str(),
                                      current == it.key()) &&
                    host.locale_select)
                    host.locale_select(it.key().c_str());
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        i18n::wrapped("ui.language_inheritance_hint",
                      "Mods and the connected server follow this language. Missing translations use English "
                      "or the fallback label.");
        i18n::disabled("ui.preference_saved_locally", "This preference is saved on this client.");
        if (action_button(ActionTone::info,
                          i18n::label("ui.reload_languages", "Reload translations").c_str()) &&
            host.locale_select)
            host.locale_select(current.c_str());
        if (i18n::state.contains("errorKey"))
            notice("LanguageError", "danger",
                   i18n::tr(i18n::state.value("errorKey", "ui.language_load_failed"),
                            "Unable to save preferences."),
                   dpi);
        ImGui::EndChild();
        ImGui::Spacing();
        ImGui::BeginChild("KeybindSettings", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        {
            BoldFont bold;
            ImGui::TextUnformatted(i18n::tr("ui.keybind.menu", "Open / close the menu").c_str());
        }
        uint32_t binding = host.menu_key ? host.menu_key() : input::default_menu_key;
        static bool rejected = false;
        ImGui::BeginDisabled(i18n::state.value("pending", false) || !host.set_menu_key);
        ImGui::SetNextItemWidth(300 * dpi);
        if (keybind::draw("MenuKey", binding))
            rejected = host.set_menu_key(binding) != BC_OK;
        ImGui::SameLine();
        if (action_button(ActionTone::info, i18n::label("ui.keybind.reset", "Restore F1").c_str())) {
            keybind::cancel();
            rejected = host.set_menu_key(input::default_menu_key) != BC_OK;
        }
        ImGui::EndDisabled();
        i18n::wrapped("ui.keybind.help", "Click, then press a key without modifiers. Escape cancels.");
        i18n::disabled("ui.keybind.persisted", "The shortcut is saved automatically on this client.");
        if (rejected)
            notice("KeybindError", "danger",
                   i18n::tr("ui.keybind.save_failed", "Could not save the shortcut. Please try again."), dpi);
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::End();
    keybind::end_frame();
}
} // namespace bc::menu
