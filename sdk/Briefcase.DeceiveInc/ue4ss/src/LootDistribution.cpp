#include <Briefcase/DeceiveInc/LootDistribution.hpp>

#include <Helpers/String.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStrProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UStruct.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <algorithm>
#include <stdexcept>

namespace briefcase::deceive {
namespace {
using namespace RC::Unreal;

FProperty *field(UStruct *type, const wchar_t *name) {
    if (!type) throw std::runtime_error("Loot struct is unavailable");
    for (auto *property : type->ForEachPropertyInChain())
        if (property->GetName() == name) return property;
    throw std::runtime_error("Deceive Inc loot field is unavailable");
}

FProperty *field(UObject *object, const wchar_t *name) {
    if (!object || object->IsUnreachable()) throw std::runtime_error("Loot object is unavailable");
    auto *property = object->GetPropertyByNameInChain(name);
    if (!property) throw std::runtime_error("Deceive Inc loot field is unavailable");
    return property;
}

template <typename T> T number(UStruct *type, void *record, const wchar_t *name) {
    auto *property = field(type, name);
    if (property->GetSize() != sizeof(T)) throw std::runtime_error("Loot numeric layout changed");
    return *property->ContainerPtrToValuePtr<T>(record);
}
template <typename T> void set_number(UStruct *type, void *record, const wchar_t *name, T value) {
    auto *property = field(type, name);
    if (property->GetSize() != sizeof(T)) throw std::runtime_error("Loot numeric layout changed");
    *property->ContainerPtrToValuePtr<T>(record) = value;
}
std::string string(UStruct *type, void *record, const wchar_t *name) {
    auto *property = field(type, name);
    if (!property->IsA<FStrProperty>()) throw std::runtime_error("Loot string layout changed");
    auto *value = property->ContainerPtrToValuePtr<FString>(record);
    const auto *characters = value->GetCharArray();
    return characters ? RC::to_string(characters) : std::string{};
}
std::string string(UObject *object, const wchar_t *name) {
    auto *property = field(object, name);
    if (!property->IsA<FStrProperty>()) throw std::runtime_error("Loot string layout changed");
    auto *value = property->ContainerPtrToValuePtr<FString>(object);
    const auto *characters = value->GetCharArray();
    return characters ? RC::to_string(characters) : std::string{};
}
void set_string(UStruct *type, void *record, const wchar_t *name, const std::string &value) {
    auto *property = field(type, name);
    if (!property->IsA<FStrProperty>()) throw std::runtime_error("Loot string layout changed");
    *property->ContainerPtrToValuePtr<FString>(record) = FString(RC::to_wstring(value).c_str());
}
bool boolean(UStruct *type, void *record, const wchar_t *name) {
    auto *property = field(type, name);
    if (!property->IsA<FBoolProperty>()) throw std::runtime_error("Loot boolean layout changed");
    return static_cast<FBoolProperty *>(property)->GetPropertyValueInContainer(record);
}
void set_boolean(UStruct *type, void *record, const wchar_t *name, bool value) {
    auto *property = field(type, name);
    if (!property->IsA<FBoolProperty>()) throw std::runtime_error("Loot boolean layout changed");
    static_cast<FBoolProperty *>(property)->SetPropertyValueInContainer(record, value);
}

struct StructArray {
    FArrayProperty *property;
    UStruct *type;
    FProperty *inner;
    FScriptArray *values;
    StructArray(FProperty *source, void *container)
        : property(source && source->IsA<FArrayProperty>() ? static_cast<FArrayProperty *>(source) : nullptr),
          type(property && property->GetInner()->IsA<FStructProperty>()
                   ? static_cast<FStructProperty *>(property->GetInner())->GetStruct() : nullptr),
          inner(property ? property->GetInner() : nullptr),
          values(require_property()->ContainerPtrToValuePtr<FScriptArray>(container)) {
        if (!type) throw std::runtime_error("Loot array element layout changed");
        if (values->Num() < 0 || values->Num() > 100000) throw std::runtime_error("Loot array is oversized");
    }
    FArrayProperty *require_property() const {
        if (!property) throw std::runtime_error("Loot array layout changed");
        return property;
    }
    int size() const { return values->Num(); }
    void *row(int index) const {
        return static_cast<std::uint8_t *>(values->GetData()) + index * inner->GetElementSize();
    }
    void replace_size(int size) const {
        const auto original = values->Num();
        for (int i = 0; i < original; ++i) inner->DestroyValue(row(i));
        values->Empty(0, inner->GetElementSize(), inner->GetMinAlignment());
        values->Add(size, inner->GetElementSize(), inner->GetMinAlignment());
        for (int i = 0; i < size; ++i) inner->InitializeValue(row(i));
    }
};

StructArray array(UObject *object, const wchar_t *name) {
    return {field(object, name), object};
}
StructArray array(UStruct *type, void *record, const wchar_t *name) {
    return {field(type, name), record};
}

LootCandidate candidate(UStruct *type, void *record) {
    return {string(type, record, L"ObjectType"), number<float>(type, record, L"SpawnWeight"),
            number<std::uint32_t>(type, record, L"ObjectTypeCRC")};
}

bool live(UObject *object) {
    return object && !object->IsUnreachable() &&
           !object->HasAnyFlags(static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject));
}
} // namespace

bool LootDistribution::is_manager(UObject *object) {
    static UClass *type{};
    if (!type) type = UObjectGlobals::StaticFindObject<UClass *>(
        nullptr, nullptr, STR("/Script/DeceiveInc.ObjectSpawningManager"));
    return type && live(object) && object->IsA(type) &&
           object->GetPropertyByNameInChain(STR("ObjectsToSpawn")) &&
           object->GetPropertyByNameInChain(STR("ObjectsToSpawnCount"));
}
bool LootDistribution::is_point(UObject *object) {
    static UClass *actor{};
    static UClass *component{};
    if (!actor) actor = UObjectGlobals::StaticFindObject<UClass *>(
        nullptr, nullptr, STR("/Script/DeceiveInc.ObjectSpawn"));
    if (!component) component = UObjectGlobals::StaticFindObject<UClass *>(
        nullptr, nullptr, STR("/Script/DeceiveInc.ObjectSpawnComponent"));
    return live(object) && ((actor && object->IsA(actor)) ||
                            (component && object->IsA(component))) &&
           object->GetPropertyByNameInChain(STR("CustomPossibleObjectsToSpawn")) &&
           object->GetPropertyByNameInChain(STR("PartOfRoomCRC"));
}

std::vector<LootObject> LootDistribution::objects(UObject *manager) {
    if (!is_manager(manager)) throw std::runtime_error("Not a live loot manager");
    auto entries = array(manager, L"ObjectsToSpawn");
    std::vector<LootObject> result;
    result.reserve(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        auto *row = entries.row(i);
        result.push_back({string(entries.type, row, L"ObjectType"),
                          number<float>(entries.type, row, L"OccurenceFactor"),
                          number<float>(entries.type, row, L"ObjectScore"),
                          boolean(entries.type, row, L"MaxOnePerRoom"),
                          boolean(entries.type, row, L"CanFillRoom"),
                          boolean(entries.type, row, L"CanSpawnInPrivateLobbies"),
                          boolean(entries.type, row, L"bSpawnOnStart"),
                          number<std::int32_t>(entries.type, row, L"SpawnPriority")});
    }
    return result;
}

std::vector<LootCount> LootDistribution::counts(UObject *manager) {
    if (!is_manager(manager)) throw std::runtime_error("Not a live loot manager");
    auto entries = array(manager, L"ObjectsToSpawnCount");
    std::vector<LootCount> result;
    result.reserve(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        auto *row = entries.row(i);
        result.push_back({string(entries.type, row, L"ObjectType"),
                          number<std::uint32_t>(entries.type, row, L"SpawnCount")});
    }
    return result;
}

bool LootDistribution::update_object(UObject *manager, const LootObject &value) {
    auto entries = array(manager, L"ObjectsToSpawn");
    for (int i = 0; i < entries.size(); ++i) {
        auto *row = entries.row(i);
        if (string(entries.type, row, L"ObjectType") != value.object_type) continue;
        set_number(entries.type, row, L"OccurenceFactor", value.occurrence_factor);
        set_number(entries.type, row, L"ObjectScore", value.score);
        set_boolean(entries.type, row, L"MaxOnePerRoom", value.max_one_per_room);
        set_boolean(entries.type, row, L"CanFillRoom", value.can_fill_room);
        set_boolean(entries.type, row, L"CanSpawnInPrivateLobbies", value.private_lobbies);
        set_boolean(entries.type, row, L"bSpawnOnStart", value.spawn_on_start);
        set_number(entries.type, row, L"SpawnPriority", value.priority);
        return true;
    }
    return false;
}

bool LootDistribution::update_count(UObject *manager, const LootCount &value) {
    auto entries = array(manager, L"ObjectsToSpawnCount");
    for (int i = 0; i < entries.size(); ++i) {
        auto *row = entries.row(i);
        if (string(entries.type, row, L"ObjectType") != value.object_type) continue;
        set_number(entries.type, row, L"SpawnCount", value.count);
        return true;
    }
    return false;
}

std::optional<LootPoint> LootDistribution::point(UObject *object) {
    if (!is_point(object)) return std::nullopt;
    auto *custom_property = field(object, L"CustomPossibleObjectsToSpawn");
    if (!custom_property->IsA<FStructProperty>()) throw std::runtime_error("Loot candidate struct changed");
    auto *custom_type = static_cast<FStructProperty *>(custom_property)->GetStruct();
    auto *custom = custom_property->ContainerPtrToValuePtr<void>(object);
    auto entries = array(custom_type, custom, L"ObjectsToSpawn");
    LootPoint result;
    result.object = object;
    result.path = RC::to_string(object->GetPathName());
    result.implementation = object->GetPropertyByNameInChain(STR("DisableSpawnPoint")) ? "Component" : "Actor";
    result.point_type = string(object, L"ObjectSpawnPointType");
    if (result.point_type.empty()) result.point_type = "Default";
    result.room_crc = *field(object, L"PartOfRoomCRC")->ContainerPtrToValuePtr<std::uint32_t>(object);
    if (result.implementation == "Component")
        result.disabled = static_cast<FBoolProperty *>(field(object, L"DisableSpawnPoint"))
                              ->GetPropertyValueInContainer(object);
    result.candidates.reserve(entries.size());
    for (int i = 0; i < entries.size(); ++i)
        result.candidates.push_back(candidate(entries.type, entries.row(i)));
    result.selection = result.candidates.empty() ? "Preset" :
                       result.candidates.size() == 1 ? "Fixed" : "Weighted";
    return result;
}

void LootDistribution::replace_candidates(UObject *point,
                                           const std::vector<LootCandidate> &candidates) {
    if (!is_point(point) || candidates.empty() || candidates.size() > 64)
        throw std::runtime_error("Invalid loot point replacement");
    auto *custom_property = field(point, L"CustomPossibleObjectsToSpawn");
    if (!custom_property->IsA<FStructProperty>()) throw std::runtime_error("Loot candidate struct changed");
    auto *custom_type = static_cast<FStructProperty *>(custom_property)->GetStruct();
    auto *custom = custom_property->ContainerPtrToValuePtr<void>(point);
    auto entries = array(custom_type, custom, L"ObjectsToSpawn");
    // FScriptArrayHelper constructs/destroys FString elements through Unreal's
    // reflected property; never memcpy an Unreal struct containing a TArray.
    entries.replace_size(static_cast<std::int32_t>(candidates.size()));
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        auto *row = entries.row(static_cast<std::int32_t>(i));
        set_string(entries.type, row, L"ObjectType", candidates[i].object_type);
        set_number(entries.type, row, L"SpawnWeight", candidates[i].weight);
        set_number(entries.type, row, L"ObjectTypeCRC", object_type_crc(candidates[i].object_type));
    }
}

std::vector<LootRoom> LootDistribution::rooms() {
    auto *room_class = UObjectGlobals::StaticFindObject<UClass *>(
        nullptr, nullptr, STR("/Script/DeceiveInc.RoomVolume"));
    if (!room_class) throw std::runtime_error("RoomVolume class is unavailable");
    std::vector<LootRoom> result;
    UObjectGlobals::ForEachUObject([&](UObject *object, std::int32_t, std::int32_t) {
        if (!live(object) || !object->IsA(room_class)) return RC::LoopAction::Continue;
        const auto crc = *field(object, L"RoomCRC")->ContainerPtrToValuePtr<std::uint32_t>(object);
        if (crc) {
            const auto security = *field(object, L"SecurityLevel")
                                       ->ContainerPtrToValuePtr<std::uint8_t>(object);
            const auto vault = static_cast<FBoolProperty *>(field(object, L"bInVault"))
                                   ->GetPropertyValueInContainer(object);
            result.push_back({crc, security, vault});
        }
        return RC::LoopAction::Continue;
    });
    return result;
}

std::uint32_t LootDistribution::object_type_crc(const std::string &object_type) {
    std::uint32_t crc = 0xffffffffu;
    const auto text = RC::to_wstring(object_type);
    for (wchar_t character : text) {
        std::uint32_t code = static_cast<std::uint32_t>(character);
        for (int index = 0; index < 4; ++index) {
            crc ^= code & 0xffu;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
            code >>= 8;
        }
    }
    return ~crc;
}
} // namespace briefcase::deceive
