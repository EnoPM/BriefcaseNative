#pragma once
#include "../Briefcase.Admin/Service.hpp"
#include "Credentials.hpp"
#include <condition_variable>
#include <optional>
namespace bc::admin {
class Client {
    struct Command {
        std::string operation, name, game_endpoint, endpoint, pin, password;
        uint64_t favorite{};
        bool remember{};
        Json payload;
        Command() = default;
        Command(Command &&) = default;
        Command &operator=(Command &&) = default;
        ~Command() { erase(password); }
    };
    std::chrono::milliseconds heartbeat_interval;
    fs::path root;
    PasswordStore passwords;
    std::string selected_game;
    std::mutex mutex;
    std::condition_variable changed;
    std::unique_ptr<std::thread> worker;
    std::atomic<bool> quitting{}, cancelled{};
    std::optional<Command> command;
    Json state{{"state", "idle"}, {"pending", false}, {"favoriteId", 0}, {"message", ""}};
    std::string serialized = state.dump();
    uint64_t sequence{1};
    void publish();
    bool enqueue(Command);
    void run() noexcept;
    void refresh(Connection &);
    void load_pairing(const Command &);
    void save_pairing(const Command &);

  public:
    explicit Client(fs::path, std::chrono::milliseconds heartbeat = std::chrono::seconds(30));
    ~Client();
    bool select(uint64_t, const std::string &name, const std::string &game_endpoint);
    bool connect(uint64_t, const std::string &name, const std::string &game_endpoint,
                 const std::string &endpoint, const std::string &pin, const std::string &password,
                 bool remember = false);
    bool submit(const std::string &operation, const Json &payload = Json::object());
    void disconnect();
    void request_stop() noexcept;
    void stop() noexcept;
    bool snapshot(std::string &out, uint64_t &version);
};
} // namespace bc::admin
