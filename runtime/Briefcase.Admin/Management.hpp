#pragma once
#include "../Briefcase.NativeHost/Manifest.hpp"
#include "Service.hpp"
namespace bc::admin {
class Management {
    fs::path root, ini, profile;
    std::mutex mutex;
    Json config_active, selection_active, balance_active;
    std::vector<Manifest> manifests;
    Json catalog = Json::object();
    Json config_read(), config_write(const Json &);
    Json selection_read(), selection_write(const Json &);
    Json balance_read(const std::string &), balance_write(const Json &);

  public:
    Management(fs::path briefcase_root, std::vector<Manifest>);
    Json dispatch(const std::string &, const Json &);
    static Json config_schema();
    static std::string group(const std::string &table, const std::string &row);
};
Json log_tail(const fs::path &, size_t maximum = 32768);
Json schedule_restart(const fs::path &root);
void commit_restart() noexcept;
} // namespace bc::admin
