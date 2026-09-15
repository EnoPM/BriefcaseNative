#include <Briefcase/Unreal.hpp>
#include <array>
#include <format>
static const BcApi *api;
static const BcUnrealApi *unreal;
static BcHandle function, object, pre, post, retained, last_call;
static int checks, pre_count, post_count;
static bool callback_ok = true, write_mode;
static constexpr const char *path = "/Script/Engine.KismetMathLibrary:Abs_Int";
static constexpr const char *signature = R"({"parameterSize":8,"parameters":[
 {"name":"A","type":"int32","offset":0},{"name":"ReturnValue","type":"int32","offset":4,"return":true}]})";
static void check(bool value) {
    ++checks;
    if (!value)
        throw std::runtime_error("Runtime probe check " + std::to_string(checks));
}
static void BC_CALL observed(const BcHookEvent *e, void *) noexcept {
    BcValue value{};
    if (write_mode) {
        last_call = e->call;
        auto input = briefcase::i32(-21), wrong = briefcase::f32(21);
        if (e->phase == BC_HOOK_PRE) {
            callback_ok = callback_ok &&
                          unreal->write_argument(api->context, e->call, "A", &wrong) == BC_INVALID_ARGUMENT &&
                          unreal->write_argument(api->context, e->call, "ReturnValue", &input) == BC_DENIED &&
                          unreal->write_argument(api->context, e->call, "A", &input) == BC_OK &&
                          unreal->read_argument(api->context, e->call, "A", &value) == BC_OK &&
                          value.data.integer == -21;
        } else {
            callback_ok = callback_ok &&
                          unreal->write_argument(api->context, e->call, "A", &input) == BC_DENIED &&
                          unreal->read_argument(api->context, e->call, "ReturnValue", &value) == BC_OK &&
                          value.data.integer == 21;
        }
        return;
    }
    callback_ok = callback_ok && e->parameters_available && e->transport == BC_TRANSPORT_EVENT &&
                  unreal->read_argument(api->context, e->call, "A", &value) == BC_OK &&
                  value.kind == BC_VALUE_I32 && value.data.integer < 0;
    last_call = e->call;
    if (e->phase == BC_HOOK_PRE) {
        ++pre_count;
        if (!retained) {
            retained = e->self;
            callback_ok = callback_ok && unreal->retain(api->context, retained) == BC_OK;
        }
        callback_ok = callback_ok && e->self == retained;
    } else {
        ++post_count;
        callback_ok = callback_ok &&
                      unreal->read_argument(api->context, e->call, "ReturnValue", &value) == BC_OK &&
                      value.data.integer == 7;
    }
}
static void BC_CALL on_deleted(BcHandle, void *) noexcept {}
static void BC_CALL probe(void *) noexcept {
    try {
        briefcase::Unreal u(api);
        int64_t phase{};
        check(unreal->enum_value(api->context, "/Script/DeceiveInc.ESpyGamePhase", "ESpyGamePhase::PREGAME",
                                 &phase) == BC_OK &&
              phase == 1);
        check(unreal->enum_value(api->context, "/Script/DeceiveInc.ESpyGamePhase", "NotAnEnumerator",
                                 &phase) == BC_NOT_FOUND);
        function = u.resolve(path, signature);
        check(api->find_object(api->context, "/Script/Engine.Default__KismetMathLibrary",
                               sizeof("/Script/Engine.Default__KismetMathLibrary") - 1, &object) == BC_OK);
        BcObjectInfo info{};
        info.size = sizeof(info);
        check(unreal->object_info(api->context, object, &info) == BC_OK && (info.flags & 16));
        BcHandle bad{};
        check(unreal->resolve_function(api->context, path, R"({"parameterSize":0,"parameters":[]})", &bad) ==
                  BC_INVALID_ARGUMENT &&
              bad == 0);
        pre = u.hook(function, BC_HOOK_PRE, observed);
        post = u.hook(function, BC_HOOK_POST, observed);
        std::array<BcNamedValue, 1> args{{{"A", briefcase::i32(-7)}}};
        auto result = u.invoke(object, function, args);
        check(result.kind == BC_VALUE_I32 && result.data.integer == 7);
        check(pre_count == 1 && post_count == 1 && callback_ok);
        BcValue expired{};
        check(unreal->read_argument(api->context, last_call, "A", &expired) == BC_STALE_HANDLE);
        args[0].value = briefcase::f32(-7);
        check(unreal->invoke(api->context, object, function, args.data(), 1, &result) == BC_INVALID_ARGUMENT);
        check(pre_count == 1 && post_count == 1);
        args[0].value = briefcase::i32(-9);
        u.unhook(post);
        result = u.invoke(object, function, args);
        check(result.data.integer == 9 && pre_count == 2 && post_count == 1 && callback_ok);
        u.unhook(pre);
        args[0].value = briefcase::i32(-11);
        result = u.invoke(object, function, args);
        check(result.data.integer == 11 && pre_count == 2 && post_count == 1);
        auto subscription = u.deleted(on_deleted);
        auto old = subscription;
        u.unhook(subscription);
        check(unreal->unhook(api->context, old) == BC_STALE_HANDLE);
        check(unreal->invoke(reinterpret_cast<void *>(1), object, function, args.data(), 1, &result) ==
              BC_DENIED);
        write_mode = true;
        pre = u.hook(function, BC_HOOK_PRE, observed);
        post = u.hook(function, BC_HOOK_POST, observed);
        args[0].value = briefcase::i32(-3);
        result = u.invoke(object, function, args);
        check(result.data.integer == 21 && callback_ok);
        check(unreal->write_argument(api->context, last_call, "A", &args[0].value) == BC_STALE_HANDLE);
        check(unreal->write_argument(reinterpret_cast<void *>(1), last_call, "A", &args[0].value) ==
              BC_DENIED);
        check(unreal->write_property(api->context, object, "Any", &args[0].value) == BC_DENIED);
        u.unhook(pre);
        u.unhook(post);
        result = u.invoke(object, function, args);
        check(result.data.integer == 3);
        u.release(retained);
        u.release(object);
        u.release(function);
        briefcase::Api(api).log(
            std::format("RUNTIME PROBE PASS: {} checks; typed invoke, deduplicated "
                        "hooks, PRE writes, POST/CDO write rejection, removal, stale calls, ownership",
                        checks));
    } catch (const std::exception &e) {
        briefcase::Api(api).log(std::string("RUNTIME PROBE FAILED: ") + e.what());
    }
}
extern "C" BC_EXPORT BcResult BC_CALL BriefcaseModLoad(const BcApi *provided) noexcept {
    try {
        api = provided;
        unreal = &briefcase::Services(api).service<BcUnrealApi>(BC_UNREAL_SERVICE);
        BcHandle ignored{};
        check(unreal->resolve_function(api->context, path, signature, &ignored) == BC_WRONG_THREAD);
        return api->post_game_thread(api->context, probe, nullptr);
    } catch (...) {
        return BC_INTERNAL;
    }
}
extern "C" BC_EXPORT void BC_CALL BriefcaseModUnload() noexcept {
    if (!api)
        return;
    for (auto *h : {&pre, &post})
        if (*h) {
            unreal->unhook(api->context, *h);
            *h = 0;
        }
    for (auto *h : {&retained, &object, &function})
        if (*h) {
            api->release_handle(api->context, *h);
            *h = 0;
        }
    briefcase::Api(api).log("Runtime probe unload complete");
}
