#include <Briefcase/DeceiveInc/Spy.hpp>

#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <cmath>
#include <stdexcept>

namespace briefcase::deceive {
namespace {
using namespace RC::Unreal;

constexpr auto spy_class_path = STR("/Script/DeceiveInc.Spy");

void require_live(UObject *object) {
    if (!object || object->IsUnreachable() || !UObjectArray::IsValid(object->GetObjectItem(), false))
        throw std::runtime_error("Spy is no longer valid");
}

UClass *spy_class() {
    auto *value = UObjectGlobals::StaticFindObject<UClass *>(nullptr, nullptr, spy_class_path);
    if (!value)
        throw std::runtime_error("DeceiveInc.Spy class is unavailable");
    return value;
}

UFunction *function(const wchar_t *path) {
    auto *value = UObjectGlobals::StaticFindObject<UFunction *>(nullptr, nullptr, path);
    if (!value)
        throw std::runtime_error("Required Spy function is unavailable");
    return value;
}

template <typename T> T read(UObject *object, const wchar_t *name) {
    require_live(object);
    auto *property = object->GetPropertyByNameInChain(name);
    if (!property || property->GetSize() != sizeof(T))
        throw std::runtime_error("Spy property layout changed");
    return *property->ContainerPtrToValuePtr<T>(object);
}

template <typename T> void write(UObject *object, const wchar_t *name, T value) {
    require_live(object);
    auto *property = object->GetPropertyByNameInChain(name);
    if (!property || property->GetSize() != sizeof(T))
        throw std::runtime_error("Spy property layout changed");
    *property->ContainerPtrToValuePtr<T>(object) = value;
}
} // namespace

Spy::operator bool() const noexcept {
    return object_ && !object_->IsUnreachable() && UObjectArray::IsValid(object_->GetObjectItem(), false);
}

bool Spy::is_template() const noexcept {
    return object_ && object_->HasAnyFlags(static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject));
}

bool Spy::is_authoritative_player() const {
    require_live(object_);
    return object_->IsA(spy_class()) && !is_template() && role() == 3;
}

std::uint8_t Spy::role() const { return read<std::uint8_t>(object_, STR("Role")); }
float Spy::stamina() const { return read<float>(object_, STR("StaminaCurrent")); }
float Spy::cover_ratio() const { return read<float>(object_, STR("CoverRatio")); }
float Spy::stamina_drain_multiplier() const {
    return read<float>(object_, STR("StaminaDrainRateMultiplier"));
}

void Spy::set_stamina_drain_multiplier(float value) const {
    if (!std::isfinite(value))
        throw std::invalid_argument("Stamina drain multiplier must be finite");
    write(object_, STR("StaminaDrainRateMultiplier"), value);
}

bool Spy::run_drain_enabled() const {
    require_live(object_);
    struct Params {
        bool ReturnValue{};
    } params;
    object_->ProcessEvent(function(STR("/Script/DeceiveInc.Spy:GetRunDrainEnabled")), &params);
    return params.ReturnValue;
}

void Spy::set_run_drain_enabled(bool enabled) const {
    require_live(object_);
    struct Params {
        bool Enabled{};
    } params{enabled};
    object_->ProcessEvent(function(STR("/Script/DeceiveInc.Spy:SetRunDrainEnabled")), &params);
}

std::wstring Spy::path() const {
    require_live(object_);
    return object_->GetPathName();
}

} // namespace briefcase::deceive
