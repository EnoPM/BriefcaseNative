#pragma once

#include "ServerBridgeProtocol.hpp"
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace briefcase::server {

enum class BridgeCommandSet { LocalBasic, All };

// Owns the command lifecycle across the game thread and one local UI connection.
// Resetting the session discards commands and replies from the previous client.
class BridgeCommandChannel final {
public:
    void reset_session() {
        std::lock_guard lock(mutex_);
        session_.fetch_add(1, std::memory_order_release);
        commands_.clear();
        responses_.clear();
        pending_.store(false, std::memory_order_release);
    }

    void receive(std::string_view line, BridgeCommandSet supported) {
        auto command = BridgeProtocol::parse_command(line);
        if (!command) return;
        if (supported == BridgeCommandSet::LocalBasic &&
            command->kind != BridgeCommand::Kind::Kick &&
            command->kind != BridgeCommand::Kind::MoveTeam &&
            command->kind != BridgeCommand::Kind::SwapTeam) return;
        std::lock_guard lock(mutex_);
        if (commands_.size() >= 32) {
            responses_.push_back(BridgeProtocol::result(
                command->request_id, false, "Server command queue is full"));
            return;
        }
        command->session = session_.load(std::memory_order_relaxed);
        commands_.push_back(std::move(*command));
        pending_.store(true, std::memory_order_release);
    }

    bool has_pending() const noexcept { return pending_.load(std::memory_order_acquire); }

    std::deque<BridgeCommand> take_commands() {
        std::lock_guard lock(mutex_);
        std::deque<BridgeCommand> result;
        result.swap(commands_);
        pending_.store(false, std::memory_order_release);
        return result;
    }

    bool is_current(const BridgeCommand& command) const noexcept {
        return command.session == session_.load(std::memory_order_acquire);
    }

    void reply(const BridgeCommand& command, bool ok, std::string_view message) {
        if (!is_current(command)) return;
        std::lock_guard lock(mutex_);
        if (command.session == session_.load(std::memory_order_relaxed))
            responses_.push_back(BridgeProtocol::result(command.request_id, ok, message));
    }

    std::deque<std::string> take_responses() {
        std::lock_guard lock(mutex_);
        std::deque<std::string> result;
        result.swap(responses_);
        return result;
    }

private:
    mutable std::mutex mutex_;
    std::deque<BridgeCommand> commands_;
    std::deque<std::string> responses_;
    std::atomic<std::uint64_t> session_{};
    std::atomic<bool> pending_{};
};

} // namespace briefcase::server
