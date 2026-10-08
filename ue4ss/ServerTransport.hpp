#pragma once

#include <cstdint>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>

namespace briefcase::server {

// A transport publishes protocol messages supplied by the game-state collector.
// Windows named pipes, Unix sockets and authenticated network streams can all
// implement this interface without changing the collector.
class IServerTransport {
public:
    using NextMessage = std::function<std::string(std::uint64_t& last_revision)>;
    using ReceiveMessage = std::function<void(std::string_view message)>;
    virtual ~IServerTransport() = default;
    virtual void run(std::stop_token stop, const NextMessage& next_message,
                     const ReceiveMessage& receive_message) = 0;
    virtual void interrupt() noexcept = 0;
};

}
