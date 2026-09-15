#pragma once
#include "Transport.hpp"
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>
namespace bc::admin {
using Json = nlohmann::json;
namespace fs = std::filesystem;
struct Error : std::runtime_error {
    std::string code;
    Error(std::string code, std::string message)
        : std::runtime_error(std::move(message)), code(std::move(code)) {}
};
std::string read_file(const fs::path &, size_t limit = 65536);
void write_file(const fs::path &, std::string_view, bool replace = true, bool private_file = false);
struct Settings {
    std::string server_id, listen_address{"127.0.0.1"}, endpoint;
    uint16_t port{50002};
    IdentityData identity;
    Bytes salt, verifier;
    uint64_t iterations{600000};
    Json serialize() const; // Runtime snapshot: verifier only, never the configured cleartext password.
    static Settings parse(const Json &);
    static Settings load(const fs::path &);
};
Settings provision(const fs::path &briefcase_root, const std::string &address, uint16_t port,
                   const std::string &endpoint, std::string_view password);
class ConfigStore {
    struct Entry {
        std::string id;
        fs::path path;
        Json schema, active;
    };
    struct Receipt {
        std::string request_hash;
        Json response;
    };
    std::mutex mutex;
    std::map<std::string, Entry> entries;
    std::map<std::string, Receipt> receipts;
    std::deque<std::string> receipt_order;
    Json snapshot(const Entry &);

  public:
    static bool allowed(std::string_view);
    void add(const std::string &id, fs::path config, const Json &schema, const Json &active);
    bool contains(const std::string &id);
    Json read(const std::string &id);
    Json write(const Json &payload);
};
class Server {
    Settings settings;
    std::shared_ptr<ConfigStore> configs;
    std::function<Json()> status, mods;
    std::function<void(std::string_view)> log;
    std::function<Json(const std::string &, const Json &)> extension;
    std::function<void()> restarted;
    std::atomic<bool> stopping{};
    std::unique_ptr<Credentials> credentials;
    std::unique_ptr<Listener> listener;
    std::vector<std::thread> workers;
    std::mutex attempts_mutex;
    std::deque<Clock::time_point> global_attempts;
    std::map<std::string, std::deque<Clock::time_point>> attempts;
    bool admit(const std::string &);
    void serve(Stream &);
    void worker() noexcept;

  public:
    Server(Settings, std::shared_ptr<ConfigStore>, std::function<Json()> status, std::function<Json()> mods,
           std::function<void(std::string_view)> log = {},
           std::function<Json(const std::string &, const Json &)> extension = {},
           std::function<void()> restarted = {});
    ~Server();
    void start();
    void request_stop() noexcept;
    void stop() noexcept;
    uint16_t port() const;
};
class Connection {
    Credentials credentials;
    std::unique_ptr<Stream> stream;

  public:
    std::string protocol, server_id;
    void connect(const std::string &endpoint, const std::string &pin, std::string &password,
                 const std::atomic<bool> &stop);
    Json request(const std::string &operation, const Json &payload = Json::object());
};
} // namespace bc::admin
