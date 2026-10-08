#include <Briefcase/DeceiveInc/ServerEvents.hpp>
#include <Briefcase/DeceiveInc/MatchState.hpp>

#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace briefcase::deceive::server {
void EventCollector::update() noexcept {
    if (!events_.has_subscribers()) return;
    const auto now = std::chrono::steady_clock::now();
    if (now < next_scan_) return;
    next_scan_ = now + std::chrono::milliseconds(250);

    try {
        using namespace RC::Unreal;
        auto *type = UObjectGlobals::StaticFindObject<UClass *>(
            nullptr, nullptr, STR("/Script/DeceiveInc.DeceiveIncMatchGameState"));
        if (!type) return;
        UObject *current{};
        UObjectGlobals::ForEachUObject([&](UObject *object, std::int32_t, std::int32_t) {
            if (object && !object->IsUnreachable() &&
                UObjectArray::IsValid(object->GetObjectItem(), false) && object->IsA(type) &&
                !MatchState{object}.is_template()) {
                current = object;
                return RC::LoopAction::Break;
            }
            return RC::LoopAction::Continue;
        });
        if (!current) {
            phases_.clear();
            return;
        }
        if (!pregame_phase_)
            pregame_phase_ = resolve_match_phase(L"PREGAME");
        const auto phase = static_cast<std::uint8_t>(MatchState{current}.phase());
        if (auto event = phases_.observe(current, phase, pregame_phase_)) events_.publish(*event);
    } catch (...) {
        // Unreal discovery is transient during map travel; retry next tick.
    }
}
} // namespace briefcase::deceive::server
