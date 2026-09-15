#pragma once
#include "Startup.hpp"
#include "UnrealApi.h"
#include <cstddef>
#include <span>
#include <string>
namespace briefcase {
inline BcValue i32(int32_t value) {
    BcValue v{};
    v.kind = BC_VALUE_I32;
    v.data.integer = value;
    return v;
}
inline BcValue f32(float value) {
    BcValue v{};
    v.kind = BC_VALUE_F32;
    v.data.number = value;
    return v;
}
inline void require(BcResult result, const char *operation) {
    if (result != BC_OK)
        throw std::runtime_error(std::string(operation) + " failed (" + std::to_string(result) + ")");
}
class Unreal {
    const BcApi *api_;
    const BcUnrealApi *service_;

  public:
    explicit Unreal(const BcApi *api)
        : api_(api), service_(&Services(api).service<BcUnrealApi>(BC_UNREAL_SERVICE,
                                                                  offsetof(BcUnrealApi, write_property))) {}
    const BcUnrealApi &raw() const { return *service_; }
    BcHandle resolve(const char *path, const char *signature) const {
        BcHandle h{};
        require(service_->resolve_function(api_->context, path, signature, &h), "resolve function");
        return h;
    }
    BcValue invoke(BcHandle self, BcHandle fn, std::span<const BcNamedValue> arguments = {}) const {
        BcValue result{};
        require(
            service_->invoke(api_->context, self, fn, arguments.data(), uint32_t(arguments.size()), &result),
            "invoke");
        return result;
    }
    BcHandle hook(BcHandle fn, uint32_t phase, BcHookCallback callback, void *user = nullptr) const {
        BcHandle h{};
        require(service_->hook(api_->context, fn, phase, callback, user, &h), "hook");
        return h;
    }
    BcHandle deleted(BcDeletedCallback callback, void *user = nullptr) const {
        BcHandle h{};
        require(service_->on_deleted(api_->context, callback, user, &h), "deleted subscription");
        return h;
    }
    void unhook(BcHandle &h) const {
        if (h) {
            require(service_->unhook(api_->context, h), "unhook");
            h = 0;
        }
    }
    void retain(BcHandle h) const { require(service_->retain(api_->context, h), "retain"); }
    void release(BcHandle &h) const {
        if (h) {
            api_->release_handle(api_->context, h);
            h = 0;
        }
    }
    BcValue property(BcHandle h, const char *name) const {
        BcValue v{};
        require(service_->read_property(api_->context, h, name, &v), "read property");
        return v;
    }
};
} // namespace briefcase
