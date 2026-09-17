// Common dedicated-server administration bridge.
static admin::Server *server_admin{};
static void start_admin_server() noexcept {
    try {
        if (environment != Environment::server)
            return;
        auto provisioned = admin::load_or_provision_local(administration_root);
        if (provisioned.created)
            log("Administration: generated local identity and password in Briefcase/Admin/server.json; "
                "listening on 127.0.0.1:32189.");
        auto settings = std::move(provisioned.settings);
        auto configs = std::make_shared<admin::ConfigStore>();
        for (const auto &s : scopes) {
            if (s->loaded && admin::ConfigStore::allowed(s->manifest.id) && !s->config_schema.empty())
                configs->add(s->manifest.id, s->manifest.directory / "Data" / "config.json",
                             strict_json(s->config_schema), strict_json(s->config_text));
        }
        std::vector<Manifest> manifests;
        for(auto &s:scopes) manifests.push_back(s->manifest);
        auto management=std::make_shared<admin::Management>(administration_root,std::move(manifests));
        auto status = [management] {
            uint32_t loaded = 0;
            for (const auto &s : scopes)
                if (s->loaded)
                    ++loaded;
            auto result = nlohmann::json{
                {"framework", build_info.framework_version},
                {"environment", "server"},
                {"administrationVersion",4},
                {"processId",platform::process_id()},
                #ifdef _WIN32
                {"gameBuild", std::format("{:08X} / {:08X}", build_info.pe_timestamp, build_info.image_size)},
#else
                {"gameBuild", "Linux ELF"},
#endif
                {"gameSha256", build_info.executable_sha256},
                {"unreal", "4.27"},
                {"backendState", backend_status()},
                {"uptimeSeconds", std::chrono::duration_cast<std::chrono::seconds>(
                                      std::chrono::steady_clock::now() - start_time)
                                      .count()},
                {"loadedMods", loaded},
                {"discoveredMods", scopes.size()},
                {"bootstrapMs", bootstrap_ms}};
            result["session"] = management->session_status();
            return result;
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
                if(op.ends_with(".write")||op=="server.restart"||op=="server.shutdown")
                    log("Administration: "+op+" accepted.");
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
