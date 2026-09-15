#pragma once
#include "../Briefcase.NativeHost/Configuration.hpp"
#include <mutex>
#include <optional>
namespace bc::client_settings {
class Store {
    mutable std::mutex mutex_;
    std::filesystem::path path_;
    nlohmann::json schema_, saved_, active_;
    std::optional<nlohmann::json> queued_;
    uint64_t revision_ = 1, applied_ = 0;
    bool initialized_ = false, pending_ = false, live_ = false;
    std::string message_, tone_ = "info", runtime_key_, runtime_text_, runtime_tone_ = "info";

  public:
    void initialize(std::filesystem::path, const nlohmann::json &, const nlohmann::json &);
    nlohmann::json snapshot() const;
    bool request(uint64_t, const nlohmann::json &);
    void process(); // Host worker only: never render/game thread IO.
    bool current(std::string &, uint64_t &) const;
    void acknowledge(uint64_t);
    void enable_live();
    void report(std::string, std::string, uint32_t);
};
} // namespace bc::client_settings
