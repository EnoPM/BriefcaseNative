#include <Briefcase/DeceiveInc/ServerEvents.hpp>

#include <stdexcept>
#include <utility>

using namespace briefcase::deceive::server;

static_assert(EventCollector::supports(EventKind::PhaseChanged));
static_assert(!EventCollector::supports(EventKind::MatchWon));
static_assert(!EventCollector::supports(EventKind::DamageDealt));

int main() {
    EventStream stream;
    Event event{.kind = EventKind::PhaseChanged, .match_id = "match-1", .sequence = 1};
    int first{};
    int second{};
    auto one = stream.subscribe([&](const Event &received) {
        if (received.match_id != "match-1")
            throw std::runtime_error("wrong match ID");
        ++first;
        throw std::runtime_error("callback failure must be contained");
    });
    auto two = stream.subscribe([&](const Event &) { ++second; });
    stream.publish(event);
    if (first != 1 || second != 1) return 1;

    one.reset();
    stream.publish(event);
    if (first != 1 || second != 2) return 2;

    auto moved = std::move(two);
    if (two || !moved) return 3;
    moved.reset();
    stream.publish(event);
    if (second != 2) return 4;

    stream.close();
    bool rejected{};
    try {
        [[maybe_unused]] auto ignored = stream.subscribe([](const Event &) {});
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    if (!rejected) return 5;

    PhaseTracker phases;
    int first_object{};
    int second_object{};
    const void *first_state = &first_object;
    const void *second_state = &second_object;
    auto initial = phases.observe(first_state, 1);
    if (!initial || initial->kind != EventKind::PhaseChanged || !initial->phase ||
        *initial->phase != 1 || initial->sequence != 1 || initial->match_id.empty()) return 6;
    if (phases.observe(first_state, 1)) return 7;
    auto next = phases.observe(first_state, 2);
    if (!next || next->match_id != initial->match_id || next->sequence != 2) return 8;
    auto another = phases.observe(second_state, 1);
    if (!another || another->match_id == initial->match_id || another->sequence != 1) return 9;
    phases.clear();
    auto restarted = phases.observe(first_state, 1);
    if (!restarted || restarted->match_id == initial->match_id) return 10;
    auto live = phases.observe(first_state, 3, 1);
    auto next_match = phases.observe(first_state, 1, 1);
    if (!live || !next_match || next_match->match_id == live->match_id ||
        next_match->sequence != 1) return 11;
}
