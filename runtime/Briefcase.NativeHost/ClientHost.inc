
static fs::path client_metrics_path;
static std::atomic<bool> client_startup_ready{};
static ClientStartupSignal startup_signal;
static std::vector<fs::path> startup_logs;
static std::vector<uint64_t> startup_offsets;
static FILETIME process_created;
static void poll_client_startup() noexcept {
    if (client_startup_ready)
        return;
    try {
        for (size_t i = 0; i < startup_logs.size(); ++i) {
            WIN32_FILE_ATTRIBUTE_DATA attributes{};
            if (!GetFileAttributesExW(startup_logs[i].c_str(), GetFileExInfoStandard, &attributes) ||
                CompareFileTime(&attributes.ftLastWriteTime, &process_created) < 0)
                continue;
            const uint64_t length = (uint64_t(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
            auto &offset = startup_offsets[i];
            if (length < offset)
                offset = 0;
            if (length == offset)
                continue;
            std::ifstream stream(startup_logs[i], std::ios::binary);
            if (!stream)
                continue;
            stream.seekg(offset);
            std::array<char, 65536> bytes{};
            stream.read(bytes.data(), bytes.size());
            const auto count = stream.gcount();
            if (count <= 0)
                continue;
            offset += uint64_t(count);
            if (startup_signal.feed(std::string_view(bytes.data(), size_t(count)))) {
                client_startup_ready = true;
                log("Client startup gate released: shader precompilation completed / login menu reached.");
                return;
            }
        }
    } catch (...) {
    }
}
static uint32_t BC_CALL client_can_initialize() noexcept {
    return client_startup_ready;
}

static void BC_CALL client_log(const char *message) noexcept {
    try {
        if (message)
            log(std::string("Client: ") + message);
    } catch (...) {
    }
}
static BcResult BC_CALL client_home(BcClientHome *out) noexcept {
    if (!out || out->size < sizeof(*out))
        return BC_INVALID_ARGUMENT;
    *out = {};
    out->size = sizeof(*out);
    out->build = build_info;
    out->bootstrap_ms = bootstrap_ms;
    out->unreal_state = backend_status();
    out->unreal_ms = backend_initialization_ms();
    out->discovered = uint32_t(scopes.size());
    for (const auto &s : scopes)
        if (s->loaded.load())
            ++out->loaded;
    return BC_OK;
}
template <size_t N> static void text_field(char (&out)[N], const std::string &in) {
    std::memcpy(out, in.data(), std::min(N - 1, in.size()));
    out[std::min(N - 1, in.size())] = 0;
}
static BcResult BC_CALL client_mod(uint32_t index, BcClientModRow *out) noexcept {
    if (!out || out->size < sizeof(*out))
        return BC_INVALID_ARGUMENT;
    if (index >= scopes.size())
        return BC_NOT_FOUND;
    try {
        const auto &s = *scopes[index];
        const auto &m = s.manifest;
        *out = {};
        out->size = sizeof(*out);
        out->state = s.loaded ? BC_UI_LOADED : s.status.load();
        text_field(out->id, m.id);
        text_field(out->name, m.name);
        text_field(out->author, m.author);
        text_field(out->version, std::format("{}.{}.{}", m.version[0], m.version[1], m.version[2]));
        text_field(out->environment, m.environment);
        if (out->state == BC_UI_ERROR || out->state == BC_UI_DISABLED)
            text_field(out->error, s.error);
        std::string deps;
        for (const auto &d : m.dependencies) {
            if (!deps.empty())
                deps += ", ";
            deps += std::format("{} >= {}.{}.{}", d.id, d.minimum[0], d.minimum[1], d.minimum[2]);
        }
        text_field(out->dependencies, deps);
        return BC_OK;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static void BC_CALL request_client_unreal() noexcept {
    client_unreal_requested = true;
}
static void BC_CALL request_client_shutdown() noexcept {
    client_shutdown_requested = true;
}
#include "ClientServers.inc"

// Admin services depend on the server directory declared above.
#include "AdminHost.inc"
static locale::Client *client_locale{};
static BcResult BC_CALL locale_snapshot(char *buffer, uint32_t capacity, uint64_t *version) noexcept {
    try {
        if (!client_locale || !buffer || !version || capacity < 2)
            return BC_INVALID_ARGUMENT;
        std::string value;
        auto next = *version;
        if (!client_locale->snapshot(value, next)) {
            buffer[0] = 0;
            return BC_OK;
        }
        if (value.size() >= capacity)
            return BC_LIMIT;
        memcpy(buffer, value.c_str(), value.size() + 1);
        *version = next;
        return BC_OK;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL locale_select(const char *language) noexcept {
    try {
        if (!client_locale || !language || strnlen_s(language, 32) >= 32)
            return BC_INVALID_ARGUMENT;
        return client_locale->select(language) ? BC_OK : BC_INVALID_ARGUMENT;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static uint32_t BC_CALL client_menu_key() noexcept {
    return client_locale ? client_locale->menu_key() : 0x70;
}
static BcResult BC_CALL client_set_menu_key(uint32_t key) noexcept {
    try {
        return client_locale && client_locale->select_menu_key(key) ? BC_OK : BC_INVALID_ARGUMENT;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static Scope *client_mod_scope(const char *id) {
    if (!id || strnlen_s(id, 129) > 128)
        return nullptr;
    for (auto &s : scopes)
        if (s->manifest.id == id && s->loaded && s->accepting)
            return s.get();
    return nullptr;
}
static BcResult BC_CALL mod_settings(const char *id, char *buffer, uint32_t capacity) noexcept {
    try {
        auto *s = client_mod_scope(id);
        if (!s)
            return BC_NOT_FOUND;
        if (!buffer || capacity < 2)
            return BC_INVALID_ARGUMENT;
        auto value = s->client_settings.snapshot();
        if (value.is_null())
            return BC_NOT_FOUND;
        value["modId"] = s->manifest.id;
        auto text = value.dump();
        if (text.size() >= capacity)
            return BC_LIMIT;
        memcpy(buffer, text.c_str(), text.size() + 1);
        return BC_OK;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL save_mod_settings(const char *id, uint64_t revision, const char *values) noexcept {
    try {
        auto *s = client_mod_scope(id);
        if (!s)
            return BC_NOT_FOUND;
        if (!values || strnlen_s(values, 65537) > 65536)
            return BC_INVALID_ARGUMENT;
        return s->client_settings.request(revision, strict_json(values)) ? BC_OK : BC_INVALID_ARGUMENT;
    } catch (...) {
        return BC_INVALID_ARGUMENT;
    }
}
static void start_client_module(const fs::path &root) {
    client_admin = new admin::Client(root);
    std::vector<Manifest> locale_mods;
    for (auto &scope : scopes)
        locale_mods.push_back(scope->manifest);
    client_locale = new locale::Client(root, std::move(locale_mods));
    client_metrics_path = root / "Logs" / "client-metrics.json";
    server_directory.load(root / "servers.json");
    if (auto message = server_directory.snapshot().message; !message.empty())
        log(message);
    startup_logs.push_back(root / "Logs" / "DeceiveInc-client.log");
    wchar_t appdata[32768]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, 32768))
        startup_logs.push_back(fs::path(appdata) / "DeceiveInc" / "Saved" / "Logs" / "DeceiveInc.log");
    startup_offsets.resize(startup_logs.size());
    FILETIME exited{}, kernel{}, user{};
    GetProcessTimes(GetCurrentProcess(), &process_created, &exited, &kernel, &user);
    const auto path = root / "Core" / "Client" / "Briefcase.Client.Rendering.dll";
    no_reparse(path, root);
    auto library = LoadLibraryExW(path.c_str(), nullptr,
                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library)
        throw std::runtime_error(std::format("Client rendering module load failed: {}", GetLastError()));
    auto get = reinterpret_cast<BcGetClientModuleApi>(GetProcAddress(library, "BriefcaseGetClientModuleApi"));
    client_module = get ? get() : nullptr;
    if (!client_module || client_module->version != 1 || client_module->size < sizeof(BcClientModuleApi))
        throw std::runtime_error("Client module ABI unavailable");
    static const BcClientHostApi services{sizeof(BcClientHostApi),
                                          7,
                                          client_log,
                                          client_home,
                                          client_mod,
                                          request_client_unreal,
                                          request_client_shutdown,
                                          client_can_initialize,
                                          client_servers,
                                          client_add_server,
                                          client_remove_server,
                                          client_join_server,
                                          admin_select,
                                          admin_connect,
                                          admin_command,
                                          admin_disconnect,
                                          admin_snapshot,
                                          locale_snapshot,
                                          locale_select,
                                          mod_settings,
                                          save_mod_settings,
                                          client_menu_key,
                                          client_set_menu_key};
    if (client_module->start(&services) != BC_OK)
        throw std::runtime_error("Client module startup failed");
    log("Client rendering discovery started; ImGui and Unreal initialization deferred until first menu "
        "opening.");
}

static void write_client_metrics() noexcept {
    try {
        if (!client_module)
            return;
        BcClientMetrics m{sizeof(m)};
        if (client_module->metrics(&m) != BC_OK)
            return;
        nlohmann::json j{{"backend", "Direct3D 11"},
                         {"processId", GetCurrentProcessId()},
                         {"startup_ready", client_startup_ready.load()},
                         {"bootstrap_ms", bootstrap_ms},
                         {"unreal_ms", backend_initialization_ms()},
                         {"unreal_state", backend_status()},
                         {"graphics_state", m.graphics_state},
                         {"context_created", m.context_created},
                         {"menu_open", m.menu_open},
                         {"hook_ms", m.hook_ms},
                         {"context_ms", m.context_ms},
                         {"fonts_ms", m.fonts_ms},
                         {"resources_ms", m.resources_ms},
                         {"dormant_average_us", m.dormant_average_us},
                         {"closed_average_us", m.closed_average_us},
                         {"open_average_us", m.open_average_us},
                         {"dormant_frames", m.dormant_frames},
                         {"closed_frames", m.closed_frames},
                         {"open_frames", m.open_frames},
                         {"width", m.width},
                         {"height", m.height},
                         {"callbacks", m.callback_count},
                         {"device_errors", m.device_errors}};
        std::ofstream(client_metrics_path) << j.dump(2);
        log("Client metrics snapshot: " + j.dump());
    } catch (...) {
        log("Client metrics snapshot failed");
    }
}
