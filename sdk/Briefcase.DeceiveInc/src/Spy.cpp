#include "SpyContracts.hpp"
#include <Briefcase/DeceiveInc/Spy.hpp>
#include <Briefcase/Unreal.hpp>
#include <algorithm>
#include <array>

namespace briefcase::deceive_inc {
namespace detail {
struct SpyBindings {
    const BcApi *api;
    const BcUnrealApi *unreal;
    std::array<ObjectHandle, size_t(SpyFunction::Count)> functions;

    explicit SpyBindings(const BcApi *host)
        : api(host), unreal(&Services(host).service<BcUnrealApi>(BC_UNREAL_SERVICE)) {
        if (!api->release_handle || !api->validate_handle || !unreal->resolve_function || !unreal->invoke ||
            !unreal->invoke_outputs || !unreal->enumerate_actors || !unreal->is_a || !unreal->retain ||
            !unreal->object_info)
            require(BC_VERSION_MISMATCH, "Spy API");
        // Owned array members also clean up a partially failed initialization.
        for (size_t i = 0; i < spy_contracts.size(); ++i) {
            const auto &contract = spy_contracts[i];
            BcHandle handle{};
            require(unreal->resolve_function(api->context, contract.path, contract.signature, &handle),
                    contract.path);
            functions[i] = ObjectHandle::Adopt(api, handle);
            require(handle ? BC_OK : BC_STALE_HANDLE, contract.path);
        }
    }
    BcHandle Function(SpyFunction function) const { return functions[size_t(function)].Handle(); }
    BcValue Invoke(BcHandle object, SpyFunction function, uint32_t kind) const {
        BcValue result{};
        require(unreal->invoke(api->context, object, Function(function), nullptr, 0, &result),
                spy_contracts[size_t(function)].path);
        CheckKind(result, kind);
        return result;
    }
    void CheckKind(const BcValue &value, uint32_t expected) const {
        if (value.kind != expected) {
            // Even a malformed object result must not leak its owned reference.
            if (value.kind == BC_VALUE_OBJECT && value.data.object)
                api->release_handle(api->context, value.data.object);
            require(BC_VERSION_MISMATCH, "Spy result type");
        }
    }
};
} // namespace detail
using detail::SpyFunction;
static Vector3 vector(const BcValue &value) {
    return {value.data.vector[0], value.data.vector[1], value.data.vector[2]};
}
Spy::Spy(std::shared_ptr<detail::SpyBindings> bindings, ObjectHandle object) noexcept
    : bindings_(std::move(bindings)), object_(std::move(object)) {}
const detail::SpyBindings &Spy::Bindings() const {
    if (!bindings_ || !object_)
        require(BC_STALE_HANDLE, "Empty Spy");
    return *bindings_;
}
void Spy::Reset() noexcept {
    object_.Reset();
    bindings_.reset();
}
bool Spy::IsDead() const {
    return Bindings().Invoke(Handle(), SpyFunction::Dead, BC_VALUE_BOOL).data.integer != 0;
}
bool Spy::IsBot() const {
    return Bindings().Invoke(Handle(), SpyFunction::Bot, BC_VALUE_BOOL).data.integer != 0;
}
bool Spy::IsLocallyControlled() const {
    return Bindings().Invoke(Handle(), SpyFunction::Local, BC_VALUE_BOOL).data.integer != 0;
}
bool Spy::IsInADS() const {
    return Bindings().Invoke(Handle(), SpyFunction::ADS, BC_VALUE_BOOL).data.integer != 0;
}
Vector3 Spy::GetLocation() const {
    return vector(Bindings().Invoke(Handle(), SpyFunction::Location, BC_VALUE_VECTOR));
}
Vector3 Spy::GetVelocity() const {
    return vector(Bindings().Invoke(Handle(), SpyFunction::Velocity, BC_VALUE_VECTOR));
}
EyeViewPoint Spy::GetEyesViewPoint() const {
    const auto &b = Bindings();
    BcNamedValue out[] = {{"OutLocation", {}}, {"OutRotation", {}}};
    BcValue result{};
    require(b.unreal->invoke_outputs(b.api->context, Handle(), b.Function(SpyFunction::Eyes), nullptr, 0, out,
                                     2, &result),
            "Spy.GetEyesViewPoint");
    b.CheckKind(out[0].value, BC_VALUE_VECTOR);
    b.CheckKind(out[1].value, BC_VALUE_ROTATOR);
    return {vector(out[0].value),
            {out[1].value.data.vector[0], out[1].value.data.vector[1], out[1].value.data.vector[2]}};
}
std::string Spy::GetObjectPath() const {
    const auto &b = Bindings();
    BcObjectInfo info{sizeof(info)};
    require(b.unreal->object_info(b.api->context, Handle(), &info), "Spy.GetObjectPath");
    return std::string(info.path, std::find(std::begin(info.path), std::end(info.path), '\0'));
}
ObjectHandle Spy::GetController() const {
    const auto &b = Bindings();
    return ObjectHandle::Adopt(b.api,
                               b.Invoke(Handle(), SpyFunction::Controller, BC_VALUE_OBJECT).data.object);
}
ObjectHandle Spy::GetWeaponTool() const {
    const auto &b = Bindings();
    return ObjectHandle::Adopt(b.api, b.Invoke(Handle(), SpyFunction::Weapon, BC_VALUE_OBJECT).data.object);
}
SpyApi::SpyApi(const BcApi *api) : bindings_(std::make_shared<detail::SpyBindings>(api)) {}
std::vector<Spy> SpyApi::FindAll(BcHandle worldContext) const {
    require(bindings_ ? BC_OK : BC_STALE_HANDLE, "Empty SpyApi");
    const auto &b = *bindings_;
    std::array<BcHandle, 512> handles{};
    uint32_t count{};
    require(b.unreal->enumerate_actors(b.api->context, worldContext, "/Script/DeceiveInc.Spy", handles.data(),
                                       uint32_t(handles.size()), &count),
            "Spy.FindAll");
    // Successful ABI output is owned. Guard it before allocating the result vector.
    // On failure the backend already releases any partial collection.
    struct Batch {
        const BcApi *api;
        std::array<BcHandle, 512> &handles;
        uint32_t count;
        ~Batch() {
            for (uint32_t i = 0; i < count; ++i)
                if (handles[i])
                    api->release_handle(api->context, handles[i]);
        }
    } batch{b.api, handles, std::min(count, uint32_t(handles.size()))};
    require(count <= handles.size() ? BC_OK : BC_LIMIT, "Spy.FindAll count");
    std::vector<Spy> result;
    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
        result.push_back(Spy(bindings_, ObjectHandle::Adopt(b.api, std::exchange(handles[i], 0))));
    return result;
}
Spy SpyApi::FromHandle(BcHandle borrowed) const {
    require(bindings_ ? BC_OK : BC_STALE_HANDLE, "Empty SpyApi");
    const auto &b = *bindings_;
    require(borrowed ? BC_OK : BC_STALE_HANDLE, "Spy.FromHandle");
    uint32_t matches{};
    require(b.unreal->is_a(b.api->context, borrowed, "/Script/DeceiveInc.Spy", &matches), "Spy.FromHandle");
    require(matches ? BC_OK : BC_INVALID_ARGUMENT, "Object is not a Spy");
    require(b.unreal->retain(b.api->context, borrowed), "Retain Spy");
    return Spy(bindings_, ObjectHandle::Adopt(b.api, borrowed));
}
} // namespace briefcase::deceive_inc
