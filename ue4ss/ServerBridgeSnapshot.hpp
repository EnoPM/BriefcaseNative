#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

namespace briefcase::server {

// The game thread publishes complete messages; a local transport reads them.
// The cursor and heartbeat deadline belong to each connection, not this store.
class BridgeSnapshot final {
public:
    void publish(std::string message) {
        std::lock_guard lock(mutex_);
        if (message_ == message) return;
        message_ = std::move(message);
        ++revision_;
    }

    std::string next(std::uint64_t& sent_revision,
                     std::chrono::steady_clock::time_point& next_heartbeat) const {
        std::lock_guard lock(mutex_);
        if (message_.empty()) return {};
        const auto now = std::chrono::steady_clock::now();
        if (sent_revision == revision_ && now < next_heartbeat) return {};
        sent_revision = revision_;
        next_heartbeat = now + std::chrono::seconds(3);
        return message_;
    }

private:
    mutable std::mutex mutex_;
    std::string message_;
    std::uint64_t revision_{};
};

} // namespace briefcase::server
