// Private reflected hook transport. No arbitrary native addresses cross the ABI.
struct HookPayload {
    UObject *self;
    std::shared_ptr<Function> function;
    void *parameters;
    void *result;
    uint32_t transport;
};
struct CallView {
    uint64_t owner;
    HookPayload payload;
    uint32_t phase;
};
static HookRegistry<HookPayload> hook_registry;
struct HookOwner {
    uint64_t owner;
    BcHandle function;
};
static std::unordered_map<BcHandle, HookOwner> hook_owners;
static std::unordered_map<BcHandle, CallView> calls;
static uint64_t next_call = uint64_t(1) << 62;
struct NativeTarget {
    std::shared_ptr<Function> function;
    UnrealScriptFunction original;
    UnrealScriptFunction replacement;
    bool active;
};
static std::array<std::unique_ptr<NativeTarget>, 128> native_targets;
static std::unordered_map<UFunction *, NativeTarget *> target_by_function;
static thread_local std::vector<std::pair<UObject *, UFunction *>> event_stack;
struct DeleteRegistration {
    uint64_t owner;
    BcDeletedCallback callback;
    void *user;
};
static std::unordered_map<BcHandle, DeleteRegistration> deletion_callbacks;
static uint64_t next_deletion = (uint64_t(1) << 63) + (uint64_t(1) << 62);
static void dispatch_hooks(HookPayload &payload, uint32_t phase) noexcept {
    ++dispatch_depth;
    if (hook_registry.dispatch(reinterpret_cast<uint64_t>(payload.function->object), phase, payload))
        log("ERROR: reflected hook callback failed and was disabled");
    --dispatch_depth;
}
template <size_t Slot> static void reflected_thunk(UObject *self, FFrame &frame, void *result) {
    auto *target = native_targets[Slot].get();
    const auto original = target->original;
    if (GetCurrentThreadId() != game_thread_id || !backend_ready() || !target->active ||
        (!event_stack.empty() && event_stack.back() == std::pair(self, target->function->object))) {
        original(self, frame, result);
        return;
    }
    HookPayload payload{self, target->function, frame.Code() ? nullptr : frame.Locals(), result,
                        BC_TRANSPORT_THUNK};
    dispatch_hooks(payload, BC_HOOK_PRE);
    original(self, frame, result);
    dispatch_hooks(payload, BC_HOOK_POST);
}
template <size_t... Slots> static constexpr auto thunk_table(std::index_sequence<Slots...>) {
    return std::array<UnrealScriptFunction, sizeof...(Slots)>{reflected_thunk<Slots>...};
}
static auto thunks = thunk_table(std::make_index_sequence<128>{});
static void install_transport(const std::shared_ptr<Function> &fn) {
    NativeTarget *target{};
    if (auto i = target_by_function.find(fn->object); i != target_by_function.end())
        target = i->second;
    if (!target) {
        size_t slot = 0;
        while (slot < native_targets.size() && native_targets[slot])
            ++slot;
        if (slot == native_targets.size())
            throw std::runtime_error("Reflected hook target limit");
        auto original = fn->object->GetFuncPtr();
        if (!original)
            throw std::runtime_error("Null reflected function thunk");
        native_targets[slot] =
            std::make_unique<NativeTarget>(NativeTarget{fn, original, thunks[slot], false});
        target = native_targets[slot].get();
        target_by_function.emplace(fn->object, target);
    }
    if (target->active)
        return;
    if (fn->object->GetFuncPtr() != target->original)
        throw std::runtime_error("Reflected thunk changed by another component");
    target->active = true;
    fn->object->SetFuncPtr(target->replacement);
}
static void restore_unused_transports() {
    for (auto &t : native_targets)
        if (t && t->active && !hook_registry.contains(reinterpret_cast<uint64_t>(t->function->object))) {
            if (alive(t->function->object) && t->function->object->GetFuncPtr() == t->replacement)
                t->function->object->SetFuncPtr(t->original);
            t->active = false;
        }
}
static void reflected_event_pre(UObject *self, UFunction *function, void *parameters) noexcept {
    if (GetCurrentThreadId() != game_thread_id || !backend_ready())
        return;
    try {
        event_stack.push_back({self, function});
        const auto i = target_by_function.find(function);
        if (i == target_by_function.end() || !i->second->active)
            return;
        HookPayload payload{self, i->second->function, parameters, nullptr, BC_TRANSPORT_EVENT};
        dispatch_hooks(payload, BC_HOOK_PRE);
    } catch (...) {
        log("ERROR: ProcessEvent hook dispatch failed");
    }
}
static void reflected_event_post(UObject *self, UFunction *function, void *parameters) noexcept {
    if (GetCurrentThreadId() != game_thread_id || event_stack.empty() ||
        event_stack.back() != std::pair(self, function))
        return;
    const auto i = target_by_function.find(function);
    if (i != target_by_function.end()) {
        HookPayload payload{self, i->second->function, parameters, nullptr, BC_TRANSPORT_EVENT};
        dispatch_hooks(payload, BC_HOOK_POST);
    }
    event_stack.pop_back();
}
BcResult backend_hook(uint64_t owner, BcHandle h, uint32_t phase, BcHookCallback callback, void *user,
                      BcHandle *out) {
    if (auto e = thread_check(); e != BC_OK)
        return e;
    if (!callback || !out || (phase != BC_HOOK_PRE && phase != BC_HOOK_POST))
        return BC_INVALID_ARGUMENT;
    *out = 0;
    auto fn = function_for(owner, h);
    if (!fn)
        return BC_STALE_HANDLE;
    if (!validated_functions[owner].contains(fn->object))
        return BC_DENIED;
    install_transport(fn);
    if (!handles.retain(owner, h))
        return BC_STALE_HANDLE;
    uint64_t token{};
    try {
        token = hook_registry.add(owner, reinterpret_cast<uint64_t>(fn->object), phase,
                                  [owner, h, phase, callback, user](HookPayload &payload) {
                                      if (!alive(payload.self) || calls.size() >= 32)
                                          return;
                                      auto self = handles.intern(owner, payload.self);
                                      if (!self) {
                                          log("Hook skipped: handle capacity");
                                          return;
                                      }
                                      const auto call = next_call++;
                                      calls.emplace(call, CallView{owner, payload, phase});
                                      const BcHookEvent event{
                                          sizeof(BcHookEvent),
                                          phase,
                                          *self,
                                          h,
                                          call,
                                          payload.transport,
                                          payload.parameters || payload.function->size == 0 ? 1u : 0u};
                                      try {
                                          callback(&event, user);
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
        restore_unused_transports();
        throw;
    }
    if (!token) {
        handles.release(owner, h);
        restore_unused_transports();
        return BC_LIMIT;
    }
    hook_owners.emplace(token, HookOwner{owner, h});
    *out = token;
    log("Hook armed (ProcessEvent + reflected thunk): " + utf8(fn->object->GetPathName()));
    return BC_OK;
}
static BcResult direct_unhook(uint64_t, BcHandle);
static void cleanup_direct_owner(uint64_t);
BcResult backend_unhook(uint64_t owner, BcHandle token) {
    if (auto e = thread_check(); e != BC_OK)
        return e;
    if (auto i = deletion_callbacks.find(token); i != deletion_callbacks.end()) {
        if (i->second.owner != owner)
            return BC_DENIED;
        deletion_callbacks.erase(i);
        return BC_OK;
    }
    auto i = hook_owners.find(token);
    if (i == hook_owners.end())
        return direct_unhook(owner, token);
    if (i->second.owner != owner)
        return BC_DENIED;
    hook_registry.erase(owner, token);
    handles.release(owner, i->second.function);
    hook_owners.erase(i);
    restore_unused_transports();
    return BC_OK;
}
BcResult backend_read_argument(uint64_t owner, BcHandle call, const char *name, BcValue *out) {
    if (auto e = thread_check(); e != BC_OK)
        return e;
    if (!name || strnlen_s(name, 257) > 256 || !out)
        return BC_INVALID_ARGUMENT;
    auto i = calls.find(call);
    if (i == calls.end() || i->second.owner != owner)
        return BC_STALE_HANDLE;
    auto &c = i->second;
    for (const auto &p : c.payload.function->parameters)
        if (p.name == name) {
            if (p.result && c.phase == BC_HOOK_PRE)
                return BC_NOT_READY;
            if (c.payload.parameters)
                return read_value(owner, p.property, c.payload.parameters, out);
            // A VM-stepped frame has no materialized parameter buffer; never guess its layout.
            return BC_NOT_READY;
        }
    return BC_NOT_FOUND;
}
BcResult backend_write_argument(uint64_t owner, BcHandle call, const char *name, const BcValue *value) {
    if (auto e = thread_check(); e != BC_OK)
        return e;
    if (!name || strnlen_s(name, 257) > 256 || !value)
        return BC_INVALID_ARGUMENT;
    auto i = calls.find(call);
    if (i == calls.end() || i->second.owner != owner)
        return BC_STALE_HANDLE;
    auto &c = i->second;
    if (c.phase != BC_HOOK_PRE)
        return BC_DENIED;
    for (const auto &p : c.payload.function->parameters)
        if (p.name == name) {
            if (p.result || p.property->HasAnyPropertyFlags(CPF_ReferenceParm | CPF_OutParm))
                return BC_DENIED;
            if (!c.payload.parameters)
                return BC_NOT_READY;
            return write_value(owner, p.property, c.payload.parameters, *value);
        }
    return BC_NOT_FOUND;
}
BcResult backend_deleted(uint64_t owner, BcDeletedCallback callback, void *user, BcHandle *out) {
    if (auto e = thread_check(); e != BC_OK)
        return e;
    if (!callback || !out)
        return BC_INVALID_ARGUMENT;
    if (deletion_callbacks.size() >= 128)
        return BC_LIMIT;
    const auto token = next_deletion++;
    deletion_callbacks.emplace(token, DeleteRegistration{owner, callback, user});
    *out = token;
    return BC_OK;
}
static void dispatch_deleted(const std::vector<HandleTable<UObject>::Revoked> &events) noexcept {
    try {
        const auto snapshot = deletion_callbacks;
        for (const auto &event : events)
            for (const auto &[id, reg] : snapshot)
                if (reg.owner == event.owner && deletion_callbacks.contains(id)) {
                    try {
                        reg.callback(event.token, reg.user);
                    } catch (...) {
                        deletion_callbacks.erase(id);
                        log("ERROR: deletion callback disabled after exception");
                    }
                }
    } catch (...) {
        log("ERROR: deletion dispatch failed");
    }
}
void backend_cleanup_owner(uint64_t owner) {
    if (GetCurrentThreadId() != game_thread_id)
        throw std::runtime_error("Owner cleanup requires game thread");
    {
        std::lock_guard lock(queue_mutex);
        disabled_owners.insert(owner);
        std::erase_if(queue, [&](const auto &task) { return task.owner == owner; });
    }
    cleanup_direct_owner(owner);
    const auto before = hook_registry.size();
    hook_registry.erase_owner(owner);
    for (auto i = hook_owners.begin(); i != hook_owners.end();) {
        if (i->second.owner == owner)
            i = hook_owners.erase(i);
        else
            ++i;
    }
    std::erase_if(deletion_callbacks, [&](auto &item) { return item.second.owner == owner; });
    restore_unused_transports();
    handles.erase_owner(owner);
    validated_functions.erase(owner);
    log(std::format("Owner {} cleanup complete: {} reflected hooks removed", owner,
                    before - hook_registry.size()));
}
