#pragma once
#include "Catalog.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace bc::locale {
class Client {
    Path root;
    std::vector<Manifest> mods;
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    bool stopping{}, requested{}, started{}, busy{};
    std::atomic<uint32_t> menu_key_{0x70};
    uint32_t requested_key{};
    std::string requested_language;
    Json state = {{"language", "fr"},
                  {"catalogues", Json::object()},
                  {"languages", Json::object()},
                  {"pending", false}};
    std::string serialized = state.dump();
    uint64_t sequence{1};
    void run();

  public:
    Client(Path, std::vector<Manifest>);
    ~Client();
    bool snapshot(std::string &, uint64_t &);
    bool select(const std::string &);
    uint32_t menu_key() const noexcept { return menu_key_.load(std::memory_order_relaxed); }
    bool select_menu_key(uint32_t);
    void stop();
};
} // namespace bc::locale
