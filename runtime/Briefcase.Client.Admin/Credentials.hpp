#pragma once
#include "../Briefcase.Admin/Service.hpp"
namespace bc::admin {
class PasswordStore {
    fs::path file;
    Json load();

  public:
    explicit PasswordStore(fs::path root) : file(root / "Admin" / "passwords.json") {}
    bool contains(const std::string &game, const std::string &endpoint, const std::string &pin);
    std::string get(const std::string &game, const std::string &endpoint, const std::string &pin);
    void put(const std::string &game, const std::string &endpoint, const std::string &pin,
             std::string_view password);
    void forget(const std::string &game);
};
} // namespace bc::admin
