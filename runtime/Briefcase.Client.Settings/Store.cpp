#include "Store.hpp"
#include "../Briefcase.NativeHost/Startup.hpp"
#include <Windows.h>
#include <fstream>
namespace bc::client_settings {
void Store::initialize(std::filesystem::path path, const nlohmann::json &schema,
                       const nlohmann::json &value) {
    std::lock_guard lock(mutex_);
    path_ = std::move(path);
    schema_ = schema;
    saved_ = active_ = normalize_config(schema, value);
    initialized_ = true;
    applied_ = live_ ? 0 : revision_;
}
nlohmann::json Store::snapshot() const {
    std::lock_guard lock(mutex_);
    if (!initialized_)
        return nullptr;
    return {{"schema", schema_},
            {"saved", saved_},
            {"active", active_},
            {"revision", std::to_string(revision_)},
            {"pending", pending_},
            {"live", live_},
            {"restartRequired", !live_ && revision_ != applied_},
            {"message", message_},
            {"tone", tone_},
            {"awaitingApply", live_ && revision_ != applied_},
            {"runtimeKey", runtime_key_},
            {"runtimeText", runtime_text_},
            {"runtimeTone", runtime_tone_}};
}
bool Store::request(uint64_t expected, const nlohmann::json &value) {
    std::lock_guard lock(mutex_);
    if (!initialized_ || pending_ || expected != revision_)
        return false;
    try {
        queued_ = normalize_config(schema_, value);
        pending_ = true;
        message_.clear();
        return true;
    } catch (const std::exception &e) {
        message_ = e.what();
        tone_ = "danger";
        return false;
    }
}
void Store::process() {
    nlohmann::json value;
    {
        std::lock_guard lock(mutex_);
        if (!queued_)
            return;
        value = std::move(*queued_);
        queued_.reset();
    }
    try {
        assert_plain_path(path_);
        auto temporary = path_;
        temporary += L".tmp";
        assert_plain_path(temporary);
        const auto encoded = value.dump(2) + "\n";
        if (encoded.size() > 65535)
            throw std::runtime_error("Configuration exceeds 64 KiB");
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file << encoded;
            file.flush();
            if (!file)
                throw std::runtime_error("Cannot write configuration");
        }
        if (!MoveFileExW(temporary.c_str(), path_.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace configuration");
        std::lock_guard lock(mutex_);
        saved_ = std::move(value);
        ++revision_;
        pending_ = false;
        message_ = "saved";
        tone_ = "success";
    } catch (const std::exception &e) {
        std::lock_guard lock(mutex_);
        pending_ = false;
        message_ = e.what();
        tone_ = "danger";
    }
}
bool Store::current(std::string &out, uint64_t &revision) const {
    std::lock_guard lock(mutex_);
    if (!initialized_ || revision == revision_)
        return false;
    out = saved_.dump();
    revision = revision_;
    return true;
}
void Store::report(std::string key, std::string text, uint32_t severity) {
    std::lock_guard lock(mutex_);
    runtime_key_ = std::move(key);
    runtime_text_ = std::move(text);
    runtime_tone_ = severity == 1 ? "success" : severity == 2 ? "warning" : severity == 3 ? "danger" : "info";
}
void Store::enable_live() { std::lock_guard lock(mutex_); live_=true; }
void Store::acknowledge(uint64_t revision) {
    std::lock_guard lock(mutex_);
    if (revision == revision_) {
        active_ = saved_;
        applied_ = revision;
    }
}
} // namespace bc::client_settings
