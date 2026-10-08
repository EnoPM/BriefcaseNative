#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace briefcase::deceive::server {

// These are server observations, not client-reported achievements. A producer
// must only publish a kind after validating its authoritative Unreal source.
enum class EventKind {
    PhaseChanged,
    Elimination,
    TerminalCompleted,
    KeycardPrinted,
    RetinalScanCompleted,
    VaultEntered,
    PackagePickedFromPodium,
    PackageAcquired,
    DamageDealt,
    DamageTaken,
    MatchWon,
};

struct PlayerIdentity {
    // Only fill account_id when it comes from a verified server-side platform
    // identity. A display name or UObject address is never a ranked ID.
    std::optional<std::string> account_id;
    std::string display_name;
    std::string session_id;
};

struct Event {
    EventKind kind{};
    std::string match_id;
    std::uint64_t sequence{};
    std::chrono::system_clock::time_point occurred_at{};
    std::optional<PlayerIdentity> actor;
    std::optional<PlayerIdentity> target;
    std::optional<std::uint8_t> phase;
    std::optional<double> amount;
};

class EventStream final {
    struct State;

  public:
    using Callback = std::function<void(const Event &)>;

    class Subscription final {
      public:
        Subscription() noexcept = default;
        Subscription(const Subscription &) = delete;
        Subscription &operator=(const Subscription &) = delete;
        Subscription(Subscription &&other) noexcept;
        Subscription &operator=(Subscription &&other) noexcept;
        ~Subscription();

        void reset() noexcept;
        [[nodiscard]] explicit operator bool() const noexcept { return id_ != 0; }

      private:
        friend class EventStream;
        Subscription(std::weak_ptr<State> state, std::uint64_t id) noexcept;
        std::weak_ptr<State> state_;
        std::uint64_t id_{};
    };

    EventStream();
    EventStream(const EventStream &) = delete;
    EventStream &operator=(const EventStream &) = delete;
    ~EventStream();

    [[nodiscard]] Subscription subscribe(Callback callback);
    // Intended for the game adapter. Delivery is synchronous on the producer
    // thread; subscribers should enqueue network/Discord work elsewhere.
    void publish(const Event &event) noexcept;
    void close() noexcept;
    [[nodiscard]] bool has_subscribers() const noexcept;

  private:
    std::shared_ptr<State> state_;
};

class PhaseTracker final {
  public:
    [[nodiscard]] std::optional<Event> observe(
        const void *game_state, std::uint8_t phase,
        std::optional<std::uint8_t> pregame_phase = std::nullopt);
    void clear() noexcept;

  private:
    const void *last_game_state_{};
    std::optional<std::uint8_t> last_phase_;
    std::string match_id_;
    std::uint64_t sequence_{};
};

// Per-mod server collector. Call update() from CppUserModBase::on_update().
// Currently only PhaseChanged has a verified reflected source. Other event
// kinds are reserved until their authoritative source and player identity
// can be validated on a live dedicated server.
class EventCollector final {
  public:
    using Subscription = EventStream::Subscription;
    [[nodiscard]] static constexpr bool supports(EventKind kind) noexcept {
        return kind == EventKind::PhaseChanged;
    }
    [[nodiscard]] Subscription subscribe(EventStream::Callback callback) {
        return events_.subscribe(std::move(callback));
    }
    void update() noexcept;

  private:
    EventStream events_;
    std::chrono::steady_clock::time_point next_scan_{};
    PhaseTracker phases_;
    std::optional<std::uint8_t> pregame_phase_;
};

} // namespace briefcase::deceive::server
