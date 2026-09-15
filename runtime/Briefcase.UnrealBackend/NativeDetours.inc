// Validated native member calls. Private to Backend.cpp; MinHook owns instruction relocation.
struct DirectTarget {
    std::shared_ptr<Function> function;
    void *address{};
    void *original{};
    bool enabled{};
    unsigned depth{};
    BcNativeSite site{};
    std::vector<uint8_t> exec_bytes, target_bytes;
};
static std::array<std::unique_ptr<DirectTarget>, 32> direct_targets;
static HookRegistry<HookPayload> direct_registry;
struct DirectOwner {
    uint64_t owner;
    BcHandle function;
    uint64_t internal;
    DirectTarget *target;
};
static std::unordered_map<BcHandle, DirectOwner> direct_owners;
static uint64_t next_direct = (uint64_t(1) << 63) + (uint64_t(1) << 61);
static bool direct_restore_pending;
static void restore_direct_targets() noexcept {
    if (!direct_restore_pending)
        return;
    direct_restore_pending = false;
    for (auto &t : direct_targets)
        if (t && t->enabled && !t->depth &&
            !direct_registry.contains(reinterpret_cast<uint64_t>(t->function->object))) {
            if (MH_DisableHook(t->address) == MH_OK) {
                t->enabled = false;
                if (std::memcmp(t->address, t->target_bytes.data(), t->target_bytes.size()) == 0)
                    log("Native original bytes restored: " + utf8(t->function->object->GetPathName()));
                else
                    log("ERROR: native restoration readback mismatch");
            } else
                log("ERROR: native detour restoration failed");
        }
    for (auto &t : direct_targets)
        if (t && t->enabled && t->depth &&
            !direct_registry.contains(reinterpret_cast<uint64_t>(t->function->object)))
            direct_restore_pending = true;
}
static void direct_dispatch(DirectTarget &t, UObject *self, void *parameters, uint32_t phase) noexcept {
    if (GetCurrentThreadId() != game_thread_id || !ready || !alive(self) || !self->IsA(t.function->owner))
        return;
    ++dispatch_depth;
    HookPayload payload{self, t.function, parameters, nullptr, BC_TRANSPORT_NATIVE};
    try {
        direct_registry.dispatch(reinterpret_cast<uint64_t>(t.function->object), phase, payload);
    } catch (...) {
        log("ERROR: native observer dispatch failed");
    }
    --dispatch_depth;
}
template <size_t Slot, class... Args> static void direct_thunk(UObject *self, Args... args) {
    auto &target = *direct_targets[Slot];
    // Non-game-thread callers never access mod state, registries or Unreal metadata.
    using Original = void (*)(UObject *, Args...);
    auto original = reinterpret_cast<Original>(target.original);
    if (GetCurrentThreadId() != game_thread_id) {
        original(self, args...);
        return;
    }
    ++target.depth;
    alignas(16) std::array<uint8_t, 16> buffer{};
    if constexpr (sizeof...(Args) == 1) {
        static_assert((... && std::is_trivially_copyable_v<Args>));
        auto argument = std::get<0>(std::tuple<Args...>{args...});
        std::memcpy(buffer.data(), &argument, sizeof(argument));
    }
    void *params = sizeof...(Args) ? buffer.data() : nullptr;
    direct_dispatch(target, self, params, BC_HOOK_PRE);
    if constexpr (sizeof...(Args) == 0)
        original(self);
    else {
        using T = std::tuple_element_t<0, std::tuple<Args...>>;
        T argument{};
        std::memcpy(&argument, buffer.data(), sizeof(argument));
        original(self, argument);
    }
    direct_dispatch(target, self, params, BC_HOOK_POST);
    --target.depth;
    restore_direct_targets();
}
template <class... Args, size_t... I> static constexpr auto direct_thunks(std::index_sequence<I...>) {
    return std::array<void (*)(UObject *, Args...), sizeof...(I)>{&direct_thunk<I, Args...>...};
}
static void *direct_callback(size_t slot, uint32_t kind) {
    static constexpr auto none = direct_thunks<>(std::make_index_sequence<32>{});
    static constexpr auto fp = direct_thunks<float>(std::make_index_sequence<32>{});
    static constexpr auto boolean = direct_thunks<bool>(std::make_index_sequence<32>{});
    static constexpr auto object = direct_thunks<UObject *>(std::make_index_sequence<32>{});
    switch (kind) {
    case BC_VALUE_NONE:
        return reinterpret_cast<void *>(none[slot]);
    case BC_VALUE_F32:
        return reinterpret_cast<void *>(fp[slot]);
    case BC_VALUE_BOOL:
        return reinterpret_cast<void *>(boolean[slot]);
    case BC_VALUE_OBJECT:
        return reinterpret_cast<void *>(object[slot]);
    default:
        throw std::runtime_error("Native signature shape unsupported");
    }
}
BcResult backend_native_hook(uint64_t owner, BcHandle h, const BcNativeSite *s, uint32_t phase,
                             BcHookCallback cb, void *user, BcHandle *out) {
    if (auto e = thread_check(); e != BC_OK)
        return e;
    if (!s || !cb || !out || s->size != sizeof(*s) || (phase != BC_HOOK_PRE && phase != BC_HOOK_POST))
        return BC_INVALID_ARGUMENT;
    *out = 0;
    auto fn = function_for(owner, h);
    if (!fn)
        return BC_STALE_HANDLE;
    if (!validated_functions[owner].contains(fn->object))
        return BC_DENIED;
    if (fn->parameters.size() > 1 || fn->size > 8)
        return BC_UNSUPPORTED;
    uint32_t kind = BC_VALUE_NONE;
    if (!fn->parameters.empty()) {
        const auto &p = fn->parameters.front();
        if (p.result || p.property->GetOffset_Internal() != 0 ||
            p.property->HasAnyPropertyFlags(CPF_ReferenceParm))
            return BC_UNSUPPORTED;
        kind = p.kind;
        if (kind != BC_VALUE_F32 && kind != BC_VALUE_BOOL && kind != BC_VALUE_OBJECT)
            return BC_UNSUPPORTED;
    }
    auto *image = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
    auto *nt =
        reinterpret_cast<IMAGE_NT_HEADERS64 *>(image + reinterpret_cast<IMAGE_DOS_HEADER *>(image)->e_lfanew);
    DirectTarget *target{};
    size_t available = 32;
    for (size_t i = 0; i < direct_targets.size(); ++i) {
        if (!direct_targets[i]) {
            if (available == 32)
                available = i;
            continue;
        }
        auto &t = *direct_targets[i];
        if (t.function->object == fn->object) {
            if (s->exec_size != t.exec_bytes.size() || s->target_size != t.target_bytes.size() ||
                s->exec_rva != t.site.exec_rva || s->target_rva != t.site.target_rva ||
                s->branch_offset != t.site.branch_offset || !s->exec_bytes || !s->target_bytes ||
                std::memcmp(s->exec_bytes, t.exec_bytes.data(), t.exec_bytes.size()) ||
                std::memcmp(s->target_bytes, t.target_bytes.data(), t.target_bytes.size()))
                return BC_VERSION_MISMATCH;
            target = &t;
            break;
        }
        if (reinterpret_cast<uintptr_t>(t.address) == reinterpret_cast<uintptr_t>(image) + s->target_rva)
            return BC_DENIED;
    }
    if (!target) {
        if (available == 32)
            return BC_LIMIT;
        auto current = reinterpret_cast<uintptr_t>(fn->object->GetFuncPtr());
        if (auto it = target_by_function.find(fn->object); it != target_by_function.end())
            current = reinterpret_cast<uintptr_t>(it->second->original);
        if (current != reinterpret_cast<uintptr_t>(image) + s->exec_rva)
            return BC_VERSION_MISMATCH;
        validate_native_site(image, nt->OptionalHeader.SizeOfImage, *s);
        auto t = std::make_unique<DirectTarget>();
        t->function = fn;
        t->address = image + s->target_rva;
        t->site = *s;
        t->exec_bytes.assign(s->exec_bytes, s->exec_bytes + s->exec_size);
        t->target_bytes.assign(s->target_bytes, s->target_bytes + s->target_size);
        const auto init = MH_Initialize();
        if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
            return BC_INTERNAL;
        if (MH_CreateHook(t->address, direct_callback(available, kind), &t->original) != MH_OK)
            return BC_UNSUPPORTED;
        target = t.get();
        direct_targets[available] = std::move(t);
    }
    if (!handles.retain(owner, h))
        return BC_STALE_HANDLE;
    uint64_t internal{};
    try {
        internal = direct_registry.add(owner, reinterpret_cast<uint64_t>(fn->object), phase,
                                       [owner, h, phase, cb, user](HookPayload &payload) {
                                           if (calls.size() >= 32)
                                               return;
                                           auto self = handles.intern(owner, payload.self);
                                           if (!self)
                                               return;
                                           const auto call = next_call++;
                                           calls.emplace(call, CallView{owner, payload, phase});
                                           BcHookEvent event{sizeof(event),       phase, *self, h, call,
                                                             BC_TRANSPORT_NATIVE, 1};
                                           try {
                                               cb(&event, user);
                                           } catch (...) {
                                               calls.erase(call);
                                               handles.release(owner, *self);
                                               throw;
                                           }
                                           calls.erase(call);
                                           handles.release(owner, *self);
                                       });
    } catch (...) {
        handles.release(owner, h);
        throw;
    }
    if (!internal) {
        handles.release(owner, h);
        return BC_LIMIT;
    }
    if (!target->enabled) {
        if (MH_EnableHook(target->address) != MH_OK) {
            direct_registry.erase(owner, internal);
            handles.release(owner, h);
            return BC_INTERNAL;
        }
        target->enabled = true;
    }
    const auto token = next_direct++;
    direct_owners.emplace(token, DirectOwner{owner, h, internal, target});
    *out = token;
    log(std::format("Native hook armed: {}, execRva=0x{:X}, implementationRva=0x{:X}; exact bytes validated",
                    utf8(fn->object->GetPathName()), s->exec_rva, s->target_rva));
    return BC_OK;
}
static BcResult direct_unhook(uint64_t owner, BcHandle token) {
    auto it = direct_owners.find(token);
    if (it == direct_owners.end())
        return BC_STALE_HANDLE;
    if (it->second.owner != owner)
        return BC_DENIED;
    direct_registry.erase(owner, it->second.internal);
    handles.release(owner, it->second.function);
    direct_owners.erase(it);
    direct_restore_pending = true;
    restore_direct_targets();
    return BC_OK;
}
static void cleanup_direct_owner(uint64_t owner) {
    const auto count = direct_registry.size();
    direct_registry.erase_owner(owner);
    std::erase_if(direct_owners, [&](auto &p) { return p.second.owner == owner; });
    direct_restore_pending = true;
    restore_direct_targets();
    log(std::format("Owner {} native cleanup: {} hooks removed", owner, count - direct_registry.size()));
}
