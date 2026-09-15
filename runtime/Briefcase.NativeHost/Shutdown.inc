// Host worker control plane. The game thread only executes bounded teardown.
static HANDLE shutdown_ack{};
static std::atomic<bool> shutdown_completed{};
static void BC_CALL shutdown_mods(void *) noexcept {
    if (shutdown_completed.exchange(true))
        return;
    request_admin_stop();
    log("Process shutdown requested: removing mod callbacks");
    for (auto i = load_order.rbegin(); i != load_order.rend(); ++i) {
        auto &s = *scopes[*i];
        if (client_module)
            client_module->cleanup_owner(s.id);
        if (s.loaded && s.module) {
            auto unload = reinterpret_cast<BcModUnload>(GetProcAddress(s.module, "BriefcaseModUnload"));
            if (unload)
                try {
                    unload();
                } catch (...) {
                    log("ERROR: mod unload threw: " + s.manifest.id);
                }
        }
        try {
            backend_cleanup_owner(s.id);
        } catch (...) {
            log("ERROR: owner cleanup failed: " + s.manifest.id);
        }
        s.accepting = false;
        s.loaded = false;
    }
    log("All mod shutdown callbacks completed");
    if (shutdown_ack)
        SetEvent(shutdown_ack);
}
static void run_control_plane() {
    const auto suffix = std::to_wstring(GetCurrentProcessId());
    HANDLE request = CreateEventW(nullptr, TRUE, FALSE, (L"Local\\BriefcaseNative.Stop." + suffix).c_str());
    shutdown_ack = CreateEventW(nullptr, TRUE, FALSE, (L"Local\\BriefcaseNative.Stopped." + suffix).c_str());
    if (!request || !shutdown_ack)
        throw std::runtime_error("Cannot create shutdown control events");
    backend_set_shutdown_handler(shutdown_mods, nullptr);
    log("Shutdown control ready");
    if (environment == Environment::client) {
        HANDLE metrics_request =
            CreateEventW(nullptr, FALSE, FALSE, (L"Local\\BriefcaseNative.Metrics." + suffix).c_str());
        bool unreal_started = false, late_mods = false;
        while (WaitForSingleObject(request, 100) == WAIT_TIMEOUT && !client_shutdown_requested && !shutdown_completed) {
            poll_client_startup();
            for(auto &s:scopes) if(s->loaded) s->client_settings.process();
            if (auto message = server_directory.process_one(); !message.empty()) log("Client servers: " + message);
            if (metrics_request && WaitForSingleObject(metrics_request, 0) == WAIT_OBJECT_0)
                write_client_metrics();
            if (client_unreal_requested && !unreal_started) {
                backend_start(initial_game_thread, 0);
                unreal_started = true;
            }
            if (backend_ready() && !late_mods) {
                load_phase("ready");
                late_mods = true;
            }
        }
        stop_admin_workers();
        if(client_locale)client_locale->stop();
        write_client_metrics();
        if (metrics_request)
            CloseHandle(metrics_request);
        if (client_module)
            client_module->stop();
        if (auto message = server_directory.process_one(); !message.empty())
            log("Client servers: " + message);
        if (!backend_ready())
            shutdown_mods(nullptr);
        else if (backend_post(shutdown_mods, nullptr) != BC_OK)
            log("ERROR: cannot queue client mod shutdown");
    } else {
        while(WaitForSingleObject(request,100)==WAIT_TIMEOUT && !shutdown_completed) {}
        stop_admin_workers();
        if(!shutdown_completed && backend_post(shutdown_mods,nullptr)!=BC_OK)
            log("ERROR: cannot queue mod shutdown");
    }
    // Keep the acknowledgment event alive until process termination.
}
