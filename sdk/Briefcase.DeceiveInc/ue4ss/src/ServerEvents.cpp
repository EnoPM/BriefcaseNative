#include <Briefcase/DeceiveInc/ServerEvents.hpp>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace briefcase::deceive::server {

struct EventStream::State {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, Callback> callbacks;
    std::uint64_t next_id{1};
    bool closed{};
};

EventStream::Subscription::Subscription(std::weak_ptr<State> state, std::uint64_t id) noexcept
    : state_(std::move(state)), id_(id) {}

EventStream::Subscription::Subscription(Subscription &&other) noexcept
    : state_(std::move(other.state_)), id_(std::exchange(other.id_, 0)) {}

EventStream::Subscription &EventStream::Subscription::operator=(Subscription &&other) noexcept {
    if (this != &other) {
        reset();
        state_ = std::move(other.state_);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

EventStream::Subscription::~Subscription() { reset(); }

void EventStream::Subscription::reset() noexcept {
    if (id_ == 0)
        return;
    if (auto state = state_.lock()) {
        std::lock_guard lock(state->mutex);
        state->callbacks.erase(id_);
    }
    id_ = 0;
    state_.reset();
}

EventStream::EventStream() : state_(std::make_shared<State>()) {}
EventStream::~EventStream() { close(); }

EventStream::Subscription EventStream::subscribe(Callback callback) {
    if (!callback)
        throw std::invalid_argument("Server event callback is empty");
    std::lock_guard lock(state_->mutex);
    if (state_->closed)
        throw std::runtime_error("Server event stream is closed");
    const auto id = state_->next_id++;
    state_->callbacks.emplace(id, std::move(callback));
    return {state_, id};
}

void EventStream::publish(const Event &event) noexcept {
    try {
        std::vector<Callback> callbacks;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->closed)
                return;
            callbacks.reserve(state_->callbacks.size());
            for (const auto &[id, callback] : state_->callbacks)
                callbacks.push_back(callback);
        }
        for (const auto &callback : callbacks) {
            try {
                callback(event);
            } catch (...) {
                // A mod callback must never unwind into an Unreal hook.
            }
        }
    } catch (...) {
        // Allocation failures must likewise never cross the UE4SS boundary.
    }
}

void EventStream::close() noexcept {
    if (!state_)
        return;
    std::lock_guard lock(state_->mutex);
    state_->closed = true;
    state_->callbacks.clear();
}

bool EventStream::has_subscribers() const noexcept {
    std::lock_guard lock(state_->mutex);
    return !state_->closed && !state_->callbacks.empty();
}

namespace {
std::string new_match_id() {
    std::random_device random;
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i)
        output << std::setw(8) << random();
    return output.str();
}
} // namespace

std::optional<Event> PhaseTracker::observe(const void *game_state, std::uint8_t phase,
                                           std::optional<std::uint8_t> pregame_phase) {
    if (!game_state) {
        clear();
        return std::nullopt;
    }
    if (game_state != last_game_state_ ||
        (pregame_phase && last_phase_ && phase == *pregame_phase && *last_phase_ != phase)) {
        last_game_state_ = game_state;
        last_phase_.reset();
        match_id_ = new_match_id();
        sequence_ = 0;
    }
    if (last_phase_ == phase) return std::nullopt;
    last_phase_ = phase;
    return Event{.kind = EventKind::PhaseChanged,
                 .match_id = match_id_,
                 .sequence = ++sequence_,
                 .occurred_at = std::chrono::system_clock::now(),
                 .phase = phase};
}

void PhaseTracker::clear() noexcept {
    last_game_state_ = nullptr;
    last_phase_.reset();
    match_id_.clear();
    sequence_ = 0;
}

} // namespace briefcase::deceive::server
