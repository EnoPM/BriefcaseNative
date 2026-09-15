// Services have process lifetime, but all workers are stopped outside the game/render threads.
static admin::Client *client_admin{};
static admin::Server *server_admin{};
static BcResult BC_CALL admin_select(uint64_t id) noexcept {
    try {
        if (!client_admin)
            return BC_NOT_READY;
        auto entry = server_directory.find(id);
        if (!entry)
            return BC_NOT_FOUND;
        return client_admin->select(id, entry->name, entry->endpoint) ? BC_OK : BC_NOT_READY;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL admin_connect(uint64_t id, const char *endpoint, const char *pin,
                                      const char *password, uint32_t remember) noexcept {
    try {
        if (!client_admin)
            return BC_NOT_READY;
        if (!endpoint || !pin || !password || strnlen_s(endpoint, 256) >= 256 || strnlen_s(pin, 65) != 64 ||
            strnlen_s(password, 257) > 256)
            return BC_INVALID_ARGUMENT;
        auto entry = server_directory.find(id);
        if (!entry)
            return BC_NOT_FOUND;
        return client_admin->connect(id, entry->name, entry->endpoint, endpoint, pin, password, remember != 0)
                   ? BC_OK
                   : BC_INVALID_ARGUMENT;
    } catch (...) {
        return BC_INVALID_ARGUMENT;
    }
}
static BcResult BC_CALL admin_command(const char *operation, const char *payload) noexcept {
    try {
        if (!client_admin)
            return BC_NOT_READY;
        if (!operation || !payload || strnlen_s(operation, 64) >= 64 || strnlen_s(payload, 65537) > 65536)
            return BC_INVALID_ARGUMENT;
        return client_admin->submit(operation, strict_json(payload)) ? BC_OK : BC_NOT_READY;
    } catch (...) {
        return BC_INVALID_ARGUMENT;
    }
}
static void BC_CALL admin_disconnect() noexcept {
    try {
        if (client_admin)
            client_admin->disconnect();
    } catch (...) {
    }
}
static BcResult BC_CALL admin_snapshot(char *buffer, uint32_t capacity, uint64_t *version) noexcept {
    try {
        if (!client_admin)
            return BC_NOT_READY;
        if (!buffer || capacity < 2 || !version)
            return BC_INVALID_ARGUMENT;
        std::string text;
        auto next = *version;
        if (!client_admin->snapshot(text, next)) {
            buffer[0] = 0;
            return BC_OK;
        }
        if (text.size() >= capacity)
            return BC_LIMIT;
        memcpy(buffer, text.c_str(), text.size() + 1);
        *version = next;
        return BC_OK;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static void start_admin_server() noexcept {
    try {
        if (environment != Environment::server)
            return;
        const auto file = administration_root / "Admin" / "server.json";
        if (!fs::exists(file)) {
            log("Administration: disabled; no local Admin/server.json.");
            return;
        }
        auto settings = admin::Settings::load(file);
        auto configs = std::make_shared<admin::ConfigStore>();
        for (const auto &s : scopes) {
            if (s->loaded && admin::ConfigStore::allowed(s->manifest.id) && !s->config_schema.empty())
                configs->add(s->manifest.id, s->manifest.directory / "Data" / "config.json",
                             strict_json(s->config_schema), strict_json(s->config_text));
        }
        std::vector<Manifest> manifests;
        for(auto &s:scopes) manifests.push_back(s->manifest);
        auto management=std::make_shared<admin::Management>(administration_root,std::move(manifests));
        auto status = [] {
            uint32_t loaded = 0;
            for (const auto &s : scopes)
                if (s->loaded)
                    ++loaded;
            return nlohmann::json{
                {"framework", build_info.framework_version},
                {"environment", "server"},
                {"administrationVersion",3},
                {"processId",GetCurrentProcessId()},
                {"gameBuild", std::format("{:08X} / {:08X}", build_info.pe_timestamp, build_info.image_size)},
                {"gameSha256", build_info.executable_sha256},
                {"unreal", "4.27"},
                {"backendState", backend_status()},
                {"uptimeSeconds", std::chrono::duration_cast<std::chrono::seconds>(
                                      std::chrono::steady_clock::now() - start_time)
                                      .count()},
                {"loadedMods", loaded},
                {"discoveredMods", scopes.size()},
                {"bootstrapMs", bootstrap_ms}};
        };
        auto mods = [configs] {
            auto list = nlohmann::json::array();
            for (const auto &s : scopes) {
                const auto &m = s->manifest;
                auto dependencies = nlohmann::json::array();
                for (const auto &d : m.dependencies)
                    dependencies.push_back(
                        {{"id", d.id},
                         {"minimum", std::format("{}.{}.{}", d.minimum[0], d.minimum[1], d.minimum[2])}});
                list.push_back(
                    {{"id", m.id},
                     {"name", m.name},
                     {"author", m.author},
                     {"version", std::format("{}.{}.{}", m.version[0], m.version[1], m.version[2])},
                     {"environment", m.environment},
                     {"state", s->loaded ? BC_UI_LOADED : s->status.load()},
                     {"error", s->error},
                     {"dependencies", dependencies},
                     {"editable", configs->contains(m.id)}});
            }
            return list;
        };
        auto service = std::make_unique<admin::Server>(std::move(settings), configs, status, mods,
                                                       [](std::string_view s) { log(s); },
            [management](const std::string &op,const admin::Json &payload){
                auto result=management->dispatch(op,payload);
                if(op.ends_with(".write")||op=="server.restart")log("Administration: "+op+" accepted.");
                return result;
            },admin::commit_restart);
        service->start();
        server_admin = service.release();
    } catch (const std::exception &e) {
        log(std::string("Administration unavailable: ") + e.what());
    } catch (...) {
        log("Administration unavailable: unexpected initialization error.");
    }
}
static void request_admin_stop() noexcept {
    if (client_admin)
        client_admin->request_stop();
    if (server_admin)
        server_admin->request_stop();
}
static void stop_admin_workers() noexcept {
    if (client_admin)
        client_admin->stop();
    if (server_admin)
        server_admin->stop();
}
