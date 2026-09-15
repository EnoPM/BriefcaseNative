#include "ServerAdmin.inc"
// Linux process entry; all mod discovery and public services stay in Host.cpp.
static std::atomic<bool> linux_stopped{};
static std::mutex linux_loading_mutex;
static void BC_CALL linux_shutdown(void*) noexcept {
    if (linux_stopped.exchange(true)) return;
    std::lock_guard lock(linux_loading_mutex);
    if (server_admin) server_admin->request_stop();
    log("Linux shutdown: removing mod callbacks");
    for (auto i = load_order.rbegin(); i != load_order.rend(); ++i) {
        auto& s = *scopes[*i];
        if (s.loaded) {
            auto unload = reinterpret_cast<BcModUnload>(platform::symbol(s.module, "BriefcaseModUnload"));
            try { if (unload) unload(); } catch (...) { log("ERROR: mod unload threw"); }
        }
        try { backend_cleanup_owner(s.id); } catch (...) { log("ERROR: owner cleanup failed"); }
        s.accepting = false;
        s.loaded = false;
    }
    // Libraries remain mapped until process exit, as callbacks can be unwinding.
    log("All Linux mod shutdown callbacks completed");
}
extern "C" BC_EXPORT uint32_t BC_CALL BriefcaseLinuxStart(uint32_t thread) noexcept {
    try {
        const auto exe = platform::executable();
        const auto root = exe.parent_path() / "Briefcase";
        fs::create_directories(root / "Logs");
        logfile.open(root / "Logs/BriefcaseNative.log", std::ios::trunc);
        log("BriefcaseNative " BRIEFCASE_FRAMEWORK_VERSION "; mode=server; platform=linux; no UI, console or Lua");
        if (exe.filename() != "DeceiveIncServer-Linux-Shipping" ||
            !fs::equivalent(fs::current_path(), exe.parent_path()))
            throw std::runtime_error("Expected Linux dedicated executable with Binaries/Linux cwd");
        auto hash = sha256(exe);
        if (hash != "b0b275eac71bb8314b8afb5b36368d882faefafc993d5eac05bb5956a7334ef7")
            throw std::runtime_error("Unsupported Linux executable SHA256");
        build_info = {sizeof(BcBuild), BC_API_VERSION, 0, 0, 4, 27, BRIEFCASE_FRAMEWORK_VERSION, {}};
        std::memcpy(build_info.executable_sha256, hash.c_str(), hash.size() + 1);
        initial_game_thread = thread;
        administration_root = root;
        server_config_root = (exe.parent_path() / "../../Saved/Config/LinuxServer").lexically_normal();
        log("Game SHA256: " + hash);
        game_image = executable_image().base;
        discover_mods(root / "Mods");
        load_phase("startup");
        startup_phase = false;
        backend_set_shutdown_handler(linux_shutdown, nullptr);
        backend_start(thread, 0);
        bootstrap_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start_time).count();
        std::thread([] {
            try {
                for (unsigned i = 0; i < 900 && !backend_ready() && !linux_stopped; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (linux_stopped) return;
                if (!backend_ready()) throw std::runtime_error("Linux backend readiness timeout");
                {
                    std::lock_guard lock(linux_loading_mutex);
                    if (linux_stopped) return;
                    load_phase("ready");
                    start_admin_server();
                }
                log("Linux runtime ready");
                while (!linux_stopped) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (server_admin) server_admin->stop();
            } catch (const std::exception& e) { log(std::string("ERROR Linux host: ") + e.what()); }
              catch (...) { log("ERROR Linux host worker"); }
        }).detach();
        return 0;
    } catch (const std::exception& e) { log(std::string("STARTUP FAILED: ") + e.what()); return 1; }
      catch (...) { log("STARTUP FAILED: Linux host exception"); return 1; }
}
