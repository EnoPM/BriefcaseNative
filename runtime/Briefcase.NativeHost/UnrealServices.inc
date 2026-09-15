template <uint64_t Capability, class Callback>
static BcResult unreal_call(void *context, Callback callback) noexcept {
    auto *s = scope(context, Capability);
    if (!s)
        return BC_DENIED;
    try {
        return callback(s->id);
    } catch (const std::exception &e) {
        log(s->manifest.id + ": Unreal API rejected: " + e.what());
        return BC_INVALID_ARGUMENT;
    } catch (...) {
        log(s->manifest.id + ": Unreal API internal error");
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL u_resolve(void *c, const char *p, const char *s, BcHandle *out) noexcept {
    return unreal_call<BC_CAP_REFLECTION>(c, [&](uint64_t id) { return backend_resolve(id, p, s, out); });
}
static BcResult BC_CALL u_describe(void *c, BcHandle h, char *out, uint32_t cap, uint32_t *size) noexcept {
    return unreal_call<BC_CAP_REFLECTION>(
        c, [&](uint64_t id) { return backend_describe(id, h, out, cap, size); });
}
static BcResult BC_CALL u_info(void *c, BcHandle h, BcObjectInfo *out) noexcept {
    return unreal_call<BC_CAP_REFLECTION>(c, [&](uint64_t id) { return backend_info(id, h, out); });
}
static BcResult BC_CALL u_retain(void *c, BcHandle h) noexcept {
    return unreal_call<BC_CAP_REFLECTION>(c, [&](uint64_t id) { return backend_retain(id, h); });
}
static BcResult BC_CALL u_property(void *c, BcHandle h, const char *name, BcValue *out) noexcept {
    return unreal_call<BC_CAP_REFLECTION>(
        c, [&](uint64_t id) { return backend_read_property(id, h, name, out); });
}
static BcResult BC_CALL u_invoke(void *c, BcHandle self, BcHandle fn, const BcNamedValue *a, uint32_t n,
                                 BcValue *out) noexcept {
    return unreal_call<BC_CAP_INVOCATION>(
        c, [&](uint64_t id) { return backend_invoke(id, self, fn, a, n, out); });
}
static BcResult BC_CALL u_hook(void *c, BcHandle fn, uint32_t phase, BcHookCallback cb, void *user,
                               BcHandle *out) noexcept {
    return unreal_call<BC_CAP_HOOKS>(c,
                                     [&](uint64_t id) { return backend_hook(id, fn, phase, cb, user, out); });
}
static BcResult BC_CALL u_unhook(void *c, BcHandle h) noexcept {
    // Owners may remove either a hook or a lifecycle registration.
    return unreal_call<0>(c, [&](uint64_t id) { return backend_unhook(id, h); });
}
static BcResult BC_CALL u_argument(void *c, BcHandle h, const char *name, BcValue *out) noexcept {
    return unreal_call<BC_CAP_HOOKS>(c, [&](uint64_t id) { return backend_read_argument(id, h, name, out); });
}
static BcResult BC_CALL u_deleted(void *c, BcDeletedCallback cb, void *user, BcHandle *out) noexcept {
    return unreal_call<BC_CAP_LIFECYCLE>(c, [&](uint64_t id) { return backend_deleted(id, cb, user, out); });
}
static BcResult BC_CALL u_enum(void *c, const char *p, const char *n, int64_t *out) noexcept {
    return unreal_call<BC_CAP_REFLECTION>(c, [&](uint64_t id) { return backend_enum(id, p, n, out); });
}
static BcResult BC_CALL u_write_property(void *c, BcHandle h, const char *name,
                                         const BcValue *value) noexcept {
    return unreal_call<BC_CAP_UNREAL_WRITE | BC_CAP_REFLECTION>(
        c, [&](uint64_t id) { return backend_write_property(id, h, name, value); });
}
static BcResult BC_CALL u_write_argument(void *c, BcHandle h, const char *name,
                                         const BcValue *value) noexcept {
    return unreal_call<BC_CAP_UNREAL_WRITE | BC_CAP_HOOKS>(
        c, [&](uint64_t id) { return backend_write_argument(id, h, name, value); });
}
static BcResult BC_CALL u_outputs(void *c,BcHandle self,BcHandle fn,const BcNamedValue *a,uint32_t n,BcNamedValue *o,uint32_t count,BcValue *v) noexcept {
 return unreal_call<BC_CAP_INVOCATION>(c,[&](uint64_t id){return backend_invoke_outputs(id,self,fn,a,n,o,count,v);});
}
static BcResult BC_CALL u_actors(void *c,BcHandle world,const char *path,BcHandle *out,uint32_t cap,uint32_t *count) noexcept {
 return unreal_call<BC_CAP_REFLECTION>(c,[&](uint64_t id){return backend_enumerate_actors(id,world,path,out,cap,count);});
}
static BcResult BC_CALL u_is_a(void *c,BcHandle h,const char *path,uint32_t *out) noexcept {
 return unreal_call<BC_CAP_REFLECTION>(c,[&](uint64_t id){return backend_is_a(id,h,path,out);});
}
static const BcUnrealApi unreal_service = {sizeof(BcUnrealApi),
                                           1,
                                           u_resolve,
                                           u_describe,
                                           u_info,
                                           u_retain,
                                           u_property,
                                           u_invoke,
                                           u_hook,
                                           u_unhook,
                                           u_argument,
                                           u_deleted,
                                           u_enum,
                                           u_write_property,
                                           u_write_argument, u_outputs, u_actors, u_is_a};

static BcResult BC_CALL u_native(void *c, BcHandle fn, const char *sha, const BcNativeSite *site,
                                 uint32_t phase, BcHookCallback cb, void *user, BcHandle *out) noexcept {
    return unreal_call<BC_CAP_NATIVE_HOOKS | BC_CAP_HOOKS | BC_CAP_REFLECTION>(c, [&](uint64_t id) {
        if (!sha || platform::bounded_length(sha, 65) != 64 || std::strcmp(sha, build_info.executable_sha256))
            return BC_VERSION_MISMATCH;
        return backend_native_hook(id, fn, site, phase, cb, user, out);
    });
}
static const BcNativeHookApi native_service = {sizeof(BcNativeHookApi), 1, u_native};
