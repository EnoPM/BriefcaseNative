#include <Briefcase/ModApi.hpp>
static const BcApi *api;

static void BC_CALL on_game_thread(void *) noexcept {
    briefcase::Api host(api);
    BcHandle object{};
    auto result = host.find("/Script/DeceiveInc.Spy", object);
    if (result == BC_OK && host.validate(object) == BC_OK) {
        host.log("NativeHello: game-thread callback; Spy class found; handle valid");
        host.release(object);
        host.log(host.validate(object) == BC_STALE_HANDLE ? "NativeHello: released handle rejected"
                                                          : "NativeHello: ERROR released handle still valid");
    } else {
        host.log("NativeHello: ERROR Spy class lookup/validation failed");
    }
}

extern "C" BC_EXPORT BcResult BC_CALL BriefcaseModLoad(const BcApi *supplied) noexcept {
    if (!supplied || supplied->size < sizeof(BcApi) || supplied->version != BC_API_VERSION)
        return BC_VERSION_MISMATCH;
    BcBuild build{};
    if (supplied->get_build(supplied->context, &build) != BC_INVALID_ARGUMENT)
        return BC_INTERNAL;
    build.size = sizeof(build);
    if (supplied->get_build(supplied->context, &build) != BC_OK || build.api_version != BC_API_VERSION ||
        build.engine_major != 4 || build.engine_minor != 27 || !build.executable_sha256[0])
        return BC_INTERNAL;
    api = supplied;
    briefcase::Api host(api);
    host.log("NativeHello: loaded; framework/build identity verified");
    BcHandle ignored{};
    if (host.find("/Script/DeceiveInc.Spy", ignored) != BC_WRONG_THREAD)
        return BC_INTERNAL;
    host.log("NativeHello: worker Unreal call rejected");
    return host.post(on_game_thread, nullptr);
}
