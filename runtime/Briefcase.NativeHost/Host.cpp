#include "../Briefcase.Client.Rendering/ClientBridge.h"
#include "Backend.hpp"
#include "../Briefcase.Client.Admin/Client.hpp"
#include "../Briefcase.Admin/Management.hpp"
#include "../Briefcase.Localization/Client.hpp"
#include "ClientStartup.hpp"
#include "../Briefcase.Client.Servers/ServerDirectory.hpp"
#include "Configuration.hpp"
#include "../Briefcase.Client.Settings/Store.hpp"
#include "GameProfile.hpp"
#include "Manifest.hpp"
#include "Startup.hpp"
#include "UnrealServices.hpp"
#include <Windows.h>
#include <atomic>
#include <bcrypt.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
namespace bc {
namespace fs = std::filesystem;
static std::mutex log_mutex;
static std::ofstream logfile;
static const auto start_time = std::chrono::steady_clock::now();
static BcBuild build_info{};
static Environment environment = Environment::server;
static fs::path administration_root;
struct Scope {
    uint64_t id;
    Manifest manifest;
    BcApi api{};
    HMODULE module{};
    std::atomic<bool> loaded{};
    bool eligible{};
    std::atomic<uint32_t> status{BC_UI_PENDING};
    std::string error;
    std::atomic<bool> accepting{true};
    std::string config_schema, config_text;
    client_settings::Store client_settings;
    std::vector<FileChange> files;
    std::vector<ImmediateChange> patches;
};
static std::vector<std::unique_ptr<Scope>> scopes;
void log(std::string_view message) noexcept {
    try {
        std::lock_guard lock(log_mutex);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                        start_time)
                      .count();
        logfile << "[" << ms << " ms] " << message << "\n";
        logfile.flush();
    } catch (...) {
    }
}
static Scope *scope(void *context, uint64_t capability = 0) {
    // Scopes are constructed before loading mods and never removed or moved.
    for (auto &s : scopes)
        if (s.get() == context && s->accepting && (s->manifest.capabilities & capability) == capability)
            return s.get();
    return nullptr;
}
static BcResult BC_CALL api_log(void *c, uint32_t level, const char *data, uint32_t size) noexcept {
    try {
        auto *s = scope(c, BC_CAP_LOG);
        if (!s)
            return BC_DENIED;
        if (!data || size > 16384 || level > 3)
            return BC_INVALID_ARGUMENT;
        log(s->manifest.id + ": " + std::string(data, size));
        return BC_OK;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL api_build(void *c, BcBuild *out) noexcept {
    if (!scope(c))
        return BC_DENIED;
    if (!out || out->size < sizeof(BcBuild))
        return BC_INVALID_ARGUMENT;
    *out = build_info;
    return BC_OK;
}
static BcResult BC_CALL api_find(void *c, const char *path, uint32_t size, BcHandle *out) noexcept {
    try {
        auto *s = scope(c, BC_CAP_FIND);
        if (!s)
            return BC_DENIED;
        if (!path || !out || !size || size > 4096)
            return BC_INVALID_ARGUMENT;
        *out = 0;
        return backend_find(s->id, std::string_view(path, size), out);
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL api_validate(void *c, BcHandle handle) noexcept {
    try {
        auto *s = scope(c);
        return s ? backend_validate(s->id, handle) : BC_DENIED;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL api_release(void *c, BcHandle handle) noexcept {
    try {
        auto *s = scope(c);
        return s ? backend_release(s->id, handle) : BC_DENIED;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL api_post(void *c, BcTask task, void *user) noexcept {
    try {
        auto *s = scope(c, BC_CAP_SCHEDULE);
        return s ? backend_post(task, user, s->id) : BC_DENIED;
    } catch (...) {
        return BC_INTERNAL;
    }
}
#include "ClientServices.inc"
#include "Services.inc"
static std::string sha256(const fs::path &path) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA256 provider unavailable");
    struct Cleanup {
        BCRYPT_ALG_HANDLE a;
        BCRYPT_HASH_HANDLE *h;
        ~Cleanup() {
            if (*h)
                BCryptDestroyHash(*h);
            BCryptCloseAlgorithmProvider(a, 0);
        }
    } cleanup{algorithm, &hash};
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        throw std::runtime_error("SHA256 init failed");
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Cannot open game executable");
    std::array<char, 65536> chunk{};
    while (stream) {
        stream.read(chunk.data(), chunk.size());
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(chunk.data()), static_cast<ULONG>(stream.gcount()),
                           0) < 0)
            throw std::runtime_error("SHA256 update failed");
    }
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(hash, digest.data(), 32, 0) < 0)
        throw std::runtime_error("SHA256 final failed");
    std::string result;
    for (auto b : digest)
        result += std::format("{:02x}", b);
    return result;
}
static void no_reparse(const fs::path &path, const fs::path &root) {
    for (auto p = path; p != root && !p.empty(); p = p.parent_path()) {
        auto a = GetFileAttributesW(p.c_str());
        if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Missing file or reparse point: " + p.string());
    }
}
static std::vector<size_t> load_order;
static void discover_mods(const fs::path &root) {
    std::set<std::string> enabled;
    const auto settings =
        root.parent_path() / (environment == Environment::client ? "loader.json" : "settings.json");
    const bool explicit_selection = fs::exists(settings);
    if (explicit_selection) {
        auto json = strict_json(read_bounded(settings, 65536));
        if (!json.at("enabledMods").is_array())
            throw std::runtime_error("enabledMods must be an array");
        for (const auto &id : json.at("enabledMods"))
            if (!enabled.insert(id.get<std::string>()).second)
                throw std::runtime_error("Duplicate enabled mod");
    }
    fs::create_directories(root);
    std::vector<Manifest> active;
    std::vector<size_t> active_indices;
    for (const auto &d : fs::directory_iterator(root)) {
        if (!d.is_directory())
            continue;
        if (scopes.size() >= 128)
            throw std::runtime_error("Maximum 128 mods");
        no_reparse(d.path(), root);
        const auto file = d.path() / "briefcase.mod.json";
        if (!fs::exists(file))
            continue;
        auto s = std::make_unique<Scope>();
        s->id = scopes.size() + 1;
        try {
            no_reparse(file, root);
            s->manifest = parse_manifest(read_bounded(file, 65536));
            auto &m = s->manifest;
            if (m.id != d.path().filename().string())
                throw std::runtime_error("Mod folder must match manifest id");
            m.directory = d.path();
            const bool selected = !explicit_selection || enabled.contains(m.id);
            enabled.erase(m.id);
            if (environment == Environment::client && m.phase == "startup") {
                s->error = "Before-entry startup phase is reserved to the server";
                s->status = BC_UI_DISABLED;
            } else if (!matches_environment(m.environment, environment)) {
                s->error = "Package environment does not match this process";
                s->status = BC_UI_DISABLED;
                log("Skipped " + m.environment + " mod " + m.id);
            } else if (!selected) {
                s->error = "Disabled in loader settings";
                s->status = BC_UI_DISABLED;
                log("Disabled mod " + m.id);
            } else {
                s->eligible = true;
                active_indices.push_back(scopes.size());
                active.push_back(m);
            }
            log("Discovered mod " + m.id + " (" + m.environment + ")");
        } catch (const std::exception &e) {
            s->manifest.id = d.path().filename().string();
            s->manifest.name = s->manifest.id;
            s->error = e.what();
            s->status = BC_UI_ERROR;
            log("Mod discovery error " + s->manifest.id + ": " + s->error);
            if (environment == Environment::server)
                throw;
        }
        s->api = {sizeof(BcApi), BC_API_VERSION, s->manifest.capabilities,
                  s.get(),       api_log,        api_build,
                  api_find,      api_validate,   api_release,
                  api_post,      api_service};
        scopes.push_back(std::move(s));
    }
    if (explicit_selection && !enabled.empty())
        throw std::runtime_error("Enabled mod package missing");
    try {
        for (auto index : dependency_order(active))
            load_order.push_back(active_indices[index]);
    } catch (const std::exception &e) {
        if (environment == Environment::server)
            throw;
        for (auto index : active_indices) {
            scopes[index]->error = e.what();
            scopes[index]->status = BC_UI_ERROR;
            scopes[index]->eligible = false;
        }
        log(std::string("Client dependency plan rejected: ") + e.what());
    }
}

static void load_phase(std::string_view phase, bool rendering_only = false) {
    std::set<std::string> loaded;
    for (const auto &s : scopes)
        if (s->loaded)
            loaded.insert(s->manifest.id);
    for (auto i : load_order) {
        auto &s = *scopes[i];
        if (!s.eligible || s.loaded || s.status == BC_UI_ERROR || s.manifest.phase != phase)
            continue;
        constexpr auto unreal_caps = BC_CAP_FIND | BC_CAP_SCHEDULE | BC_CAP_REFLECTION | BC_CAP_INVOCATION |
                                     BC_CAP_HOOKS | BC_CAP_LIFECYCLE | BC_CAP_NATIVE_HOOKS |
                                     BC_CAP_UNREAL_WRITE;
        if (rendering_only && (s.manifest.capabilities & unreal_caps))
            continue;
        bool dependencies_ready = true;
        for (const auto &d : s.manifest.dependencies)
            if (!loaded.contains(d.id))
                dependencies_ready = false;
        if (!dependencies_ready) {
            if (rendering_only)
                continue;
            s.error = "Dependency failed to load";
            s.status = BC_UI_ERROR;
            log("Skipped " + s.manifest.id + ": dependency failed");
            continue;
        }
        auto dll = s.manifest.directory / s.manifest.entry;
        assert_plain_path(dll);
        s.module = LoadLibraryExW(dll.c_str(), nullptr,
                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!s.module) {
            s.error = std::format("LoadLibrary error {}", GetLastError());
            s.status = BC_UI_ERROR;
            log(std::format("Mod {} LoadLibrary error {}", s.manifest.id, GetLastError()));
            continue;
        }
        auto entry = reinterpret_cast<BcModLoad>(GetProcAddress(s.module, "BriefcaseModLoad"));
        loading_scope = &s;
        loading_thread.store(GetCurrentThreadId(), std::memory_order_release);
        BcResult result = BC_VERSION_MISMATCH;
        try {
            if (entry)
                result = entry(&s.api);
        } catch (...) {
            result = BC_INTERNAL;
        }
        loading_thread.store(0, std::memory_order_release);
        loading_scope = nullptr;
        if (result != BC_OK) {
            s.error = std::format("Mod entry returned {}", result);
            s.status = BC_UI_ERROR;
            if (client_module)
                client_module->cleanup_owner(s.id);
            log(std::format("Mod {} rejected: {}", s.manifest.id, result));
            continue;
        }
        if (phase == "startup") {
            commit_startup(game_image, s.patches, s.files);
            log(std::format("{}: startup transaction committed before EXE entry ({} immediates)",
                            s.manifest.id, s.patches.size()));
            for (const auto &f : s.files)
                if (f.before != f.after)
                    log(s.manifest.id + ": server INI synchronized atomically; previous file backed up");
        }
        s.loaded = true;
        loaded.insert(s.manifest.id);
        log("Mod loaded: " + s.manifest.id);
    }
    for (const auto &s : scopes)
        if (s->eligible && s->manifest.phase == "startup" && !s->loaded)
            throw std::runtime_error("Required startup mod failed; refusing to start game");
    log(std::format("Mod phase {} complete: {}/{} loaded", phase, loaded.size(), scopes.size()));
}
#include "ClientHost.inc"
#include "Shutdown.inc"
} // namespace bc
extern "C" __declspec(dllexport) uint32_t __cdecl BriefcasePrepare(uint32_t game_thread) noexcept {
    try {
        wchar_t path[32768]{};
        auto length = GetModuleFileNameW(nullptr, path, 32768);
        if (!length || length == 32768)
            return 1;
        const std::filesystem::path exe(path), root = exe.parent_path() / "Briefcase";
        const bool client = exe.filename() == L"DeceiveInc-Win64-Shipping.exe";
        bc::environment = client ? bc::Environment::client : bc::Environment::server;
        bc::initial_game_thread = game_thread;
        bc::administration_root = root;
        std::filesystem::create_directories(root / "Logs");
        bc::logfile.open(client ? root / "Briefcase.log" : root / "Logs" / "BriefcaseNative.log",
                         std::ios::out | std::ios::trunc);
        bc::log(
            client ? "BriefcaseNative 0.3.0; mode=client; asynchronous bootstrap; no UE4SS UI/console/Lua"
                   : "BriefcaseNative 0.3.0; mode=server; preparing before EXE entry; no UI, console or Lua");
        const auto base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
        const auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
        const auto nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
        const auto *profile = bc::game_profile(exe.filename().wstring(), nt->FileHeader.TimeDateStamp,
                                               nt->OptionalHeader.SizeOfImage);
        if (!profile)
            throw std::runtime_error("Unsupported game build; backend not installed");
        if (std::filesystem::current_path() != exe.parent_path())
            throw std::runtime_error("Working directory must equal executable Win64 directory");
        bc::build_info = {sizeof(BcBuild),
                          BC_API_VERSION,
                          nt->FileHeader.TimeDateStamp,
                          nt->OptionalHeader.SizeOfImage,
                          4,
                          27,
                          "0.3.0",
                          {}};
        auto hash = bc::sha256(exe);
        strcpy_s(bc::build_info.executable_sha256, hash.c_str());
        bc::log("Game SHA256: " + hash);
        if (hash != profile->sha256)
            throw std::runtime_error("Unsupported executable SHA256; backend not installed");
        bc::log("Working directory: " + exe.parent_path().string());
        bc::game_image = base;
        bc::server_config_root = (exe.parent_path() / "../../Saved/Config/WindowsServer").lexically_normal();
        bc::discover_mods(root / "Mods");
        bc::load_phase("startup");
        bc::startup_phase = false;
        bc::bootstrap_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bc::start_time)
                .count();
        if (client)
            bc::log(
                std::format("Client bootstrap completed in {:.3f} ms; Unreal deferred.", bc::bootstrap_ms));
        else {
            bc::backend_start(game_thread);
            bc::log("Preparation complete; entering original EXE entry");
        }
        return 0;
    } catch (const std::exception &e) {
        bc::log(std::string("STARTUP FAILED: ") + e.what());
        return 1;
    } catch (...) {
        bc::log("STARTUP FAILED: unexpected exception");
        return 1;
    }
}
extern "C" __declspec(dllexport) uint32_t __cdecl BriefcaseRun() noexcept {
    try {
        if (bc::environment == bc::Environment::client) {
            wchar_t path[32768]{};
            GetModuleFileNameW(nullptr, path, 32768);
            bc::start_client_module(std::filesystem::path(path).parent_path() / "Briefcase");
            bc::load_phase("ready", true);
            bc::run_control_plane();
            return 0;
        }
        for (unsigned i = 0; i < 900 && !bc::backend_ready(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!bc::backend_ready())
            throw std::runtime_error("Backend did not become ready within 90 seconds");
        bc::load_phase("ready");
        bc::start_admin_server();
        bc::run_control_plane();
        return 0;
    } catch (const std::exception &e) {
        bc::log(std::string("ERROR: ") + e.what());
        return 1;
    } catch (...) {
        bc::log("ERROR: unexpected host exception");
        return 1;
    }
}

extern "C" __declspec(dllexport) void __cdecl BriefcaseRecordBootstrap(double proxy_us,
                                                                       double host_ms) noexcept {
    if (bc::environment != bc::Environment::client)
        return;
    try {
        bc::bootstrap_ms = host_ms;
        bc::log(
            std::format("Client bootstrap total: {:.3f} ms on worker; proxy loader-lock forwarding {:.3f} us",
                        host_ms, proxy_us));
    } catch (...) {
    }
}
