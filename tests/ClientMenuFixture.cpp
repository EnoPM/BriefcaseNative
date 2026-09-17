#include "data/ServerSettingsFixtures.hpp"
#include "data/ClientSettingsFixture.hpp"
#include "../runtime/Briefcase.Admin/Management.hpp"
#include "../runtime/Briefcase.Client.Menu/Administration.hpp"
#include "../runtime/Briefcase.Client.Menu/Font.hpp"
#include "../runtime/Briefcase.Client.Menu/Menu.hpp"
#include "../runtime/Briefcase.Client.Servers/ServerDirectory.hpp"
#include "../runtime/Briefcase.NativeHost/Configuration.hpp"
#include <Windows.h>
#include <d3d11.h>
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_internal.h>
#include <iostream>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
static unsigned checks, server_reads;
static bool open = true;
static uint64_t joined{};
static bc::servers::Directory directory;
static void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
static BcResult BC_CALL home(BcClientHome *h) {
    *h = {};
    h->size = sizeof(*h);
    h->unreal_state = 2;
    h->discovered = h->loaded = 1;
    strcpy_s(h->build.framework_version, "0.3.0");
    h->build.pe_timestamp = 0x6A96564B;
    h->build.image_size = 0x06283000;
    return BC_OK;
}
static BcResult BC_CALL mod(uint32_t index, BcClientModRow *out) {
    if (index)
        return BC_NOT_FOUND;
    *out = {};
    out->size = sizeof(*out);
    out->state = BC_UI_LOADED;
    strcpy_s(out->id, "sample.settings");
    strcpy_s(out->name, "Settings Sample");
    strcpy_s(out->version, "0.1.0");
    strcpy_s(out->author, "BriefcaseNative");
    strcpy_s(out->environment, "client");
    return BC_OK;
}
static nlohmann::json local_config;
static unsigned local_saves;
static BcResult BC_CALL local_settings(const char *id, char *buffer, uint32_t size) {
    if (std::string(id) != "sample.settings")
        return BC_NOT_FOUND;
    const auto text = local_config.dump();
    if (text.size() >= size)
        return BC_LIMIT;
    strcpy_s(buffer, size, text.c_str());
    return BC_OK;
}
static BcResult BC_CALL local_save(const char *id, uint64_t revision, const char *text) {
    check(std::string(id) == "sample.settings" && revision == 1, "local save identity/revision");
    local_config["saved"] = local_config["active"] =
        bc::normalize_config(local_config["schema"], nlohmann::json::parse(text));
    local_config["revision"] = "2";
    ++local_saves;
    return BC_OK;
}
static BcResult BC_CALL servers(BcClientServerList *out) {
    ++server_reads;
    *out = {};
    out->size = sizeof(*out);
    auto snapshot = directory.snapshot();
    out->writable = snapshot.writable;
    out->pending = snapshot.pending;
    out->count = uint32_t(snapshot.entries.size());
    out->join_state = joined ? 2 : 0;
    strcpy_s(out->message, snapshot.message.c_str());
    for (size_t i = 0; i < snapshot.entries.size(); ++i) {
        out->entries[i].id = snapshot.entries[i].id;
        strcpy_s(out->entries[i].name, snapshot.entries[i].name.c_str());
        strcpy_s(out->entries[i].endpoint, snapshot.entries[i].endpoint.c_str());
    }
    return BC_OK;
}
static BcResult BC_CALL add(const char *n, const char *e) {
    return directory.add(n, e);
}
static BcResult BC_CALL remove(uint64_t id) {
    return directory.remove(id);
}
static BcResult BC_CALL join(uint64_t id) {
    joined = id;
    return BC_OK;
}
static nlohmann::json admin_state = nlohmann::json::object();
static uint64_t admin_sequence = 1, selected_admin{};
static unsigned admin_connections{}, admin_refreshes{}, admin_saves{};
static BcResult BC_CALL select_admin(uint64_t id) {
    selected_admin = id;
    ++admin_sequence;
    admin_state = {{"favoriteId", id},
                   {"pending", false},
                   {"state", "idle"},
                   {"endpoint", "127.0.0.1:32189"},
                   {"fingerprint", std::string(64, 'a')},
                   {"message", ""}};
    return BC_OK;
}
static void ready_admin() {
    using J = nlohmann::json;
    admin_state["state"] = "ready";
    admin_state["tls"] = "TLSv1.3";
    admin_state["pending"] = false;
    admin_state["server"] = {{"framework", "0.3.0"},  {"gameBuild", "6A966107 / 05B60000"},
                             {"unreal", "4.27"},      {"backendState", 2},
                             {"uptimeSeconds", 1234}, {"loadedMods", 3},
                             {"discoveredMods", 3}};
    admin_state["mods"] = J::array();
    admin_state["configs"] = J::object();
    for (auto &[id, name, schema] : std::array<std::tuple<const char *, const char *, std::string_view>, 3>{
             {{"sample.settings-alpha", "Sample Alpha", sample_alpha::schema},
              {"sample.settings-beta", "Sample Beta", sample_beta::schema},
              {"sample.settings-gamma", "Sample Gamma", sample_gamma::schema}}}) {
        auto parsed = J::parse(schema);
        J values = J::object();
        for (auto it = parsed["properties"].begin(); it != parsed["properties"].end(); ++it)
            values[it.key()] = it.value()["default"];
        admin_state["mods"].push_back({{"id", id},
                                       {"name", name},
                                       {"version", "0.1.0"},
                                       {"state", 1},
                                       {"author", "Briefcase"},
                                       {"dependencies", J::array()},
                                       {"editable", true}});
        admin_state["configs"][id] = {{"modId", id},
                                      {"schema", parsed},
                                      {"active", values},
                                      {"saved", values},
                                      {"revision", std::string(64, 'b')},
                                      {"restartRequired", false}};
    }
    ++admin_sequence;
}
static void extended_admin() {
    using J = nlohmann::json;
    admin_state["server"]["administrationVersion"] = 2;
    auto schema = bc::admin::Management::config_schema();
    auto values = J::object();
    for (auto it = schema["properties"].begin(); it != schema["properties"].end(); ++it)
        values[it.key()] = it.value()["default"];
    admin_state["serverConfig"] = {{"schema", schema},
                                   {"active", values},
                                   {"saved", values},
                                   {"revision", std::string(64, 'b')},
                                   {"restartRequired", false}};
    J enabled = J::array();
    for (auto &m : admin_state["mods"])
        enabled.push_back(m["id"]);
    admin_state["selection"] = {{"saved", enabled},
                                {"active", enabled},
                                {"revision", std::string(64, 'b')},
                                {"restartRequired", false}};
    admin_state["balance"] = {{"groups", {{"Ace", 2}, {"Yumi", 2}}},
                              {"group", ""},
                              {"entries", J::array()},
                              {"revision", std::string(64, 'b')},
                              {"restartRequired", false},
                              {"available", true}};
    ++admin_sequence;
}
static unsigned log_reads{}, restart_requests{}, balance_reads{}, balance_saves{}, admin_disconnects{};
static bool reject_balance_read{};
static BcResult BC_CALL connect_admin(uint64_t id, const char *endpoint, const char *pin,
                                      const char *password, uint32_t) {
    check(id == selected_admin && std::string(endpoint) == "127.0.0.1:32189" && std::string(pin).size() == 64,
          "admin connection target");
    check(std::string(password) == "fixture-test-password", "password dispatched");
    ++admin_connections;
    ready_admin();
    return BC_OK;
}
static BcResult BC_CALL command_admin(const char *operation, const char *payload) {
    using J = nlohmann::json;
    if (std::string(operation) == "refresh")
        ++admin_refreshes;
    if (std::string(operation) == "save") {
        ++admin_saves;
        auto p = J::parse(payload);
        auto id = p.at("modId").get<std::string>();
        check(p["expectedRevision"] == admin_state["configs"][id]["revision"], "draft revision passed");
        admin_state["configs"][id]["saved"] = p.at("values");
        admin_state["configs"][id]["revision"] = std::string(64, 'c');
        admin_state["configs"][id]["restartRequired"] = true;
    }
    auto op = std::string(operation);
    if (op == "server.logs") {
        ++log_reads;
        admin_state["logs"] = {{"source", "framework"},
                               {"text", "[12 ms] Server ready\n[56 ms] Sample modules loaded\n"},
                               {"truncated", false}};
    }
    if (op == "server.restart")
        ++restart_requests;
    if (op == "balance.read") {
        ++balance_reads;
        if (reject_balance_read) {
            admin_state["message"] = "Fixture group read failed";
            admin_state["messageKind"] = "danger";
            admin_state["errorCode"] = "unavailable";
            ++admin_sequence;
            return BC_OK;
        }
        auto p = J::parse(payload);
        admin_state["balance"]["group"] = p["group"];
        admin_state["balance"]["entries"] = J::array({{{"id", "damage"},
                                                       {"table", "DT_Balancing_HitscanWeapons"},
                                                       {"row", "Ace_Weapon_Base"},
                                                       {"field", "Damage"},
                                                       {"saved", 15},
                                                       {"active", 15},
                                                       {"default", 15},
                                                       {"editable", true},
                                                       {"allowedRange", "0 to 100"},
                                                       {"info", "Weapon damage"}},
                                                      {{"id", "speed"},
                                                       {"table", "DT_Balancing_Projectiles"},
                                                       {"row", "Ace_Projectile_Base"},
                                                       {"field", "Speed"},
                                                       {"saved", 1200},
                                                       {"active", 1200},
                                                       {"default", 1200},
                                                       {"editable", true},
                                                       {"allowedRange", "0 to 10000"}}});
    }
    if (op == "balance.write") {
        ++balance_saves;
        auto p = J::parse(payload);
        for (auto &change : p["changes"])
            for (auto &x : admin_state["balance"]["entries"])
                if (x["id"] == change["id"])
                    x["saved"] = change["value"];
        admin_state["balance"]["revision"] = std::string(64, 'c');
        admin_state["balance"]["restartRequired"] = true;
    }
    ++admin_sequence;
    return BC_OK;
}
static void BC_CALL disconnect_admin() {
    ++admin_disconnects;
    admin_state["state"] = "idle";
    ++admin_sequence;
}
static BcResult BC_CALL snapshot_admin(char *out, uint32_t size, uint64_t *sequence) {
    if (*sequence == admin_sequence) {
        out[0] = 0;
        return BC_OK;
    }
    auto text = admin_state.dump();
    if (text.size() >= size)
        return BC_LIMIT;
    memcpy(out, text.c_str(), text.size() + 1);
    *sequence = admin_sequence;
    return BC_OK;
}
static nlohmann::json fixture_locale;
static uint64_t fixture_locale_sequence = 1;
static BcResult BC_CALL snapshot_locale(char *out, uint32_t size, uint64_t *sequence) {
    if (*sequence == fixture_locale_sequence) {
        out[0] = 0;
        return BC_OK;
    }
    auto text = fixture_locale.dump();
    if (text.size() >= size)
        return BC_LIMIT;
    memcpy(out, text.c_str(), text.size() + 1);
    *sequence = fixture_locale_sequence;
    return BC_OK;
}
static BcResult BC_CALL select_locale(const char *language) {
    fixture_locale["language"] = language;
    ++fixture_locale_sequence;
    return BC_OK;
}
static uint32_t fixture_menu_key = VK_F1;
static unsigned menu_key_saves{};
static uint32_t BC_CALL menu_key() {
    return fixture_menu_key;
}
static BcResult BC_CALL set_menu_key(uint32_t key) {
    fixture_menu_key = key;
    ++menu_key_saves;
    return BC_OK;
}
static BcClientHostApi host{sizeof(host),
                            7,
                            nullptr,
                            home,
                            mod,
                            nullptr,
                            nullptr,
                            nullptr,
                            servers,
                            add,
                            remove,
                            join,
                            select_admin,
                            connect_admin,
                            command_admin,
                            disconnect_admin,
                            snapshot_admin,
                            snapshot_locale,
                            select_locale,
                            local_settings,
                            local_save,
                            menu_key,
                            set_menu_key};
static bool show_keybind_fixture{};
static uint32_t fixture_binding = 2;
static unsigned binding_changes{};
static ImVec2 binding_start{}, binding_end{};
static void draw_keybind_fixture() {
    namespace kb = bc::menu::keybind;
    kb::begin_frame();
    ImGui::SetNextWindowPos({60, 60});
    ImGui::SetNextWindowSize({550, 240});
    ImGui::Begin("Key binding fixture", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("Optional keyboard / mouse shortcut");
    binding_start = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemWidth(350);
    if (kb::draw("Shortcut", fixture_binding, true, true))
        ++binding_changes;
    binding_end = ImGui::GetItemRectMax();
    ImGui::End();
    kb::end_frame();
}
static bool show_numeric_fixture{}, numeric_disabled{};
static int numeric_integer = 4;
static double numeric_decimal = .3;
static ImVec2 integer_end{}, decimal_end{};
static void draw_numeric_fixture() {
    ImGui::SetNextWindowPos({60, 60});
    ImGui::SetNextWindowSize({550, 240});
    ImGui::Begin("Numeric controls", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6, 7.5f});
    ImGui::BeginDisabled(numeric_disabled);
    ImGui::TextUnformatted("Integer");
    ImGui::SetNextItemWidth(350);
    bc::menu::numbers::input_integer("##Integer", numeric_integer);
    integer_end = ImGui::GetItemRectMax();
    ImGui::TextUnformatted("Decimal");
    ImGui::SetNextItemWidth(350);
    bc::menu::numbers::input_decimal("##Decimal", numeric_decimal);
    decimal_end = ImGui::GetItemRectMax();
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::End();
}
struct Graphics {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> buffer;
    ComPtr<ID3D11RenderTargetView> target;
    void create(unsigned width = 1280, unsigned height = 800) {
        if (!device)
            check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                              D3D11_SDK_VERSION, &device, nullptr, &context)),
                  "isolated WARP device");
        context->OMSetRenderTargets(0, nullptr, nullptr);
        target.Reset();
        buffer.Reset();
        D3D11_TEXTURE2D_DESC d{};
        d.Width = width;
        d.Height = height;
        d.MipLevels = d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET;
        check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &buffer)), "offscreen texture");
        check(SUCCEEDED(device->CreateRenderTargetView(buffer.Get(), nullptr, &target)), "offscreen target");
        ImGui::GetIO().DisplaySize = {float(width), float(height)};
    }
    void frame(unsigned count = 1) {
        for (unsigned i = 0; i < count; ++i) {
            auto &io = ImGui::GetIO();
            io.DeltaTime = 1.f / 60;
            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();
            BcClientMetrics metrics{sizeof(metrics)};
            metrics.graphics_state = 2;
            if (show_keybind_fixture)
                draw_keybind_fixture();
            else if (show_numeric_fixture)
                draw_numeric_fixture();
            else if (open)
                bc::menu::draw(host, metrics, open);
            ImGui::Render();
            const float background[]{.035f, .048f, .07f, 1};
            context->ClearRenderTargetView(target.Get(), background);
            auto *view = target.Get();
            context->OMSetRenderTargets(1, &view, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
    }
    void click(ImVec2 point) {
        auto &io = ImGui::GetIO();
        io.AddMousePosEvent(point.x, point.y);
        frame(2);
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame(2);
    }
    void png(const wchar_t *path) {
        ComPtr<ID3D11Texture2D> staging;
        D3D11_TEXTURE2D_DESC d{};
        buffer->GetDesc(&d);
        d.Usage = D3D11_USAGE_STAGING;
        d.BindFlags = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        d.MiscFlags = 0;
        check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &staging)), "snapshot staging");
        context->CopyResource(staging.Get(), buffer.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "snapshot map");
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&factory))),
              "WIC factory");
        check(SUCCEEDED(factory->CreateStream(&stream)) &&
                  SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)),
              "PNG stream");
        check(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
                  SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)),
              "PNG encoder");
        check(SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)),
              "PNG frame");
        frame->SetSize(d.Width, d.Height);
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        check(SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGRA,
              "PNG RGBA");
        std::vector<BYTE> pixels(d.Width * d.Height * 4);
        for (UINT y = 0; y < d.Height; ++y)
            for (UINT x = 0; x < d.Width; ++x) {
                const auto *in = static_cast<BYTE *>(map.pData) + y * map.RowPitch + x * 4;
                auto *out = pixels.data() + (y * d.Width + x) * 4;
                out[0] = in[2];
                out[1] = in[1];
                out[2] = in[0];
                out[3] = in[3];
            }
        check(SUCCEEDED(frame->WritePixels(d.Height, d.Width * 4, UINT(pixels.size()), pixels.data())),
              "PNG pixels");
        frame->Commit();
        encoder->Commit();
        context->Unmap(staging.Get(), 0);
    }
};
static ImGuiWindow *window(const char *fragment) {
    for (auto *w : GImGui->Windows)
        if (w->Active && strstr(w->Name, fragment))
            return w;
    throw std::runtime_error(std::string("window missing: ") + fragment);
}
static ImVec2 tab_position(const char *label) {
    for (auto &bar : GImGui->TabBars.Buf)
        for (auto &tab : bar.Tabs)
            if (std::string(ImGui::TabBarGetTabName(&bar, &tab)).find(std::string(label) + "###") !=
                std::string::npos)
                return {bar.BarRect.Min.x + tab.Offset + tab.Width / 2, bar.BarRect.GetCenter().y};
    throw std::runtime_error("Administration tab missing");
}
int main() {
    try {
        auto catalogue = bc::locale::read_languages(
            std::filesystem::path(__FILE__).parent_path().parent_path() / "resources" / "Localization");
        catalogue["en"]["mods.sample.settings.settings.enabled"]="Show sample text";
        catalogue["fr"]["mods.sample.settings.settings.enabled"]="Afficher le texte exemple";
        const auto sample_schema = nlohmann::json::parse(sample_settings::schema_text);
        const auto sample_defaults = bc::normalize_config(sample_schema, nlohmann::json::object());
        local_config = {{"schema", sample_schema},
                        {"saved", sample_defaults},
                        {"active", sample_defaults},
                        {"revision", "1"},
                        {"modId", "sample.settings"},
                        {"pending", false},
                        {"live", true}};
        fixture_locale = {{"language", "fr"},
                          {"catalogues", catalogue},
                          {"languages", {{"fr", "Français"}, {"en", "English"}}},
                          {"pending", false}};
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::filesystem::create_directories("menu-fixture");
        const auto data = std::filesystem::current_path() / "menu-fixture" /
                          ("servers-" + std::to_string(GetCurrentProcessId()) + ".json");
        directory.load(data);
        directory.add("Serveur de développement", "127.0.0.1:7777");
        directory.process_one();
        directory.add("Communauté Briefcase", "play.example.org:7778");
        directory.process_one();
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        bc::menu::load_fonts("C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\segoeuib.ttf", 17);
        bc::menu::style(1);
        Graphics gfx;
        gfx.create();
        check(ImGui_ImplDX11_Init(gfx.device.Get(), gfx.context.Get()), "ImGui renderer");
        gfx.frame(3);
        auto *initial = window("Briefcase##Framework");
        check(initial->Pos.x == 64 && initial->Pos.y == 80 && initial->Size.x == 1152 &&
                  initial->Size.y == 640,
              "menu margins at 1280x800");
        check((initial->Flags & (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) ==
                  (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize),
              "menu movement and resizing disabled");
        gfx.png(L"menu-fixture/home.png");
        auto *nav = window("/Navigation_");
        gfx.click({nav->Pos.x + 70, nav->DC.CursorStartPos.y + (44 + 10) + 22});
        gfx.frame(3);
        auto *sample_card = window("/Package_");
        sample_card->StateStorage.SetInt(
            sample_card->GetID(bc::menu::i18n::label("ui.mod_configuration", "Configuration").c_str()), 1);
        gfx.frame(4);
        check(bc::menu::administration::drafts.contains("local:sample.settings"),
              "client mod editor absent");
        check(bc::menu::i18n::tr("mods.sample.settings.settings.enabled", "") == "Afficher le texte exemple",
              "Settings Sample translations absent");
        gfx.png(L"menu-fixture/sample-configuration.png");
        strcpy_s(bc::menu::administration::configuration_filter, "enabled");
        gfx.frame(4);
        auto &local_draft = bc::menu::administration::drafts.at("local:sample.settings");
        local_draft.values["enabled"] = true;
        gfx.frame(2);
        gfx.click({sample_card->WorkRect.Min.x + 85,
                   sample_card->DC.CursorPosPrevLine.y + ImGui::GetFrameHeight() / 2});
        check(local_saves == 1 && admin_saves == 0, "local settings routed through server administration");
        gfx.frame(2);
        gfx.png(L"menu-fixture/sample-filtered.png");
        bc::menu::administration::configuration_filter[0] = 0;
        gfx.click({nav->Pos.x + 70, nav->DC.CursorStartPos.y + 3 * (44 + 10) + 22});
        gfx.frame(3);
        auto *language = window("/LanguageSettings");
        gfx.png(L"menu-fixture/settings-fr.png");
        gfx.click({language->WorkRect.Min.x + 140, language->DC.CursorStartPos.y +
                                                       ImGui::GetTextLineHeightWithSpacing() +
                                                       ImGui::GetFrameHeight() / 2});
        auto *languages = window("##Combo_");
        gfx.click(
            {languages->WorkRect.Min.x + 45, languages->WorkRect.Min.y + ImGui::GetTextLineHeight() / 2});
        gfx.frame(3);
        check(fixture_locale["language"] == "en" &&
                  bc::menu::i18n::tr("ui.settings", "Settings") == "Settings",
              "language combo changes menu");
        gfx.png(L"menu-fixture/settings-en.png");
        namespace keybind = bc::menu::keybind;
        auto *binding = window("/KeybindSettings");
        auto key_point = ImVec2{binding->DC.CursorStartPos.x + 130,
                                binding->DC.CursorStartPos.y + ImGui::GetTextLineHeightWithSpacing() +
                                    ImGui::GetFrameHeight() / 2};
        gfx.click(key_point);
        check(keybind::active(), "keybind button did not enter listening mode");
        gfx.png(L"menu-fixture/keybind-listening.png");
        io.AddKeyEvent(ImGuiKey_F6, true);
        gfx.frame(2);
        check(fixture_menu_key == VK_F6 && menu_key_saves == 1 && !keybind::active(),
              "keybind assignment not dispatched");
        io.AddKeyEvent(ImGuiKey_F6, false);
        gfx.frame(2);
        gfx.png(L"menu-fixture/keybind-saved.png");
        gfx.click(key_point);
        io.AddKeyEvent(ImGuiKey_Escape, true);
        gfx.frame(2);
        check(!keybind::active() && fixture_menu_key == VK_F6 && menu_key_saves == 1,
              "Escape changed keybind");
        io.AddKeyEvent(ImGuiKey_Escape, false);
        gfx.frame(2);
        gfx.click(key_point);
        io.AddKeyEvent(ImGuiMod_Alt, true);
        io.AddKeyEvent(ImGuiKey_F4, true);
        gfx.frame(2);
        check(keybind::active() && menu_key_saves == 1, "Alt+F4 was assigned as a shortcut");
        io.AddKeyEvent(ImGuiKey_F4, false);
        io.AddKeyEvent(ImGuiMod_Alt, false);
        gfx.frame(2);
        io.AddKeyEvent(ImGuiKey_F1, true);
        gfx.frame(2);
        check(!keybind::active() && fixture_menu_key == VK_F1 && menu_key_saves == 2,
              "menu key unavailable to keybind widget");
        io.AddKeyEvent(ImGuiKey_F1, false);
        gfx.frame(2);
        io.AddKeyEvent(ImGuiKey_A, true);
        gfx.frame(2);
        gfx.click(key_point);
        gfx.frame(3);
        check(keybind::active() && menu_key_saves == 2, "held key accidentally assigned");
        io.AddKeyEvent(ImGuiKey_A, false);
        gfx.frame(2);
        io.AddKeyEvent(ImGuiKey_B, true);
        gfx.frame(2);
        check(fixture_menu_key == 'B' && menu_key_saves == 3, "released key did not arm selector");
        io.AddKeyEvent(ImGuiKey_B, false);
        gfx.frame(2);
        gfx.click({binding->DC.CursorStartPos.x + 360, key_point.y});
        gfx.frame(2);
        check(fixture_menu_key == VK_F1 && menu_key_saves == 4, "restore F1 failed");
        gfx.click(key_point);
        io.AddFocusEvent(false);
        gfx.frame(2);
        check(!keybind::active(), "focus loss left key capture armed");
        io.AddFocusEvent(true);
        gfx.frame(2);
        gfx.click(key_point);
        gfx.click({nav->Pos.x + 70, nav->DC.CursorStartPos.y + 22});
        gfx.frame(2);
        check(!keybind::active(), "navigation left hidden keybind capture armed");
        gfx.click({nav->Pos.x + 70, nav->DC.CursorStartPos.y + 3 * (44 + 10) + 22});
        gfx.frame(2);
        select_locale("fr");
        gfx.frame(2);

        gfx.click({nav->Pos.x + 70, nav->DC.CursorStartPos.y + 2 * (44 + 10) + 22});
        check(server_reads > 0, "Servers navigation via icon/text button");
        gfx.frame(5); // Let newly visible auto-height cards settle before the screenshot.
        unsigned visible_cards = 0;
        for (auto *w : GImGui->Windows)
            if (w->Active && strstr(w->Name, "/Server_") && w->Pos.y + w->Size.y <= 730)
                ++visible_cards;
        check(visible_cards == 2, "both server cards visible");
        gfx.png(L"menu-fixture/servers.png");
        auto *content = window("/Content_");
        const ImVec2 add_position{content->WorkRect.Max.x - 18, content->DC.CursorStartPos.y + 18};
        io.AddMousePosEvent(add_position.x, add_position.y);
        gfx.frame(30);
        check(window("##Tooltip_")->Active, "add icon tooltip appears");
        gfx.png(L"menu-fixture/tooltip.png");
        gfx.click(add_position);
        auto *dialog = window("Ajouter un serveur");
        check(dialog->Flags & ImGuiWindowFlags_Modal, "add action opens modal");
        const float opening_width = dialog->Size.x;
        gfx.frame(180);
        std::cout << "Modal width: opening=" << opening_width << ", after 180 frames=" << dialog->Size.x
                  << "\n";
        check(dialog->Size.x == opening_width, "add modal width stays stable while idle");
        gfx.png(L"menu-fixture/add.png");
        // The form gives initial focus to Name, then keyboard Tab reaches Address.
        io.AddInputCharactersUTF8("Fixture ajouté");
        gfx.frame(2);
        io.AddKeyEvent(ImGuiKey_Tab, true);
        gfx.frame();
        io.AddKeyEvent(ImGuiKey_Tab, false);
        gfx.frame(2);
        io.AddInputCharactersUTF8("[::1]:8888");
        gfx.frame(2);
        io.AddKeyEvent(ImGuiKey_Enter, true);
        gfx.frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        gfx.frame(2);
        check(directory.snapshot().pending, "form dispatches add through host service");
        directory.process_one();
        gfx.frame(2);
        check(directory.snapshot().entries.size() == 3, "form adds server");
        // Shield opens the administration form without joining the game.
        auto *admin_card = window("/Server_");
        gfx.click({admin_card->WorkRect.Max.x - 114, admin_card->DC.CursorStartPos.y + 18});
        check(selected_admin == directory.snapshot().entries.front().id && joined == 0,
              "admin action uses selected server, does not join");
        gfx.frame(3);
        gfx.png(L"menu-fixture/admin-login.png");
        check(bc::menu::administration::form_selected == selected_admin, "pairing fields populated");
        strcpy_s(bc::menu::administration::password, "fixture-test-password");
        // Exercise the actual button using ImGui's navigation focus in this owned fixture.
        auto *form = window("/AdminConnection");
        gfx.click({form->WorkRect.Min.x + 65, form->DC.CursorPosPrevLine.y + ImGui::GetFrameHeight() / 2});
        check(admin_connections == 1, "admin connect button submits credentials");
        gfx.frame(3);
        gfx.png(L"menu-fixture/admin-ready.png");
        check(admin_state["state"] == "ready", "admin state displayed");
        check(bc::menu::administration::password[0] == 0, "menu password cleared after submission");
        auto &draft = bc::menu::administration::drafts["sample.settings-alpha"];
        draft.values["itemLimit"] = 10;
        ImGui::SetScrollY(window("/Content"), 200);
        gfx.frame(2);
        // Find the first visible card's save button position from its own layout.
        auto *remote = window("/RemoteMod");
        gfx.click(
            {remote->WorkRect.Min.x + 145, remote->DC.CursorPosPrevLine.y + ImGui::GetFrameHeight() / 2});
        check(admin_saves == 1, "save settings button dispatched through private host bridge");
        gfx.frame(3);
        gfx.png(L"menu-fixture/admin-saved.png");
        check(admin_state["configs"]["sample.settings-alpha"]["active"]["itemLimit"] == 12 &&
                  admin_state["configs"]["sample.settings-alpha"]["saved"]["itemLimit"] == 10,
              "saved values do not replace active values");
        extended_admin();
        gfx.frame(3);
        gfx.png(L"menu-fixture/admin-mods.png");
        gfx.click(tab_position("Serveur"));
        gfx.frame(3);
        check(window("/ServerPage")->Active, "Server tab opens");
        gfx.png(L"menu-fixture/admin-server.png");
        // Logs are only requested after clicking their refresh control.
        check(log_reads == 0, "background log polling");
        auto *tools = window("/ServerPage");
        // Record the controls through the owned ImGui navigation log, then exercise via positions.
        ImGui::SetScrollY(tools, tools->ScrollMax.y);
        gfx.frame(3);
        auto *log = window("/LogOutput");
        gfx.png(L"menu-fixture/admin-log-controls.png");
        std::cout << "Log output " << log->Pos.x << "," << log->Pos.y << " frame " << ImGui::GetFrameHeight()
                  << " spacing " << ImGui::GetStyle().ItemSpacing.y << "\n";
        gfx.click({log->Pos.x + 225, log->Pos.y - 56});
        check(log_reads == 1, "logs refresh button dispatch");
        gfx.frame(2);
        gfx.png(L"menu-fixture/admin-logs.png");
        gfx.click(tab_position("Configuration"));
        gfx.frame(3);
        check(window("/ConfigurationPage")->Active, "Configuration tab opens");
        gfx.png(L"menu-fixture/admin-configuration.png");
        // Capture the header under the pointer and the full map rotation for visual review.
        for (int i = 0; i < GImGui->Tables.GetMapSize(); ++i)
            if (auto *table = GImGui->Tables.TryGetMapData(i)) {
                if (table->LastFrameActive == GImGui->FrameCount && table->ColumnsCount == 3 &&
                    table->OuterRect.Min.y > window("/ConfigurationPage")->ClipRect.Min.y &&
                    table->OuterRect.Min.y + 30 < window("/ConfigurationPage")->ClipRect.Max.y) {
                    io.AddMousePosEvent(table->Columns[0].WorkMinX + 45, table->OuterRect.Min.y + 12);
                    gfx.frame(3);
                    gfx.png(L"menu-fixture/table-header-hover.png");
                    break;
                }
            }
        io.AddMousePosEvent(0, 0);
        strcpy_s(bc::menu::administration::configuration_filter, "MapRotation");
        gfx.create(1280, 1100);
        gfx.frame(3);
        gfx.png(L"menu-fixture/map-rotation.png");
        bc::menu::administration::configuration_filter[0] = 0;
        gfx.create(1280, 800);
        gfx.frame(3);

        for (int i = 0; i < GImGui->Tables.GetMapSize(); ++i)
            if (auto *table = GImGui->Tables.TryGetMapData(i))
                if (table->LastFrameActive == GImGui->FrameCount)
                    check(table->OuterPaddingX >= 10 &&
                              table->Columns[0].WorkMinX - table->OuterRect.Min.x >= 10,
                          "first column padding applies to table header and body");
        strcpy_s(bc::menu::administration::configuration_filter, "50000");
        gfx.frame(3);
        gfx.png(L"menu-fixture/configuration-search.png");
        bc::menu::administration::configuration_filter[0] = 0;
        admin_state["translations"] = {{"fr", {{"server.settings.ServerName", "Nom communautaire personnalisé"}}}};
        ++admin_sequence;
        gfx.frame(2);
        check(bc::menu::i18n::tr("server.settings.ServerName", "Server name") ==
                  "Nom communautaire personnalisé",
              "server label shown in configured language");
        admin_state["message"] = "Enregistré";
        admin_state["messageKey"] = "ui.message.saved";
        admin_state["messageKind"] = "success";
        ++admin_sequence;
        gfx.frame(3);
        gfx.png(L"menu-fixture/notice-success.png");
        admin_state["messageKind"] = "danger";
        ++admin_sequence;
        gfx.frame(3);
        gfx.png(L"menu-fixture/notice-danger.png");
        admin_state["messageKind"] = "warning";
        ++admin_sequence;
        gfx.frame(3);
        gfx.png(L"menu-fixture/notice-warning.png");
        admin_state["messageKind"] = "info";
        ++admin_sequence;
        gfx.frame(3);
        gfx.png(L"menu-fixture/notice-info.png");
        admin_state["message"] = "";
        admin_state.erase("messageKey");
        ++admin_sequence;
        gfx.frame(3);

        gfx.click(tab_position("Équilibrage"));
        gfx.frame(3);
        auto *balance_page = window("/BalancePage");
        gfx.click({balance_page->WorkRect.Min.x + 110, balance_page->DC.CursorStartPos.y + 56});
        gfx.frame(2);
        auto *popup = window("##Combo_");
        gfx.click({popup->WorkRect.Min.x + 40, popup->WorkRect.Min.y + ImGui::GetTextLineHeight() / 2});
        gfx.frame(3);
        check(balance_reads == 1 && bc::menu::administration::balance_group == "Ace",
              "character selection loads balance");
        gfx.png(L"menu-fixture/admin-balance.png");
        auto &balance_draft = bc::menu::administration::balance_drafts["Ace"];
        balance_draft.values["damage"] = 18;
        gfx.frame(2);
        // Save is immediately under the group and filter rows.
        gfx.click({balance_page->WorkRect.Min.x + 100, balance_page->DC.CursorStartPos.y + 162});
        check(balance_saves == 1, "balance save dispatch");
        gfx.frame(3);
        gfx.png(L"menu-fixture/admin-balance-saved.png");
        check(admin_state["balance"]["entries"][0]["active"] == 15 &&
                  admin_state["balance"]["entries"][0]["saved"] == 18,
              "balance active value preserved");
        check(restart_requests == 0, "page navigation never restarts a server");
        const auto readsBeforeReconnect = balance_reads;
        content = window("/Content_");
        // Click the distinct logout icon, reconnect, and receive the empty group
        // inventory sent by the real service. The selected character must recover.
        gfx.click({content->WorkRect.Max.x - 18, content->DC.CursorStartPos.y + 18});
        check(admin_disconnects == 1 && admin_state["state"] == "idle",
              "logout icon disconnects only administration");
        check(open, "logout closed framework menu");
        ready_admin();
        extended_admin();
        gfx.frame(5);
        check(bc::menu::administration::balance_group == "Ace" && admin_state["balance"]["group"] == "Ace" &&
                  balance_reads == readsBeforeReconnect + 1,
              "selected balance group not reloaded on reconnect");
        check(bc::menu::administration::balance_drafts["Ace"].values["damage"] == 15,
              "stale draft leaked into reconnected session");
        gfx.frame(30);
        check(balance_reads == readsBeforeReconnect + 1, "group reread on every frame");
        gfx.png(L"menu-fixture/admin-balance-reconnected.png");
        // Global refresh also sends an empty group; preserve the selection and load once.
        extended_admin();
        gfx.frame(5);
        check(balance_reads == readsBeforeReconnect + 2 && admin_state["balance"]["group"] == "Ace",
              "group disappeared after global refresh");
        reject_balance_read = true;
        extended_admin();
        gfx.frame(30);
        check(balance_reads == readsBeforeReconnect + 3, "failed group read retried automatically");
        reject_balance_read = false;
        admin_state.erase("errorCode");
        admin_state["message"] = "";
        ++admin_sequence;
        gfx.frame(3);
        auto *balance_retry = window("/BalancePage");
        gfx.click({balance_retry->WorkRect.Min.x + 85,
                   balance_retry->DC.CursorPosPrevLine.y + ImGui::GetFrameHeight() / 2});
        gfx.frame(3);
        check(balance_reads == readsBeforeReconnect + 4 && admin_state["balance"]["group"] == "Ace",
              "manual group retry did not restore contents");

        // A different favorite must not inherit this server's selected group.
        auto other = directory.snapshot().entries[1];
        BcClientServerRow otherRow{};
        otherRow.id = other.id;
        strcpy_s(otherRow.name, other.name.c_str());
        strcpy_s(otherRow.endpoint, other.endpoint.c_str());
        bc::menu::administration::select(host, otherRow);
        ready_admin();
        extended_admin();
        gfx.frame(4);
        check(bc::menu::administration::balance_group.empty(), "balance group leaked into another server");
        bc::menu::administration::close(host);
        gfx.frame(2);
        check(admin_state["state"] == "ready", "back to list disconnected administration");
        // Actions of first card, using its actual bounds rather than desktop coordinates.
        auto *card = window("/Server_");
        gfx.click({card->WorkRect.Max.x - 18, card->DC.CursorStartPos.y + 18});
        check(directory.snapshot().pending, "remove icon dispatches");
        directory.process_one();
        gfx.frame(2);
        check(directory.snapshot().entries.size() == 2, "remove persists selected row");
        card = window("/Server_");
        const auto first = directory.snapshot().entries.front().id;
        gfx.click({card->WorkRect.Max.x - 66, card->DC.CursorStartPos.y + 18});
        check(joined == first, "join action uses stable selected server ID");
        check(!open, "menu closes after accepted connection dispatch");
        open = true;
        joined = 0;
        gfx.create(960, 600);
        gfx.frame(3);
        gfx.png(L"menu-fixture/resized.png");
        auto *main = window("Briefcase##Framework");
        check(main->Pos.x == 48 && main->Pos.y == 60 && main->Size.x == 864 && main->Size.y == 480,
              "menu keeps 5 percent horizontal and 10 percent vertical margins after resize");
        gfx.click({main->Pos.x + main->Size.x - ImGui::GetStyle().WindowPadding.x - 18,
                   main->Pos.y + ImGui::GetStyle().WindowPadding.y + 18});
        check(!open, "close icon closes menu");
        show_numeric_fixture = true;
        gfx.frame(3);
        auto plus = [](ImVec2 end) { return ImVec2{end.x - 16, end.y - 16}; };
        auto minus = [](ImVec2 end) {
            return ImVec2{end.x - 48 - ImGui::GetStyle().ItemInnerSpacing.x, end.y - 16};
        };
        gfx.click(plus(integer_end));
        check(numeric_integer == 5, "integer plus changed step");
        gfx.click(minus(integer_end));
        check(numeric_integer == 4, "integer minus changed step");
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        gfx.frame(2);
        gfx.click(plus(integer_end));
        check(numeric_integer == 104, "Ctrl integer step changed");
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        gfx.frame(2);
        for (int i = 0; i < 3; ++i)
            gfx.click(minus(decimal_end));
        check(numeric_decimal == 0, "decimal button residual at zero");
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        gfx.frame(2);
        gfx.click(plus(decimal_end));
        check(numeric_decimal == 1, "Ctrl decimal step changed");
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        gfx.frame(2);
        auto repeat_point = plus(integer_end);
        auto before_repeat = numeric_integer;
        io.AddMousePosEvent(repeat_point.x, repeat_point.y);
        gfx.frame(2);
        io.AddMouseButtonEvent(0, true);
        gfx.frame(45);
        io.AddMouseButtonEvent(0, false);
        gfx.frame(2);
        check(numeric_integer > before_repeat + 1, "hold-to-repeat lost");
        numeric_integer = std::numeric_limits<int>::max();
        gfx.frame(2);
        gfx.click(plus(integer_end));
        check(numeric_integer == std::numeric_limits<int>::max(), "integer overflow");
        numeric_disabled = true;
        numeric_integer = 4;
        gfx.frame(2);
        gfx.click(plus(integer_end));
        check(numeric_integer == 4, "disabled numeric control changed");
        numeric_disabled = false;
        gfx.frame(2);
        gfx.click({integer_end.x - 220, integer_end.y - 16});
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        gfx.frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        gfx.frame(2);
        io.AddInputCharactersUTF8("23");
        gfx.frame(3);
        check(numeric_integer == 23, "direct numeric text editing changed");
        gfx.png(L"menu-fixture/numeric-symbols.png");
        show_numeric_fixture = false;
        show_keybind_fixture = true;
        gfx.frame(3);
        auto key_click = ImVec2{binding_start.x + 100, binding_start.y + ImGui::GetFrameHeight() / 2};
        auto clear_click =
            ImVec2{binding_end.x - ImGui::GetFrameHeight() / 2, binding_end.y - ImGui::GetFrameHeight() / 2};
        gfx.click(key_click);
        check(keybind::active() && binding_changes == 0, "activation click assigned mouse");
        io.AddMouseButtonEvent(1, true);
        gfx.frame(2);
        check(!keybind::active() && fixture_binding == VK_RBUTTON, "mouse binding unavailable");
        io.AddMouseButtonEvent(1, false);
        gfx.frame(2);
        gfx.click(key_click);
        io.AddMouseButtonEvent(3, true);
        gfx.frame(2);
        check(fixture_binding == VK_XBUTTON1 && binding_changes == 1, "side mouse key code incorrect");
        io.AddMouseButtonEvent(3, false);
        gfx.frame(2);
        gfx.png(L"menu-fixture/keybind-mouse.png");
        gfx.click(clear_click);
        check(fixture_binding == 0 && binding_changes == 2, "optional binding cannot be cleared");
        gfx.click(key_click);
        io.AddKeyEvent(ImGuiKey_F8, true);
        gfx.frame(2);
        check(fixture_binding == VK_F8 && binding_changes == 3, "keyboard binding in mouse-enabled control");
        io.AddKeyEvent(ImGuiKey_F8, false);
        gfx.frame(2);
        show_keybind_fixture = false;
        fixture_locale["catalogues"] = nlohmann::json::object();
        ++fixture_locale_sequence;
        bc::menu::administration::selected = 0;
        open = true;
        gfx.create();
        gfx.frame(3);
        nav = window("/Navigation_");
        gfx.click({nav->Pos.x + 70, nav->DC.CursorStartPos.y + 3 * (44 + 10) + 22});
        gfx.frame(3);
        check(bc::menu::i18n::state["catalogues"].empty(), "fallback fixture still has translations");
        gfx.png(L"menu-fixture/settings-english-fallback.png");
        ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext();
        std::filesystem::remove(data);
        std::cout << "PASS " << checks << " offscreen menu checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
