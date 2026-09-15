// Linux initializes reflection on a worker while the engine constructs objects.
// Only the engine-tick callback may publish readiness and execute mod work.
void backend_start(uint32_t thread, uint32_t) {
    game_thread_id = thread;
    if (init_state.exchange(1) != 0) throw std::runtime_error("Backend already started");
    std::thread([] {
        const auto start = std::chrono::steady_clock::now();
        try {
            RC::Output::DefaultTargets::get_default_devices_ref().push_back(std::make_unique<LogDevice>());
            UnrealInitializer::Config config{};
            config.SecondsToScanBeforeGivingUp = 10;
            config.bUseUObjectArrayCache = false;
            config.bEnableCache = false;
            config.bHookEngineTick = true;
            config.bHookUObjectProcessEvent = true;
            UnrealInitializer::Initialize(config);
            if (Version::Major != 4 || Version::Minor != 27)
                throw std::runtime_error("Unsupported Unreal version");
            Hook::RegisterProcessEventPreCallback([](auto&, UObject* self, UFunction* fn, void* params) {
                reflected_event_pre(self, fn, params);
            }, {false, false, STR("Briefcase"), STR("ProcessEventPre")});
            Hook::RegisterProcessEventPostCallback([](auto&, UObject* self, UFunction* fn, void* params) {
                reflected_event_post(self, fn, params);
            }, {false, false, STR("Briefcase"), STR("ProcessEventPost")});
            Hook::RegisterEngineTickPreCallback([start, initialized = false](auto&, UEngine*, float, bool) mutable {
                if (platform::thread_id() != game_thread_id) return;
                if (!ready.load(std::memory_order_acquire)) {
                    if (initialized) return; // Never reattach after UObject array shutdown.
                    initialized = true;
                    UObjectArray::AddUObjectDeleteListener(&delete_listener);
                    measured_initialization_ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start).count();
                    ready.store(true, std::memory_order_release);
                    init_state = 2;
                    log("Linux Unreal backend ready on game thread");
                }
                pump();
            }, {false, false, STR("Briefcase"), STR("GameThreadPump")});
        } catch (const std::exception& e) {
            log(std::string("ERROR Linux backend: ") + e.what());
            init_state = 3;
        } catch (...) { log("ERROR Linux backend: initialization failed"); init_state = 3; }
    }).detach();
}
