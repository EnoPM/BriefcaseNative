#ifndef _WIN32
#include "LinuxServerBridge.hpp"
#include "LinuxSpectatorApi.hpp"
#include "../Briefcase.UnrealBackend/Backend.hpp"

#include <Helpers/String.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Core/Containers/ScriptArray.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FObjectProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UEnum.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/World.hpp>
#include "../../ue4ss/ServerEquipmentIds.hpp"
#include "../../ue4ss/ServerBridgeProtocol.hpp"
#include "../../ue4ss/ServerBridgeSnapshot.hpp"
#include "../../ue4ss/ServerBridgeChannel.hpp"
#include <nlohmann/json.hpp>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace bc {
namespace {
using namespace RC::Unreal;
using Json = nlohmann::json;
namespace fs = std::filesystem;

struct Player {
    std::string token;
    FWeakObjectPtr state;
    FWeakObjectPtr controller;
    std::string account_id, bandit_id, platform_id, platform_type;
};
struct PendingPack {
    FWeakObjectPtr spy;
    FWeakObjectPtr world;
    std::vector<FWeakObjectPtr> cached_packs;
    std::chrono::steady_clock::time_point expires;
};
using Command = briefcase::server::BridgeCommand;

std::atomic<bool> active{};
briefcase::server::BridgeCommandChannel channel;
briefcase::server::BridgeSnapshot snapshots;
std::unordered_map<UObject*, Player> players;
std::vector<PendingPack> pending_packs;
UClass* spy_pack_class{};
UFunction* destroy_actor_function{};
std::atomic<std::int64_t> next_extension_ms{};
std::uint64_t next_token{};
std::jthread worker;
fs::path socket_path;
std::atomic<std::int64_t> next_collect_ms{};
UFunction* backend_init_function{};
UFunction* player_infos_function{};

bool valid(UObject* object) {
    return object && !object->IsUnreachable() &&
           UObjectArray::IsValid(object->GetObjectItem(), false);
}

UObject* object_field(UObject* object, const RC::CharType* name) {
    if (!valid(object)) return nullptr;
    auto* property = object->GetPropertyByNameInChain(name);
    if (!property || property->GetClass().GetName() != STR("ObjectProperty") ||
        property->GetElementSize() != sizeof(UObject*)) return nullptr;
    return *property->ContainerPtrToValuePtr<UObject*>(object);
}

bool flag(UObject* object, const RC::CharType* name) {
    if (!valid(object)) return false;
    auto* property = object->GetPropertyByNameInChain(name);
    return property && property->GetClass().GetName() == STR("BoolProperty") &&
           static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(object);
}

std::string text_field(UObject* object, const RC::CharType* name) {
    if (!valid(object)) return {};
    auto* property = object->GetPropertyByNameInChain(name);
    if (!property || property->GetClass().GetName() != STR("StrProperty")) return {};
    const auto* value = property->ContainerPtrToValuePtr<FString>(object);
    const auto& chars = value->GetCharArray();
    const auto length = chars.Num() - 1;
    if (length < 1 || length > 256 || !chars.GetData()) return {};
    return RC::to_utf8_string(RC::StringViewType(chars.GetData(), length));
}

bool connected(UObject* state) {
    if (!valid(state) || flag(state, STR("bIsInactive")) || flag(state, STR("bIsABot")))
        return false;
    auto* controller = object_field(state, STR("Owner"));
    return valid(controller) && valid(object_field(controller, STR("NetConnection")));
}

std::optional<int> team_of(UObject* state) {
    if (!valid(state)) return std::nullopt;
    auto* property = state->GetPropertyByNameInChain(STR("FactionID"));
    if (!property || property->GetClass().GetName() != STR("ByteProperty") ||
        property->GetElementSize() != 1) return std::nullopt;
    int value = *property->ContainerPtrToValuePtr<std::uint8_t>(state);
    return value < 32 ? std::optional<int>(value) : std::nullopt;
}

std::string platform_of(UObject* state) {
    auto* property = state->GetPropertyByNameInChain(STR("PlatformType"));
    if (!property || property->GetClass().GetName() != STR("EnumProperty") ||
        property->GetElementSize() != 1) return "Unknown";
    switch (*property->ContainerPtrToValuePtr<std::uint8_t>(state)) {
        case 0: return "PC";
        case 1: return "Xbox";
        case 2: return "PlayStation";
        default: return "Unknown";
    }
}

std::string string_field(const void* base, FProperty* property) {
    if (!base || !property || property->GetClass().GetName() != STR("StrProperty")) return {};
    const auto* value = property->ContainerPtrToValuePtr<FString>(base);
    const auto& chars = value->GetCharArray();
    const auto length = chars.Num() - 1;
    if (length < 1 || length > 256 || !chars.GetData()) return {};
    return RC::to_utf8_string(RC::StringViewType(chars.GetData(), length));
}

const void* struct_field(const void* base, FProperty* property, const RC::CharType* expected) {
    if (!base || !property || property->GetClass().GetName() != STR("StructProperty")) return nullptr;
    auto* structure = static_cast<FStructProperty*>(property)->GetStruct().Get();
    return structure && structure->GetPathName() == expected
        ? property->ContainerPtrToValuePtr<void>(base) : nullptr;
}

std::optional<int> level_field(const void* base, FProperty* property) {
    if (!base || !property || property->GetClass().GetName() != STR("IntProperty") ||
        property->GetElementSize() != sizeof(std::int32_t)) return std::nullopt;
    auto value = *property->ContainerPtrToValuePtr<std::int32_t>(base);
    return value >= 1 && value <= 10000 ? std::optional<int>(value) : std::nullopt;
}

std::string player_network_id(UObject* player) {
    auto* property = player->GetPropertyByNameInChain(STR("UniqueId"));
    if (!property || property->GetClass().GetName() != STR("StructProperty")) return {};
    FString exported;
    property->ExportTextItem(exported, property->ContainerPtrToValuePtr<void>(player),
        nullptr, player, 0);
    const auto& chars = exported.GetCharArray();
    const auto length = chars.Num() - 1;
    std::string id;
    if (length > 0 && length <= 256 && chars.GetData())
        id = RC::to_utf8_string(RC::StringViewType(chars.GetData(), length));
    exported.Clear();
    if (id.empty() || id == "()" || id == "INVALID") return {};
    return id;
}

std::string spectator_identity(const Player& entry, UObject* player) {
    auto network = valid(player) ? player_network_id(player) : std::string{};
    if (!network.empty()) return "network:" + network;
    return entry.account_id.empty() ? std::string{} : "account:" + entry.account_id;
}

const BriefcaseLinuxSpectatorApiV1* spectator_extension() {
    auto* symbol = linux_loaded_mod_symbol("briefcase.spectator-mode",
        "briefcase_linux_spectator_api_v1");
    auto get_api = reinterpret_cast<BriefcaseGetLinuxSpectatorApiV1>(symbol);
    const auto* api = get_api ? get_api() : nullptr;
    return api && api->version == 1 && api->ready && api->ready() &&
        api->set_selected && api->is_selected && api->forget_token && api->any_selected
        ? api : nullptr;
}

std::string primary_asset_name(const void* base, FProperty* property) {
    const auto* id = struct_field(base, property, STR("/Script/CoreUObject.PrimaryAssetId"));
    if (!id) return {};
    auto* id_type = static_cast<FStructProperty*>(property)->GetStruct().Get();
    auto* name_property = id_type->GetPropertyByNameInChain(STR("PrimaryAssetName"));
    if (!name_property || name_property->GetClass().GetName() != STR("NameProperty") ||
        name_property->GetElementSize() != sizeof(FName)) return {};
    return RC::to_utf8_string(name_property->ContainerPtrToValuePtr<FName>(id)->ToString());
}

const void* selection_info(UObject* player, UScriptStruct*& type) {
    auto* selection_property = player->GetPropertyByNameInChain(STR("AgentSelection"));
    const auto* selection = struct_field(player, selection_property,
        STR("/Script/DeceiveInc.PlayerAgentSelectionInfo"));
    if (!selection) return nullptr;
    auto* selection_type = static_cast<FStructProperty*>(selection_property)->GetStruct().Get();
    auto* info_property = selection_type->GetPropertyByNameInChain(STR("SelectionInfo"));
    const auto* info = struct_field(selection, info_property,
        STR("/Script/DeceiveInc.DISerializedAgentSelectionInfo"));
    type = info ? static_cast<FStructProperty*>(info_property)->GetStruct().Get() : nullptr;
    return info;
}

std::string selected_agent(UObject* player) {
    auto* selection_property = player->GetPropertyByNameInChain(STR("AgentSelection"));
    const auto* selection = struct_field(player, selection_property,
        STR("/Script/DeceiveInc.PlayerAgentSelectionInfo"));
    if (selection) {
        auto* type = static_cast<FStructProperty*>(selection_property)->GetStruct().Get();
        auto slug = briefcase::server::agent_id(primary_asset_name(selection,
            type->GetPropertyByNameInChain(STR("AgentId"))));
        if (!slug.empty()) return slug;
    }
    auto* spy = object_field(player, STR("OwnedSpy"));
    auto* agent = valid(spy) ? object_field(spy, STR("AgentData")) : nullptr;
    return valid(agent) ? briefcase::server::agent_id(RC::to_utf8_string(agent->GetName())) : std::string{};
}

std::optional<int> selection_variant(const void* info, UScriptStruct* type,
                                     const RC::CharType* field, std::string_view prefix,
                                     std::string_view agent) {
    if (!info || !type || agent.empty()) return std::nullopt;
    const auto name = primary_asset_name(info, type->GetPropertyByNameInChain(field));
    return briefcase::server::selection_variant_id(name, prefix, agent);
}

Json selected_loadout(UObject* player, std::string_view agent) {
    Json result = Json::object();
    UScriptStruct* type{};
    const auto* info = selection_info(player, type);
    for (const auto& [key, field, prefix] : {
             std::tuple{"weapon", STR("WeaponVariant"), "DA_AgentWeaponData_"},
             std::tuple{"expertise", STR("ActiveVariant"), "DA_AgentExpertiseData_"},
             std::tuple{"passive", STR("PassiveVariant"), "DA_AgentPassiveData_"}}) {
        if (auto variant = selection_variant(info, type, field, prefix, agent))
            result[key] = *variant;
    }
    return result;
}

Json selected_gadgets(UObject* player) {
    Json result = Json::array();
    UScriptStruct* type{};
    const auto* info = selection_info(player, type);
    if (!info || !type) return result;
    for (const auto* field : {STR("Gadget1"), STR("Gadget2")}) {
        auto name = briefcase::server::gadget_asset_id(
            primary_asset_name(info, type->GetPropertyByNameInChain(field)));
        if (!name.empty()) result.push_back(std::move(name));
    }
    return result;
}

const void* player_progression(UObject* player, UScriptStruct*& type) {
    auto* property = player->GetPropertyByNameInChain(STR("PlayerProgression"));
    const auto* data = struct_field(player, property, STR("/Script/DeceiveInc.PlayerProgression"));
    type = data ? static_cast<FStructProperty*>(property)->GetStruct().Get() : nullptr;
    return data;
}

std::optional<int> selected_agent_level(const void* progression, UScriptStruct* type,
                                        std::string_view agent) {
    if (!progression || !type || agent.empty()) return std::nullopt;
    auto* property = type->GetPropertyByNameInChain(STR("PlayerXpTypes"));
    if (!property || property->GetClass().GetName() != STR("ArrayProperty")) return std::nullopt;
    auto* inner = static_cast<FArrayProperty*>(property)->GetInner();
    if (!inner || inner->GetClass().GetName() != STR("StructProperty") ||
        inner->GetElementSize() < 24 || inner->GetElementSize() > 256) return std::nullopt;
    auto* entry_type = static_cast<FStructProperty*>(inner)->GetStruct().Get();
    if (!entry_type || entry_type->GetPathName() != STR("/Script/DeceiveInc.PlayerXpTypeDefinition"))
        return std::nullopt;
    const auto* array = property->ContainerPtrToValuePtr<FScriptArray>(progression);
    if (!array || array->Num() < 0 || array->Num() > 128 || array->Max() < array->Num() ||
        array->Max() > 1024 || (array->Num() && !array->GetData())) return std::nullopt;
    auto* xp_type = entry_type->GetPropertyByNameInChain(STR("XpType"));
    auto* level = entry_type->GetPropertyByNameInChain(STR("Level"));
    std::string wanted(agent);
    std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    for (int i = 0; i < array->Num(); ++i) {
        const auto* entry = static_cast<const std::byte*>(array->GetData()) +
            i * inner->GetElementSize();
        auto category = string_field(entry, xp_type);
        std::transform(category.begin(), category.end(), category.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (category == wanted || category == "agent_" + wanted ||
            category == "agent" + wanted || category == "da_agentdata_" + wanted)
            return level_field(entry, level);
    }
    return std::nullopt;
}

std::optional<float> health_value(UObject* component, const RC::CharType* function_name) {
    if (!valid(component)) return std::nullopt;
    auto* function = component->GetFunctionByNameInChain(function_name);
    auto* result = function ? function->GetPropertyByNameInChain(STR("ReturnValue")) : nullptr;
    if (!function || function->GetParmsSize() != sizeof(float) || !result ||
        result->GetClass().GetName() != STR("FloatProperty") ||
        result->GetOffset_Internal() != 0) return std::nullopt;
    float value{};
    component->ProcessEvent(function, &value);
    return std::isfinite(value) && value >= 0 && value <= 10000
        ? std::optional<float>(value) : std::nullopt;
}

std::string powerup_kind(UEnum* enumeration, std::uint8_t value) {
    if (!enumeration) return {};
    std::string name;
    for (const auto pair : enumeration->ForEachName()) {
        if (pair.Value == value) {
            name = RC::to_utf8_string(pair.Key.ToString());
            break;
        }
    }
    return briefcase::server::powerup_id(std::move(name));
}

bool owns_powerup(UObject* spy, std::uint8_t kind, std::uint8_t tier) {
    if (!valid(spy)) return false;
    auto* function = spy->GetFunctionByNameInChain(STR("GetPowerupLevel"));
    if (!function || function->GetParmsSize() != 3) return false;
    auto* powerup = function->GetPropertyByNameInChain(STR("Powerup"));
    auto* level = function->GetPropertyByNameInChain(STR("Level"));
    auto* available = function->GetPropertyByNameInChain(STR("bAvailable"));
    if (!powerup || !level || !available ||
        powerup->GetClass().GetName() != STR("EnumProperty") ||
        level->GetClass().GetName() != STR("EnumProperty") ||
        available->GetClass().GetName() != STR("BoolProperty") ||
        powerup->GetElementSize() != 1 || level->GetElementSize() != 1 ||
        powerup->GetOffset_Internal() != 0 || level->GetOffset_Internal() != 1 ||
        available->GetOffset_Internal() != 2) return false;
    std::uint8_t params[3]{kind, 0, 0};
    spy->ProcessEvent(function, params);
    return static_cast<FBoolProperty*>(available)->GetPropertyValueInContainer(params) &&
        params[1] == tier;
}

Json selected_upgrades(UObject* player) {
    Json result = Json::array();
    auto* deck_property = player->GetPropertyByNameInChain(STR("EquippedDeck"));
    const auto* deck = struct_field(player, deck_property,
        STR("/Script/DeceiveInc.DISerializedDeckEntry"));
    if (!deck) return result;
    auto* type = static_cast<FStructProperty*>(deck_property)->GetStruct().Get();
    auto* property = type->GetPropertyByNameInChain(STR("EquippedPowerups"));
    if (!property || property->GetClass().GetName() != STR("ArrayProperty")) return result;
    auto* inner = static_cast<FArrayProperty*>(property)->GetInner();
    if (!inner || inner->GetClass().GetName() != STR("EnumProperty") ||
        inner->GetElementSize() != 1) return result;
    const auto* array = property->ContainerPtrToValuePtr<FScriptArray>(deck);
    if (!array || array->Num() < 0 || array->Num() > 5 || array->Max() < array->Num() ||
        array->Max() > 32 || (array->Num() && !array->GetData())) return result;
    auto* enumeration = UObjectGlobals::StaticFindObject<UEnum*>(nullptr, nullptr,
        STR("/Script/DeceiveInc.EPowerupType"));
    constexpr const char* tiers[]{"Civilian", "Staff", "Guard", "Technician", "VIP"};
    auto* spy = object_field(player, STR("OwnedSpy"));
    const auto* values = static_cast<const std::uint8_t*>(array->GetData());
    for (int i = 0; i < array->Num(); ++i) {
        auto kind = powerup_kind(enumeration, values[i]);
        if (!kind.empty()) result.push_back({{"tier", tiers[i]}, {"kind", kind},
                                             {"owned", owns_powerup(spy, values[i],
                                                 static_cast<std::uint8_t>(i))}});
    }
    return result;
}

Player& connection_entry(UObject* controller) {
    auto [it, inserted] = players.try_emplace(controller);
    if (inserted || it->second.controller.Get() != controller) {
        it->second = Player{};
        it->second.token = "p" + std::to_string(++next_token);
        it->second.controller = controller;
    }
    return it->second;
}

void capture_identity(UObject* controller, UFunction* function, void* params) {
    if (!valid(controller) || !params || !function) return;
    auto* composite = function->GetPropertyByNameInChain(STR("UniqueNetId"));
    if (!composite || composite->GetClass().GetName() != STR("StructProperty") ||
        composite->GetOffset_Internal() != 0 || function->GetParmsSize() < 0x30) return;
    auto* structure = static_cast<FStructProperty*>(composite)->GetStruct().Get();
    if (!structure || structure->GetPathName() != STR("/Script/DeceiveInc.BackendUniqueIdComposite"))
        return;
    const auto* data = composite->ContainerPtrToValuePtr<void>(params);
    auto id = string_field(data, structure->GetPropertyByNameInChain(STR("ID")));
    auto platform_id = string_field(data, structure->GetPropertyByNameInChain(STR("PlatformId")));
    auto platform_type = string_field(data, structure->GetPropertyByNameInChain(STR("PlatformType")));
    if (id.empty() && platform_id.empty()) return;
    auto& entry = connection_entry(controller);
    entry.account_id = std::move(id);
    entry.platform_id = std::move(platform_id);
    entry.platform_type = std::move(platform_type);
}

void capture_bandit_identity(UObject* controller, UFunction* function, void* params) {
    if (!valid(controller) || !params || !function) return;
    auto* property = function->GetPropertyByNameInChain(STR("BanditIdIn"));
    if (!property || property->GetClass().GetName() != STR("StrProperty") ||
        property->GetOffset_Internal() != 0 || property->GetElementSize() != sizeof(FString) ||
        function->GetParmsSize() < sizeof(FString)) return;
    auto id = string_field(params, property);
    if (!id.empty()) connection_entry(controller).bandit_id = std::move(id);
}

UObject* match_state() {
    auto* type = UObjectGlobals::StaticFindObject<UClass*>(
        nullptr, nullptr, STR("/Script/DeceiveInc.DeceiveIncMatchGameState"));
    if (!type) return nullptr;
    UObject* found{};
    UObjectGlobals::ForEachUObject([&](UObject* object, std::int32_t, std::int32_t) {
        if (valid(object) && object->IsA(type) &&
            !object->HasAnyFlags(static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject)) &&
            object->GetWorld()) {
            found = object;
            return RC::LoopAction::Break;
        }
        return RC::LoopAction::Continue;
    });
    return found;
}

bool in_lobby(UObject* state) {
    if (!valid(state)) return false;
    auto* property = state->GetPropertyByNameInChain(STR("GamePhase"));
    auto* enumeration = UObjectGlobals::StaticFindObject<UEnum*>(
        nullptr, nullptr, STR("/Script/DeceiveInc.ESpyGamePhase"));
    if (!property || property->GetSize() != 1 || !enumeration) return false;
    for (const auto pair : enumeration->ForEachName()) {
        const auto name = pair.Key.ToString();
        if (name == STR("PREGAME") || name.ends_with(STR("::PREGAME")))
            return *property->ContainerPtrToValuePtr<std::uint8_t>(state) == pair.Value;
    }
    return false;
}

bool active_match_phase(UObject* state) {
    if (!valid(state)) return false;
    auto* property = state->GetPropertyByNameInChain(STR("GamePhase"));
    auto* enumeration = UObjectGlobals::StaticFindObject<UEnum*>(
        nullptr, nullptr, STR("/Script/DeceiveInc.ESpyGamePhase"));
    if (!property || property->GetSize() != 1 || !enumeration) return false;
    const auto phase = *property->ContainerPtrToValuePtr<std::uint8_t>(state);
    for (const auto pair : enumeration->ForEachName()) {
        if (pair.Value != phase) continue;
        const auto name = pair.Key.ToString();
        return name.ends_with(STR("VAULT_LOCKED")) ||
            name.ends_with(STR("VAULT_UNLOCKED")) ||
            name.ends_with(STR("EXTRACTION_ARRIVING")) ||
            name.ends_with(STR("EXTRACTION_ARRIVED"));
    }
    return false;
}

bool is_spy_pack(UObject* object) {
    return valid(object) && spy_pack_class && object->IsA(spy_pack_class);
}

std::vector<FWeakObjectPtr> cached_packs_for(UObject* spy, UObject* world) {
    std::vector<FWeakObjectPtr> result;
    const auto prefix = spy->GetName() + STR("_");
    UObjectGlobals::ForEachUObject([&](UObject* object, std::int32_t, std::int32_t) {
        if (is_spy_pack(object) && object->GetWorld() == world &&
            object->GetName().starts_with(prefix) &&
            flag(object, STR("bIsCached")) && flag(object, STR("bHiddenInContainer"))) {
            FWeakObjectPtr weak;
            weak = object;
            result.push_back(weak);
        }
        return RC::LoopAction::Continue;
    });
    return result;
}

std::size_t remove_activated_packs(PendingPack& pending) {
    auto* spy = pending.spy.Get();
    if (!valid(spy) || !valid(pending.world.Get()) || !destroy_actor_function) return 0;
    std::size_t removed{};
    for (const auto& weak : pending.cached_packs) {
        auto* pack = weak.Get();
        if (!is_spy_pack(pack) || pack->GetWorld() != pending.world.Get() ||
            !pack->GetName().starts_with(spy->GetName() + STR("_")) ||
            flag(pack, STR("bIsCached")) || flag(pack, STR("bHiddenInContainer"))) continue;
        pack->ProcessEvent(destroy_actor_function, nullptr);
        ++removed;
    }
    return removed;
}

bool apply_fatal_damage(UObject* spy) {
    auto* gameplay = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr,
        STR("/Script/Engine.Default__GameplayStatics"));
    auto* function = valid(gameplay) ? gameplay->GetFunctionByNameInChain(STR("ApplyDamage")) : nullptr;
    if (!function || (function->GetParmsSize() != 0x2C && function->GetParmsSize() != 0x30))
        return false;
    const auto parameter = [&](const RC::CharType* name, const RC::CharType* type, int offset) {
        auto* property = function->GetPropertyByNameInChain(name);
        return property && property->GetClass().GetName() == type &&
            property->GetOffset_Internal() == offset;
    };
    if (!parameter(STR("DamagedActor"), STR("ObjectProperty"), 0) ||
        !parameter(STR("BaseDamage"), STR("FloatProperty"), 8) ||
        !parameter(STR("EventInstigator"), STR("ObjectProperty"), 0x10) ||
        !parameter(STR("DamageCauser"), STR("ObjectProperty"), 0x18) ||
        !parameter(STR("DamageTypeClass"), STR("ClassProperty"), 0x20) ||
        !parameter(STR("ReturnValue"), STR("FloatProperty"), 0x28)) return false;
    struct alignas(8) Params {
        UObject* damaged_actor{};
        float damage{};
        std::uint32_t padding{};
        UObject* instigator{};
        UObject* causer{};
        UClass* damage_class{};
        float result{};
    } params{};
    static_assert(sizeof(Params) == 0x30);
    params.damaged_actor = spy;
    params.damage = 1'000'000.0f;
    gameplay->ProcessEvent(function, &params);
    return std::isfinite(params.result) && params.result > 0;
}

void tick_spectator(UObject* player) {
    if (!valid(player) || flag(player, STR("bIsDead"))) return;
    auto* spy = object_field(player, STR("OwnedSpy"));
    auto* health = object_field(spy, STR("HealthComponent"));
    if (!valid(spy) || !valid(health) || flag(health, STR("bIgnoreDamage"))) return;
    auto* world = spy->GetWorld();
    if (!valid(world)) return;
    PendingPack pending{};
    pending.spy = spy;
    pending.world = world;
    pending.cached_packs = cached_packs_for(spy, world);
    pending.expires = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    if (!apply_fatal_damage(spy)) return;
    const auto removed = remove_activated_packs(pending);
    log("Linux spectator elimination removed " + std::to_string(removed) + " Spy Pack(s)");
    if (!removed) pending_packs.push_back(std::move(pending));
}

void tick_spectators() {
    auto now = std::chrono::steady_clock::now();
    std::erase_if(pending_packs, [now](PendingPack& pending) {
        if (!valid(pending.spy.Get()) || !valid(pending.world.Get())) return true;
        return remove_activated_packs(pending) || now >= pending.expires;
    });
    const auto* spectator = spectator_extension();
    if (!spectator || !spectator->any_selected() || !active_match_phase(match_state())) return;
    for (const auto& [controller, entry] : players) {
        auto* player = entry.state.Get();
        if (!valid(controller) || !connected(player) ||
            object_field(player, STR("Owner")) != controller) continue;
        const auto identity = spectator_identity(entry, player);
        if (spectator->is_selected(entry.token.c_str(), identity.c_str()))
            tick_spectator(player);
    }
}

void reply(const Command& command, bool ok, std::string_view message) {
    channel.reply(command, ok, message);
}

bool call_team(UObject* controller, bool join, int index = 0) {
    auto* function = controller->GetFunctionByNameInChain(
        join ? STR("ServerJoinTeam") : STR("ServerLeaveTeam"));
    if (!function) return false;
    if (!join) {
        if (function->GetParmsSize() != 0) return false;
        controller->ProcessEvent(function, nullptr);
        return true;
    }
    auto* parameter = function->GetPropertyByNameInChain(STR("TeamIndex"));
    if (function->GetParmsSize() != sizeof(std::int32_t) || !parameter ||
        parameter->GetClass().GetName() != STR("IntProperty") ||
        parameter->GetOffset_Internal() != 0) return false;
    std::int32_t value = index;
    controller->ProcessEvent(function, &value);
    return true;
}

void process_commands() {
    auto pending = channel.take_commands();
    for (const auto& command : pending) {
        if (!channel.is_current(command)) continue;
        auto found = std::find_if(players.begin(), players.end(), [&](const auto& row) {
            return row.second.token == command.player_token;
        });
        if (found == players.end() || !connected(found->second.state.Get()) ||
            object_field(found->second.state.Get(), STR("Owner")) != found->second.controller.Get()) {
            reply(command, false, "Player is no longer connected");
            continue;
        }
        auto* controller = found->second.controller.Get();
        if (command.kind == Command::Kind::Kick) {
            auto* function = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
                STR("/Script/Engine.PlayerController:ClientReturnToMainMenu"));
            auto* reason = function ? function->GetPropertyByNameInChain(STR("ReturnReason")) : nullptr;
            if (!function || function->GetParmsSize() != sizeof(FString) || !reason ||
                reason->GetClass().GetName() != STR("StrProperty") ||
                reason->GetOffset_Internal() != 0) {
                reply(command, false, "Kick is unavailable in this game build");
                continue;
            }
            FString reason_text(STR("Removed by the server administrator."));
            controller->ProcessEvent(function, &reason_text);
            reason_text.Clear();
            reply(command, true, "Kick sent to player");
            continue;
        }
        if (command.kind == Command::Kind::SetSpectator) {
            if (!in_lobby(match_state())) {
                reply(command, false, "Spectator selection is only available in the lobby");
                continue;
            }
            const auto* spectator = spectator_extension();
            if (!spectator || !spy_pack_class || !destroy_actor_function) {
                reply(command, false, "Spectator Mode is not installed or ready");
                continue;
            }
            auto* player = found->second.state.Get();
            const auto identity = spectator_identity(found->second, player);
            if (!spectator->set_selected(found->second.token.c_str(), identity.c_str(),
                                         command.spectator)) {
                reply(command, false, "Spectator selection failed");
                continue;
            }
            next_collect_ms.store(0);
            reply(command, true, command.spectator ? "Spectator selected" : "Spectator selection removed");
            continue;
        }
        if (command.kind != Command::Kind::MoveTeam &&
            command.kind != Command::Kind::SwapTeam) {
            reply(command, false, "This command is not available on Linux");
            continue;
        }
        auto* state = match_state();
        if (!in_lobby(state) || command.team_size < 2 || command.team_size > 3) {
            reply(command, false, "Teams can only be changed in a Duo or Trio lobby");
            continue;
        }
        auto* player = found->second.state.Get();
        auto original = team_of(player);
        if (!original) {
            reply(command, false, "Player has no assigned team");
            continue;
        }
        Player* other{};
        int target = command.team_index;
        if (command.kind == Command::Kind::SwapTeam) {
            auto second = std::find_if(players.begin(), players.end(), [&](const auto& row) {
                return row.second.token == command.other_token;
            });
            if (second == players.end() || !connected(second->second.state.Get())) {
                reply(command, false, "Other player is no longer connected");
                continue;
            }
            other = &second->second;
            target = team_of(other->state.Get()).value_or(-1);
        }
        if (target < 0 || target >= 32 || target == *original) {
            reply(command, false, "Invalid target team");
            continue;
        }
        try {
            if (other && !call_team(other->controller.Get(), false)) {
                reply(command, false, "Team controls unavailable");
                continue;
            }
            if (!call_team(controller, false) || !call_team(controller, true, target) ||
                team_of(player) != target) {
                call_team(controller, true, *original);
                if (other) call_team(other->controller.Get(), true, target);
                reply(command, false, "Server rejected team change");
                continue;
            }
            if (other && (!call_team(other->controller.Get(), true, *original) ||
                          team_of(other->state.Get()) != *original)) {
                call_team(controller, false);
                call_team(controller, true, *original);
                call_team(other->controller.Get(), true, target);
                reply(command, false, "Server rejected team swap");
                continue;
            }
            next_collect_ms.store(0);
            reply(command, true, other ? "Players swapped" : "Player moved to team");
        } catch (...) {
            reply(command, false, "Team change failed in Unreal");
        }
    }
}

void collect() {
    auto* state = match_state();
    if (!state) return;
    auto* property = state->GetPropertyByNameInChain(STR("PlayerArray"));
    if (!property || property->GetClass().GetName() != STR("ArrayProperty")) return;
    auto* array = static_cast<FArrayProperty*>(property);
    if (!array->GetInner() || array->GetInner()->GetClass().GetName() != STR("ObjectProperty") ||
        array->GetInner()->GetElementSize() != sizeof(UObject*)) return;
    auto* values = array->ContainerPtrToValuePtr<FScriptArray>(state);
    if (!values || values->Num() < 0 || values->Num() > 128 ||
        values->Max() < values->Num() || values->Max() > 1024 ||
        (values->Num() && !values->GetData())) return;

    auto rows = Json::array();
    const auto* spectator = spectator_extension();
    const bool spectator_ready = spectator && spy_pack_class && destroy_actor_function;
    std::unordered_set<UObject*> seen;
    auto* data = reinterpret_cast<UObject* const*>(values->GetData());
    for (int i = 0; i < values->Num(); ++i) {
        auto* state_player = data[i];
        if (!connected(state_player)) continue;
        auto name = text_field(state_player, STR("PlayerNamePrivate"));
        if (name.empty() || name.size() > 128) continue;
        auto* controller = object_field(state_player, STR("Owner"));
        seen.insert(controller);
        auto& entry = connection_entry(controller);
        entry.state = state_player;
        auto agent = selected_agent(state_player);
        UScriptStruct* progression_type{};
        const auto* progression = player_progression(state_player, progression_type);
        auto account_level = progression
            ? level_field(progression, progression_type->GetPropertyByNameInChain(STR("Level")))
            : std::nullopt;
        auto agent_level = selected_agent_level(progression, progression_type, agent);
        Json row{{"name", name}, {"token", entry.token},
                 {"platform", entry.platform_type.empty() ? platform_of(state_player)
                     : entry.platform_type},
                 {"networkId", entry.account_id.empty() ? player_network_id(state_player)
                     : entry.account_id},
                 {"platformId", entry.platform_id}, {"banditId", entry.bandit_id},
                 {"dead", flag(state_player, STR("bIsDead"))}};
        if (spectator_ready) {
            const auto identity = spectator_identity(entry, state_player);
            row["spectator"] = spectator->is_selected(entry.token.c_str(), identity.c_str());
        }
        if (!agent.empty()) row["character"] = agent == "Socialite" ? "Red" : agent;
        if (account_level) row["accountLevel"] = *account_level;
        if (agent_level) row["characterLevel"] = *agent_level;
        if (auto team = team_of(state_player)) row["team"] = *team;
        if (auto* spy = object_field(state_player, STR("OwnedSpy")); valid(spy)) {
            if (auto* health = object_field(spy, STR("HealthComponent")); valid(health)) {
                auto current = health_value(health, STR("GetHealth"));
                auto maximum = health_value(health, STR("GetMaxHealth"));
                if (current && maximum && *maximum > 0) {
                    row["health"] = *current;
                    row["maxHealth"] = *maximum;
                }
            }
        }
        row["gadgets"] = selected_gadgets(state_player);
        row["loadout"] = selected_loadout(state_player, agent);
        row["upgrades"] = selected_upgrades(state_player);
        rows.push_back(std::move(row));
    }
    for (auto it = players.begin(); it != players.end();) {
        if (!seen.contains(it->first)) {
            if (spectator_ready) spectator->forget_token(it->second.token.c_str());
            it = players.erase(it);
        }
        else ++it;
    }
    std::string map;
    if (auto* world = state->GetWorld())
        map = RC::to_utf8_string(world->GetName());
    Json value{{"schemaVersion", 2}, {"type", "snapshot"},
               {"processId", getpid()}, {"state", in_lobby(state) ? "Lobby" : "In match"},
               {"map", map}, {"players", std::move(rows)},
               {"capabilities", {{"spectatorMode", spectator_ready}}}};
    if (auto* id = std::getenv("BRIEFCASE_INSTANCE_ID"); id && std::strlen(id) == 32)
        value["instanceId"] = id;
    snapshots.publish(value.dump() + '\n');
}

void on_event(UObject* context, UFunction* function, void* params) {
    if (!active.load(std::memory_order_relaxed) || !function) return;
    if (function == backend_init_function) {
        try { capture_identity(context, function, params); }
        catch (...) { /* Identity is optional; never obstruct player login. */ }
    }
    if (function == player_infos_function) {
        try { capture_bandit_identity(context, function, params); }
        catch (...) { /* Identity is optional; never obstruct player login. */ }
    }
    if (function->GetName() != STR("ReceiveTick")) return;
    try {
        process_commands();
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now >= next_extension_ms.load(std::memory_order_relaxed)) {
            next_extension_ms.store(now + 250, std::memory_order_relaxed);
            tick_spectators();
        }
        if (now >= next_collect_ms.load(std::memory_order_relaxed)) {
            next_collect_ms.store(now + 1000, std::memory_order_relaxed);
            collect();
        }
    } catch (...) { /* Map travel may invalidate reflected objects. */ }
}

void initialize(void*) noexcept {
    if (!active.load()) return;
    try {
        backend_init_function = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
            STR("/Script/DeceiveInc.DeceiveIncPlayerController:ServerInitBackend"));
        player_infos_function = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
            STR("/Script/DeceiveInc.DeceiveIncPlayerController:ServerInitPlayerInfos"));
        spy_pack_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
            STR("/Script/DeceiveInc.SpyPack"));
        destroy_actor_function = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
            STR("/Script/Engine.Actor:K2_DestroyActor"));
        if (!destroy_actor_function || destroy_actor_function->GetParmsSize() != 0)
            destroy_actor_function = nullptr;
        Hook::RegisterProcessEventPreCallback(on_event);
        log("Linux server bridge: Unreal state collector ready");
    } catch (...) { log("ERROR: Linux server bridge collector could not start"); }
}

void receive(std::string_view line) {
    channel.receive(line, briefcase::server::BridgeCommandSet::LocalBasic);
}

bool send_message(int fd, const std::string& message, std::stop_token stop) {
    std::size_t offset{};
    while (offset < message.size() && !stop.stop_requested()) {
        auto count = ::send(fd, message.data() + offset, message.size() - offset,
                            MSG_NOSIGNAL | MSG_DONTWAIT);
        if (count > 0) { offset += static_cast<std::size_t>(count); continue; }
        if (count < 0 && (errno == EAGAIN || errno == EINTR)) {
            pollfd wait{fd, POLLOUT, 0};
            if (poll(&wait, 1, 200) > 0) continue;
        }
        return false;
    }
    return offset == message.size();
}

void run(std::stop_token stop) {
    int server = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server < 0) { log("ERROR: Linux server bridge socket creation failed"); return; }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    auto path = socket_path.string();
    if (path.size() >= sizeof(address.sun_path)) { close(server); return; }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    std::error_code error;
    fs::remove(socket_path, error);
    mode_t previous = umask(0077);
    int bound = ::bind(server, reinterpret_cast<sockaddr*>(&address),
                       offsetof(sockaddr_un, sun_path) + path.size() + 1);
    umask(previous);
    if (bound != 0 || chmod(path.c_str(), 0600) != 0 || listen(server, 1) != 0) {
        log("ERROR: Linux server bridge socket bind failed");
        close(server);
        fs::remove(socket_path, error);
        return;
    }
    log("Linux server bridge: local socket ready");
    while (!stop.stop_requested()) {
        pollfd incoming{server, POLLIN, 0};
        if (poll(&incoming, 1, 200) <= 0) continue;
        int client = accept4(server, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (client < 0) continue;
        ucred credentials{};
        socklen_t length = sizeof(credentials);
        if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0 ||
            credentials.uid != geteuid()) { close(client); continue; }
        channel.reset_session();
        std::uint64_t sent{};
        auto heartbeat = std::chrono::steady_clock::time_point{};
        std::string buffered;
        while (!stop.stop_requested()) {
            pollfd ready{client, POLLIN, 0};
            int status = poll(&ready, 1, 100);
            if (status < 0 || (status > 0 && (ready.revents & (POLLHUP | POLLERR | POLLNVAL)))) break;
            if (status > 0 && (ready.revents & POLLIN)) {
                char chunk[4096];
                auto count = recv(client, chunk, sizeof(chunk), 0);
                if (count <= 0) break;
                buffered.append(chunk, static_cast<std::size_t>(count));
                if (buffered.size() > 4096) break;
                for (auto end = buffered.find('\n'); end != std::string::npos;
                     end = buffered.find('\n')) {
                    receive(std::string_view(buffered).substr(0, end));
                    buffered.erase(0, end + 1);
                }
            }
            auto outgoing = channel.take_responses();
            auto current = snapshots.next(sent, heartbeat);
            bool okay = true;
            for (const auto& response : outgoing)
                if (!send_message(client, response, stop)) { okay = false; break; }
            if (!okay || (!current.empty() && !send_message(client, current, stop))) break;
        }
        close(client);
        channel.reset_session();
    }
    close(server);
    fs::remove(socket_path, error);
}
} // namespace

void linux_server_bridge_start() noexcept {
    if (active.exchange(true)) return;
    try {
        const auto* instance = std::getenv("BRIEFCASE_INSTANCE_ROOT");
        auto root = instance && *instance ? fs::path(instance) :
            fs::read_symlink("/proc/self/exe").parent_path();
        auto directory = root / "Briefcase" / "Run";
        fs::create_directories(directory);
        fs::permissions(directory, fs::perms::owner_all, fs::perm_options::replace);
        socket_path = directory / ("server-" + std::to_string(getpid()) + ".sock");
        if (backend_post(initialize, nullptr) != BC_OK)
            throw std::runtime_error("Could not schedule Unreal collector");
        worker = std::jthread(run);
    } catch (const std::exception& error) {
        active = false;
        log(std::string("ERROR: Linux server bridge: ") + error.what());
    } catch (...) {
        active = false;
        log("ERROR: Linux server bridge initialization failed");
    }
}

void linux_server_bridge_stop() noexcept {
    if (!active.exchange(false)) return;
    if (worker.joinable()) {
        worker.request_stop();
        worker.join();
    }
}
} // namespace bc
#endif
