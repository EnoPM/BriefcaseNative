#include <Briefcase/ModApi.h>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "Platform.hpp"

struct MockHost {
    bool game_thread{}, released{};
    BcTask pending{};
    void *user{};
    std::vector<std::string> logs;
};
static BcResult BC_CALL log_message(void *context, uint32_t, const char *text, uint32_t size) noexcept {
    try {
        static_cast<MockHost *>(context)->logs.emplace_back(text, size);
        return BC_OK;
    } catch (...) { return BC_INTERNAL; }
}
static BcResult BC_CALL get_build(void *, BcBuild *out) noexcept {
    if (!out || out->size < sizeof(*out)) return BC_INVALID_ARGUMENT;
    *out = {};
    out->size = sizeof(*out);
    out->api_version = BC_API_VERSION;
    out->engine_major = 4;
    out->engine_minor = 27;
    // Synthetic identity, never evidence of compatibility with a game build.
    std::memset(out->executable_sha256, 'a', 64);
    return BC_OK;
}
static BcResult BC_CALL find(void *context, const char *path, uint32_t size, BcHandle *out) noexcept {
    if (!static_cast<MockHost *>(context)->game_thread) return BC_WRONG_THREAD;
    if (std::string_view(path, size) != "/Script/DeceiveInc.Spy") return BC_NOT_FOUND;
    *out = 42;
    return BC_OK;
}
static BcResult BC_CALL validate(void *context, BcHandle value) noexcept {
    return value == 42 && !static_cast<MockHost *>(context)->released ? BC_OK : BC_STALE_HANDLE;
}
static BcResult BC_CALL release(void *context, BcHandle value) noexcept {
    if (validate(context, value) != BC_OK) return BC_STALE_HANDLE;
    static_cast<MockHost *>(context)->released = true;
    return BC_OK;
}
static BcResult BC_CALL post(void *context, BcTask task, void *user) noexcept {
    auto &host = *static_cast<MockHost *>(context);
    host.pending = task;
    host.user = user;
    return BC_OK;
}
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    const auto path = std::filesystem::absolute(argv[1]);
    bool relative_rejected = false;
    try { bc::platform::open_library("relative.so"); } catch (...) { relative_rejected = true; }
    if (!relative_rejected || bc::platform::thread_id() == 0 || bc::platform::process_id() == 0 ||
        !bc::platform::executable().is_absolute()) return 1;
    auto library = bc::platform::open_library(path);
    auto load = reinterpret_cast<BcModLoad>(bc::platform::symbol(library, "BriefcaseModLoad"));
    if (bc::platform::symbol(library, "BriefcaseMissingExport") != nullptr) return 1;
    if (!load) { std::cerr << "Unable to load sample export\n"; return 1; }
    MockHost host;
    BcApi api{sizeof(BcApi), BC_API_VERSION, BC_CAP_LOG | BC_CAP_FIND | BC_CAP_SCHEDULE,
              &host, log_message, get_build, find, validate, release, post, nullptr};
    bool ok = load(nullptr) == BC_VERSION_MISMATCH;
    api.version++;
    ok = ok && load(&api) == BC_VERSION_MISMATCH;
    api.version = BC_API_VERSION;
    ok = ok && load(&api) == BC_OK && host.pending && host.logs.size() == 2;
    if (ok) {
        host.game_thread = true;
        host.pending(host.user);
        host.pending = nullptr; // Never keep a callback after unloading its module.
        ok = host.released && host.logs.size() == 4;
        for (const auto &line : host.logs) ok = ok && line.find("ERROR") == std::string::npos;
    }
    bc::platform::close_library(library);
    if (!ok) { std::cerr << "Sample ABI/lifecycle contract failed\n"; return 1; }
    std::cout << "PASS sample dynamic loading, ABI rejection, queued callback and module unload (mock host)\n";
}
