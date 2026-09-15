#pragma once
#include "ModApi.hpp"
#include "StartupApi.h"
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
namespace briefcase {
class Services {
    const BcApi *api_;

  public:
    explicit Services(const BcApi *api) : api_(api) {
        if (!api || api->version != BC_API_VERSION || api->size < sizeof(BcApi) || !api->get_service)
            throw std::runtime_error("Extended API unavailable");
    }
    // Append-only ABI tables may be older than the SDK. Request only the prefix actually used.
    // Callers of optional fields must validate a prefix including those fields.
    template <class T> const T &service(const char *name, uint32_t minimumSize = sizeof(T)) const {
        if (minimumSize < 2 * sizeof(uint32_t) || minimumSize > sizeof(T))
            throw std::runtime_error("Invalid service prefix size");
        const void *table{};
        if (api_->get_service(api_->context, name, 1, &table) != BC_OK || !table)
            throw std::runtime_error("Required service unavailable");
        const auto *typed = static_cast<const T *>(table);
        if (typed->size < minimumSize || typed->version != 1)
            throw std::runtime_error("Service version mismatch");
        return *typed;
    }
    std::string config(std::string_view schema) const {
        auto &c = service<BcConfigApi>(BC_CONFIG_SERVICE);
        uint32_t size{};
        if (c.load(api_->context, schema.data(), static_cast<uint32_t>(schema.size()), nullptr, 0, &size) !=
                BC_LIMIT ||
            !size || size > 65536)
            throw std::runtime_error("Configuration load failed; see host log");
        std::vector<char> text(size);
        if (c.load(api_->context, schema.data(), static_cast<uint32_t>(schema.size()), text.data(), size,
                   &size) != BC_OK)
            throw std::runtime_error("Configuration read failed");
        return std::string(text.data(), size - 1);
    }
    std::string ini(const char *binding, const char *section, const char *key) const {
        auto &s = service<BcStartupApi>(BC_STARTUP_SERVICE);
        uint32_t size{};
        if (s.read_ini(api_->context, binding, section, key, nullptr, 0, &size) != BC_LIMIT || !size ||
            size > 4096)
            throw std::runtime_error("INI read failed; see host log");
        std::vector<char> value(size);
        if (s.read_ini(api_->context, binding, section, key, value.data(), size, &size) != BC_OK)
            throw std::runtime_error("INI read failed");
        return std::string(value.data(), size - 1);
    }
    void stage_ini(const char *binding, const char *section, const char *key, const char *value) const {
        if (service<BcStartupApi>(BC_STARTUP_SERVICE)
                .stage_ini(api_->context, binding, section, key, value) != BC_OK)
            throw std::runtime_error("INI staging failed");
    }
    void stage(const char *hash, std::span<const BcImmediatePatch> patches) const {
        if (service<BcStartupApi>(BC_STARTUP_SERVICE)
                .stage_i32(api_->context, hash, patches.data(), static_cast<uint32_t>(patches.size())) !=
            BC_OK)
            throw std::runtime_error("Instruction validation failed");
    }
};
} // namespace briefcase
