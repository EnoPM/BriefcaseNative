#include <Briefcase/DeceiveInc/MatchState.hpp>

#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FObjectProperty.hpp>
#include <Unreal/UEnum.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <cstring>
#include <cwchar>
#include <stdexcept>

namespace briefcase::deceive {
namespace {
using namespace RC::Unreal;

constexpr auto phase_property = STR("GamePhase");
constexpr auto getter_path = STR("/Script/DeceiveInc.DeceiveIncGameStateBase:GetCurrentPhaseTimeLeftInSeconds");
constexpr auto setter_path = STR("/Script/DeceiveInc.DeceiveIncGameStateBase:SetCurrentPhaseTimeLeftInSeconds");
constexpr auto phase_enum_path = STR("/Script/DeceiveInc.ESpyGamePhase");

UFunction *function(const wchar_t *path) {
    auto *value = UObjectGlobals::StaticFindObject<UFunction *>(nullptr, nullptr, path);
    if (!value)
        throw std::runtime_error("Required Deceive Inc function is unavailable");
    return value;
}

void require_live(UObject *object) {
    if (!object || object->IsUnreachable() || !UObjectArray::IsValid(object->GetObjectItem(), false))
        throw std::runtime_error("Match state is no longer valid");
}
} // namespace

MatchState::operator bool() const noexcept {
    return object_ && !object_->IsUnreachable() && UObjectArray::IsValid(object_->GetObjectItem(), false);
}

MatchPhase MatchState::phase() const {
    require_live(object_);
    auto *property = object_->GetPropertyByNameInChain(phase_property);
    if (!property || property->GetSize() != 1)
        throw std::runtime_error("GamePhase layout changed");
    return static_cast<MatchPhase>(*property->ContainerPtrToValuePtr<std::uint8_t>(object_));
}

std::int32_t MatchState::remaining_seconds() const {
    require_live(object_);
    std::int32_t result{};
    object_->ProcessEvent(function(getter_path), &result);
    return result;
}

void MatchState::set_remaining_seconds(std::int32_t seconds) const {
    require_live(object_);
    object_->ProcessEvent(function(setter_path), &seconds);
}

bool MatchState::is_template() const noexcept {
    return object_ && object_->HasAnyFlags(static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject));
}

std::wstring MatchState::path() const {
    require_live(object_);
    return object_->GetPathName();
}

std::uint8_t resolve_match_phase(const wchar_t *enumerator) {
    if (!enumerator || !*enumerator)
        throw std::invalid_argument("Enumerator is empty");
    auto *value = UObjectGlobals::StaticFindObject<UEnum *>(nullptr, nullptr, phase_enum_path);
    if (!value)
        throw std::runtime_error("ESpyGamePhase is unavailable");
    for (const auto pair : value->ForEachName()) {
        const auto name = pair.Key.ToString();
        if (name == enumerator || (name.size() > std::wcslen(enumerator) &&
                                   name.ends_with(std::wstring(L"::") + enumerator))) {
            if (pair.Value < 0 || pair.Value > 255)
                throw std::runtime_error("ESpyGamePhase value is outside uint8");
            return static_cast<std::uint8_t>(pair.Value);
        }
    }
    throw std::runtime_error("ESpyGamePhase enumerator was not found");
}

} // namespace briefcase::deceive
