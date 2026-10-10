#define NOMINMAX
#include <Windows.h>
#include <sddl.h>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FScriptArray.hpp>
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
#include "ServerTransport.hpp"
#include "ServerEquipmentIds.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <deque>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
using namespace RC::Unreal;
std::atomic<bool> active{};
constexpr std::int64_t event_coalesce_ms = 250;
constexpr std::int64_t retry_scan_ms = 1000;
constexpr std::int64_t reconcile_scan_ms = 10000;
std::atomic<std::int64_t> next_scan_ms{};
std::atomic<std::int64_t> last_scan_ms{};
std::atomic<bool> snapshot_dirty{true};
std::mutex scan_gate, snapshot_gate;
std::string snapshot;
std::uint64_t revision{};
struct PlayerEntry {
    std::string token;
    FWeakObjectPtr player;
    FWeakObjectPtr controller;
    std::string account_id;
    std::string platform_id;
    std::string platform_type;
};
struct Command {
    enum class Kind { Kick, MoveTeam, SwapTeam } kind{Kind::Kick};
    std::string request_id;
    std::string player_token;
    std::string other_token;
    int team_index{-1};
    int team_size{};
    std::uint64_t session;
};
std::unordered_map<UObject*, PlayerEntry> player_entries;
std::uint64_t next_player_token{};
std::mutex command_gate;
std::deque<Command> pending_commands;
std::deque<std::string> responses;
std::atomic<bool> commands_pending{};
std::atomic<std::uint64_t> pipe_session{};
UFunction* backend_init_function{};
std::array<UFunction*, 14> state_change_functions{};
constexpr std::array state_change_paths{
    STR("/Script/Engine.GameModeBase:K2_PostLogin"),
    STR("/Script/Engine.GameModeBase:K2_OnLogout"),
    STR("/Script/DeceiveInc.DeceiveIncMatchGameState:AdvancePhase"),
    STR("/Script/DeceiveInc.DeceiveIncMatchGameState:HandleNewSpyLoadoutCompleted"),
    STR("/Script/DeceiveInc.DeceiveIncPlayerController:OnSessionSelectionsModified"),
    STR("/Script/DeceiveInc.DeceiveIncPlayerController:ServerSelectAgent"),
    STR("/Script/DeceiveInc.DeceiveIncPlayerController:Server_SetPlayerName"),
    STR("/Script/DeceiveInc.DeceiveIncPlayerController:ServerInitPlayerInfos"),
    STR("/Script/DeceiveInc.Spy:OnPowerupStateChangedServer"),
    STR("/Script/DeceiveInc.DeceiveIncPlayerController:ServerJoinTeam"),
    STR("/Script/DeceiveInc.DeceiveIncPlayerController:ServerLeaveTeam"),
    STR("/Script/DeceiveInc.HealthComponent:HandleTakeAnyDamage"),
    STR("/Script/DeceiveInc.HealthComponent:SetHealth"),
    STR("/Script/DeceiveInc.HealthComponent:SetMaxHealth"),
};

PlayerEntry& connection_entry(UObject* controller) {
    auto [entry, inserted] = player_entries.try_emplace(controller);
    if (inserted || entry->second.controller.Get() != controller) {
        entry->second = PlayerEntry{};
        entry->second.token = "p" + std::to_string(++next_player_token);
        entry->second.controller = controller;
    }
    return entry->second;
}

struct OwnedString {
    FString value;
    OwnedString() = default;
    explicit OwnedString(const wchar_t* text) : value(text) {}
    ~OwnedString() { value.GetCharTArray().Empty(); }
    OwnedString(const OwnedString&) = delete;
    OwnedString& operator=(const OwnedString&) = delete;
};

std::string utf8(std::wstring_view value) {
    if (value.empty() || value.size() > 512) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr)) return {};
    return result;
}

bool valid(UObject* object) {
    return object && !object->IsUnreachable() &&
        UObjectArray::IsValid(object->GetObjectItem(), false);
}

bool flag(UObject* object, const wchar_t* name) {
    auto* property = object->GetPropertyByNameInChain(name);
    return property && property->GetClass().GetName() == STR("BoolProperty") &&
        static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(object);
}

std::optional<float> health_value(UObject* component, const wchar_t* function_name) {
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

UObject* object_field(UObject* object, const wchar_t* name) {
    auto* property = object->GetPropertyByNameInChain(name);
    if (!property || property->GetClass().GetName() != STR("ObjectProperty") ||
        property->GetElementSize() != sizeof(UObject*)) return nullptr;
    return *property->ContainerPtrToValuePtr<UObject*>(object);
}

bool connected_human(UObject* player) {
    // Deceive Inc caches PlayerStates after logout for reconnects. Those can
    // remain in PlayerArray, so neither array membership nor a name proves a
    // player is currently connected.
    if (flag(player, STR("bIsInactive")) || flag(player, STR("bIsABot"))) return false;
    auto* controller = object_field(player, STR("Owner"));
    return valid(controller) && valid(object_field(controller, STR("NetConnection")));
}

std::string player_platform(UObject* player) {
    auto* property = player->GetPropertyByNameInChain(STR("PlatformType"));
    if (!property || property->GetClass().GetName() != STR("EnumProperty") ||
        property->GetElementSize() != 1) return "Unknown";
    switch (*property->ContainerPtrToValuePtr<std::uint8_t>(player)) {
        case 0: return "PC";
        case 1: return "Xbox";
        case 2: return "PlayStation";
        default: return "Unknown";
    }
}

std::string player_network_id(UObject* player) {
    auto* property = player->GetPropertyByNameInChain(STR("UniqueId"));
    if (!property || property->GetClass().GetName() != STR("StructProperty")) return {};
    OwnedString exported;
    property->ExportTextItem(exported.value, property->ContainerPtrToValuePtr<void>(player),
        nullptr, player, 0);
    const auto count = exported.value.GetCharTArray().Num();
    if (count < 2 || count > 257 || !exported.value.GetCharArray()) return {};
    auto result = utf8(std::wstring_view(exported.value.GetCharArray(), count - 1));
    if (result.empty() || result == "()" || result == "INVALID") return {};
    return result;
}

std::string string_field(const void* base, FProperty* property) {
    if (!property || property->GetClass().GetName() != STR("StrProperty")) return {};
    const auto* value = property->ContainerPtrToValuePtr<FString>(base);
    const auto count = value->GetCharTArray().Num();
    if (count < 2 || count > 257 || !value->GetCharArray()) return {};
    return utf8(std::wstring_view(value->GetCharArray(), count - 1));
}

const void* struct_field(const void* base, FProperty* property, const wchar_t* expected_type) {
    if (!base || !property || property->GetClass().GetName() != STR("StructProperty")) return nullptr;
    auto* structure = static_cast<FStructProperty*>(property)->GetStruct();
    return structure && structure->GetPathName() == expected_type
        ? property->ContainerPtrToValuePtr<void>(base) : nullptr;
}

std::optional<int> level_field(const void* base, FProperty* property) {
    if (!base || !property || property->GetClass().GetName() != STR("IntProperty") ||
        property->GetElementSize() != sizeof(std::int32_t)) return std::nullopt;
    const auto level = *property->ContainerPtrToValuePtr<std::int32_t>(base);
    return level >= 1 && level <= 10000 ? std::optional<int>(level) : std::nullopt;
}

std::optional<int> player_team(UObject* player) {
    if (!valid(player)) return std::nullopt;
    auto* property = player->GetPropertyByNameInChain(STR("FactionID"));
    if (!property || property->GetClass().GetName() != STR("ByteProperty") ||
        property->GetElementSize() != sizeof(std::uint8_t)) return std::nullopt;
    const auto id = *property->ContainerPtrToValuePtr<std::uint8_t>(player);
    return id < 32 ? std::optional<int>(id) : std::nullopt;
}

std::string agent_slug(std::string name) {
    const auto prefix = name.find("DA_AgentData_");
    if (prefix != std::string::npos) name.erase(0, prefix + sizeof("DA_AgentData_") - 1);
    if (name == "SquireSeason4") name = "Squire";
    if (name.empty() || name == "None" || name == "Invalid" || name.size() > 40 ||
        !std::all_of(name.begin(), name.end(), [](unsigned char ch) {
            return std::isalnum(ch) || ch == '_';
        })) return {};
    return name;
}

std::string primary_asset_name(const void* base, FProperty* property) {
    const auto* id = struct_field(base, property, STR("/Script/CoreUObject.PrimaryAssetId"));
    if (!id) return {};
    auto* id_type = static_cast<FStructProperty*>(property)->GetStruct();
    auto* name_property = id_type->GetPropertyByNameInChain(STR("PrimaryAssetName"));
    if (!name_property || name_property->GetClass().GetName() != STR("NameProperty") ||
        name_property->GetElementSize() != sizeof(FName)) return {};
    return utf8(name_property->ContainerPtrToValuePtr<FName>(id)->ToString());
}

std::string gadget_slug(std::string name) {
    for (const auto prefix : {"DA_GadgetData_", "DA_Gadget_", "Gadget_"}) {
        if (name.starts_with(prefix)) { name.erase(0, std::char_traits<char>::length(prefix)); break; }
    }
    if (name.empty() || name.size() > 48 ||
        !std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_';
        })) return {};
    return std::string(briefcase::server::gadget_id(name));
}

nlohmann::json selected_gadgets(UObject* player) {
    nlohmann::json result = nlohmann::json::array();
    auto* selection_property = player->GetPropertyByNameInChain(STR("AgentSelection"));
    const auto* selection = struct_field(player, selection_property,
        STR("/Script/DeceiveInc.PlayerAgentSelectionInfo"));
    if (!selection) return result;
    auto* selection_type = static_cast<FStructProperty*>(selection_property)->GetStruct();
    auto* info_property = selection_type->GetPropertyByNameInChain(STR("SelectionInfo"));
    const auto* info = struct_field(selection, info_property,
        STR("/Script/DeceiveInc.DISerializedAgentSelectionInfo"));
    if (!info) return result;
    auto* info_type = static_cast<FStructProperty*>(info_property)->GetStruct();
    for (const auto* field : {STR("Gadget1"), STR("Gadget2")}) {
        auto slug = gadget_slug(primary_asset_name(info, info_type->GetPropertyByNameInChain(field)));
        if (!slug.empty() && slug != "None") result.push_back(std::move(slug));
    }
    return result;
}

std::optional<int> selection_variant(const void* info, UScriptStruct* type,
                                     const wchar_t* field, std::string_view prefix,
                                     std::string_view agent) {
    if (!info || !type || agent.empty()) return std::nullopt;
    const auto name = primary_asset_name(info, type->GetPropertyByNameInChain(field));
    const auto expected = std::string(prefix) + std::string(agent) + "_";
    if (!name.starts_with(expected)) return std::nullopt;
    const auto variant = std::string_view(name).substr(expected.size());
    if (variant == "Default") return 0;
    if (variant == "Mod1") return 1;
    if (variant == "Mod2") return 2;
    return std::nullopt;
}

nlohmann::json selected_loadout(UObject* player, std::string_view agent) {
    nlohmann::json result = nlohmann::json::object();
    auto* selection_property = player->GetPropertyByNameInChain(STR("AgentSelection"));
    const auto* selection = struct_field(player, selection_property,
        STR("/Script/DeceiveInc.PlayerAgentSelectionInfo"));
    if (!selection) return result;
    auto* selection_type = static_cast<FStructProperty*>(selection_property)->GetStruct();
    auto* info_property = selection_type->GetPropertyByNameInChain(STR("SelectionInfo"));
    const auto* info = struct_field(selection, info_property,
        STR("/Script/DeceiveInc.DISerializedAgentSelectionInfo"));
    if (!info) return result;
    auto* info_type = static_cast<FStructProperty*>(info_property)->GetStruct();
    for (const auto& [key, field, prefix] : {
             std::tuple{"weapon", STR("WeaponVariant"), "DA_AgentWeaponData_"},
             std::tuple{"expertise", STR("ActiveVariant"), "DA_AgentExpertiseData_"},
             std::tuple{"passive", STR("PassiveVariant"), "DA_AgentPassiveData_"}}) {
        if (auto variant = selection_variant(info, info_type, field, prefix, agent))
            result[key] = *variant;
    }
    return result;
}

std::string powerup_kind(UEnum* enumeration, std::uint8_t value) {
    if (!enumeration) return {};
    std::string name;
    for (const auto pair : enumeration->ForEachName()) {
        if (pair.Value == value) { name = utf8(pair.Key.ToString()); break; }
    }
    if (auto mapped = briefcase::server::powerup_icon_id(name); !mapped.empty())
        return std::string(mapped);
    if (auto colon = name.rfind("::"); colon != std::string::npos) name.erase(0, colon + 2);
    if (name == "Count" || name == "First" || name == "Last" || name.empty() ||
        name.size() > 48 || !std::all_of(name.begin(), name.end(), [](unsigned char ch) {
            return std::isalnum(ch) || ch == '_';
        })) return {};
    return name;
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
    std::uint8_t parameters[3]{kind, 0, 0};
    spy->ProcessEvent(function, parameters);
    return static_cast<FBoolProperty*>(available)->GetPropertyValueInContainer(parameters) &&
        parameters[1] == tier;
}

nlohmann::json selected_upgrades(UObject* player) {
    nlohmann::json result = nlohmann::json::array();
    auto* deck_property = player->GetPropertyByNameInChain(STR("EquippedDeck"));
    const auto* deck = struct_field(player, deck_property,
        STR("/Script/DeceiveInc.DISerializedDeckEntry"));
    if (!deck) return result;
    auto* deck_type = static_cast<FStructProperty*>(deck_property)->GetStruct();
    auto* array_property = deck_type->GetPropertyByNameInChain(STR("EquippedPowerups"));
    if (!array_property || array_property->GetClass().GetName() != STR("ArrayProperty")) return result;
    auto* inner = static_cast<FArrayProperty*>(array_property)->GetInner();
    if (!inner || inner->GetClass().GetName() != STR("EnumProperty") ||
        inner->GetElementSize() != 1) return result;
    const auto* array = array_property->ContainerPtrToValuePtr<FScriptArray>(deck);
    if (!array || array->Num() < 0 || array->Num() > 5 || array->Max() < array->Num() ||
        array->Max() > 32 || (array->Num() && !array->GetData())) return result;
    auto* enumeration = UObjectGlobals::StaticFindObject<UEnum*>(nullptr, nullptr,
        STR("/Script/DeceiveInc.EPowerupType"));
    constexpr const char* tiers[]{"Civilian", "Staff", "Guard", "Technician", "VIP"};
    auto* spy = object_field(player, STR("OwnedSpy"));
    const auto* values = static_cast<const std::uint8_t*>(array->GetData());
    for (int index = 0; index < array->Num(); ++index) {
        auto kind = powerup_kind(enumeration, values[index]);
        if (!kind.empty()) result.push_back({{"tier", tiers[index]}, {"kind", kind},
                                             {"owned", owns_powerup(spy, values[index],
                                                 static_cast<std::uint8_t>(index))}});
    }
    return result;
}

std::string selected_agent(UObject* player) {
    auto* selection_property = player->GetPropertyByNameInChain(STR("AgentSelection"));
    const auto* selection = struct_field(player, selection_property,
        STR("/Script/DeceiveInc.PlayerAgentSelectionInfo"));
    if (selection) {
        auto* selection_type = static_cast<FStructProperty*>(selection_property)->GetStruct();
        auto* id_property = selection_type->GetPropertyByNameInChain(STR("AgentId"));
        const auto* id = struct_field(selection, id_property, STR("/Script/CoreUObject.PrimaryAssetId"));
        if (id) {
            auto* id_type = static_cast<FStructProperty*>(id_property)->GetStruct();
            auto* name_property = id_type->GetPropertyByNameInChain(STR("PrimaryAssetName"));
            if (name_property && name_property->GetClass().GetName() == STR("NameProperty") &&
                name_property->GetElementSize() == sizeof(FName)) {
                const auto* name = name_property->ContainerPtrToValuePtr<FName>(id);
                auto slug = agent_slug(utf8(name->ToString()));
                if (!slug.empty()) return slug;
            }
        }
    }
    auto* spy = object_field(player, STR("OwnedSpy"));
    auto* agent_data = valid(spy) ? object_field(spy, STR("AgentData")) : nullptr;
    return valid(agent_data) ? agent_slug(utf8(agent_data->GetName())) : std::string{};
}

const void* player_progression(UObject* player, UScriptStruct*& type) {
    auto* property = player->GetPropertyByNameInChain(STR("PlayerProgression"));
    const auto* data = struct_field(player, property, STR("/Script/DeceiveInc.PlayerProgression"));
    type = data ? static_cast<FStructProperty*>(property)->GetStruct() : nullptr;
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
    auto* entry_type = static_cast<FStructProperty*>(inner)->GetStruct();
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
    for (int index = 0; index < array->Num(); ++index) {
        const auto* entry = static_cast<const std::byte*>(array->GetData()) +
            index * inner->GetElementSize();
        auto category = string_field(entry, xp_type);
        std::transform(category.begin(), category.end(), category.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        // XP categories may be plain agent names or prefixed by the game.
        if (category == wanted || category == "agent_" + wanted || category == "agent" + wanted ||
            category == "da_agentdata_" + wanted) return level_field(entry, level);
    }
    return std::nullopt;
}

void capture_identity(UObject* controller, UFunction* function, void* params) {
    if (!valid(controller) || !params || !function ||
        function->GetPathName() != STR("/Script/DeceiveInc.DeceiveIncPlayerController:ServerInitBackend")) return;
    auto* composite = function->GetPropertyByNameInChain(STR("UniqueNetId"));
    if (!composite || composite->GetClass().GetName() != STR("StructProperty") ||
        composite->GetOffset_Internal() != 0 || function->GetParmsSize() < 0x30) return;
    auto* structure = static_cast<FStructProperty*>(composite)->GetStruct();
    if (!structure || structure->GetPathName() != STR("/Script/DeceiveInc.BackendUniqueIdComposite")) return;
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

void reply(const Command& command, bool ok, std::string_view message) {
    if (command.session != pipe_session.load(std::memory_order_acquire)) return;
    nlohmann::json value{{"schemaVersion", 2}, {"type", "result"},
                         {"requestId", command.request_id}, {"ok", ok}, {"message", message}};
    std::lock_guard lock(command_gate);
    if (command.session != pipe_session.load(std::memory_order_relaxed)) return;
    responses.push_back(value.dump() + '\n');
}

void receive_command(std::string_view line) {
    try {
        if (line.size() > 4096) return;
        const auto value = nlohmann::json::parse(line);
        if (value.at("schemaVersion") != 2 || value.at("type") != "command") return;
        const auto name = value.at("command").get<std::string>();
        Command::Kind kind;
        if (name == "kick") kind = Command::Kind::Kick;
        else if (name == "moveTeam") kind = Command::Kind::MoveTeam;
        else if (name == "swapTeam") kind = Command::Kind::SwapTeam;
        else return;
        const auto request_id = value.at("requestId").get<std::string>();
        const auto token = value.at("playerToken").get<std::string>();
        if (request_id.empty() || request_id.size() > 64 || token.empty() || token.size() > 64 ||
            !std::all_of(request_id.begin(), request_id.end(), [](unsigned char ch) {
                return std::isalnum(ch) || ch == '-';
            }) || !std::all_of(token.begin(), token.end(), [](unsigned char ch) {
                return std::isalnum(ch);
            })) return;
        Command command{kind, request_id, token, {}, -1, 0,
                        pipe_session.load(std::memory_order_relaxed)};
        if (kind != Command::Kind::Kick) {
            command.team_size = value.at("teamSize").get<int>();
            if (command.team_size != 2 && command.team_size != 3) return;
            if (kind == Command::Kind::MoveTeam) {
                command.team_index = value.at("team").get<int>();
                if (command.team_index < 0 || command.team_index >= 32) return;
            } else {
                command.other_token = value.at("otherToken").get<std::string>();
                if (command.other_token.empty() || command.other_token.size() > 64 ||
                    command.other_token == token ||
                    !std::all_of(command.other_token.begin(), command.other_token.end(),
                        [](unsigned char ch) { return std::isalnum(ch); })) return;
            }
        }
        std::lock_guard lock(command_gate);
        if (pending_commands.size() >= 32) {
            responses.push_back(nlohmann::json{{"schemaVersion", 2}, {"type", "result"},
                {"requestId", request_id}, {"ok", false}, {"message", "Server command queue is full"}}.dump() + '\n');
            return;
        }
        pending_commands.push_back(std::move(command));
        commands_pending.store(true, std::memory_order_release);
    } catch (...) { /* Malformed commands are ignored without touching Unreal. */ }
}

UObject* current_match_state() {
    auto* type = UObjectGlobals::StaticFindObject<UClass*>(
        nullptr, nullptr, STR("/Script/DeceiveInc.DeceiveIncMatchGameState"));
    if (!type) return nullptr;
    UObject* state{};
    UObjectGlobals::ForEachUObject([&](UObject* object, std::int32_t, std::int32_t) {
        if (valid(object) && object->IsA(type) &&
            !object->HasAnyFlags(static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject)) &&
            object->GetWorld()) {
            state = object;
            return RC::LoopAction::Break;
        }
        return RC::LoopAction::Continue;
    });
    return state;
}

bool lobby_phase(UObject* state) {
    if (!valid(state)) return false;
    auto* property = state->GetPropertyByNameInChain(STR("GamePhase"));
    auto* enumeration = UObjectGlobals::StaticFindObject<UEnum*>(nullptr, nullptr,
        STR("/Script/DeceiveInc.ESpyGamePhase"));
    if (!property || property->GetSize() != 1 || !enumeration) return false;
    for (const auto pair : enumeration->ForEachName()) {
        const auto name = pair.Key.ToString();
        if (name == STR("PREGAME") || name.ends_with(STR("::PREGAME")))
            return *property->ContainerPtrToValuePtr<std::uint8_t>(state) == pair.Value;
    }
    return false;
}

std::optional<int> faction_size(UObject* state) {
    if (!state) return std::nullopt;
    auto* type = UObjectGlobals::StaticFindObject<UClass*>(
        nullptr, nullptr, STR("/Script/DeceiveInc.DIFactionsManager"));
    if (!type) return std::nullopt;
    UObject* manager{};
    UObjectGlobals::ForEachUObject([&](UObject* object, std::int32_t, std::int32_t) {
        if (valid(object) && object->IsA(type) &&
            !object->HasAnyFlags(static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject)) &&
            object->GetWorld() == state->GetWorld()) {
            manager = object;
            return RC::LoopAction::Break;
        }
        return RC::LoopAction::Continue;
    });
    if (!manager) return std::nullopt;
    auto* function = manager->GetFunctionByNameInChain(STR("GetFactionSize"));
    auto* result = function ? function->GetPropertyByNameInChain(STR("ReturnValue")) : nullptr;
    if (!function || function->GetParmsSize() != 1 || !result ||
        result->GetClass().GetName() != STR("ByteProperty") ||
        result->GetOffset_Internal() != 0) return std::nullopt;
    std::uint8_t size{};
    manager->ProcessEvent(function, &size);
    return size >= 1 && size <= 3 ? std::optional<int>(size) : std::nullopt;
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
    auto* team = function->GetPropertyByNameInChain(STR("TeamIndex"));
    if (function->GetParmsSize() != sizeof(std::int32_t) || !team ||
        team->GetClass().GetName() != STR("IntProperty") ||
        team->GetOffset_Internal() != 0) return false;
    std::int32_t parameter = index;
    controller->ProcessEvent(function, &parameter);
    return true;
}

bool connected_entry(const PlayerEntry& entry) {
    auto* player = entry.player.Get();
    auto* controller = entry.controller.Get();
    return valid(player) && valid(controller) && connected_human(player) &&
        object_field(player, STR("Owner")) == controller;
}

void process_team_command(const Command& command, PlayerEntry& source) {
    auto* state = current_match_state();
    if (!lobby_phase(state)) {
        reply(command, false, "Teams can only be changed in the pregame lobby");
        return;
    }
    if (faction_size(state) != command.team_size) {
        reply(command, false, "The server's team size does not match the selected game mode");
        return;
    }
    auto* source_player = source.player.Get();
    auto* source_controller = source.controller.Get();
    const auto source_team = player_team(source_player);
    if (!source_team) {
        reply(command, false, "The player has no assigned team");
        return;
    }
    int target_team = command.team_index;
    PlayerEntry* other{};
    if (command.kind == Command::Kind::SwapTeam) {
        auto found = std::find_if(player_entries.begin(), player_entries.end(), [&](const auto& item) {
            return item.second.token == command.other_token;
        });
        if (found == player_entries.end() || !connected_entry(found->second)) {
            reply(command, false, "The other player is no longer connected");
            return;
        }
        other = &found->second;
        auto other_team = player_team(other->player.Get());
        if (!other_team) {
            reply(command, false, "The other player has no assigned team");
            return;
        }
        target_team = *other_team;
    }
    if (target_team == *source_team) {
        reply(command, true, "Players are already on the same team");
        return;
    }
    if (target_team < 0 || target_team >= 32) {
        reply(command, false, "Invalid target team");
        return;
    }
    try {
        // Use the dedicated server's own leave/join operations so its faction
        // plan and replicated PlayerState stay in sync. Restore both assignments
        // if an intermediate operation is rejected.
        if (other && !call_team(other->controller.Get(), false)) {
            reply(command, false, "Team controls are unavailable in this game build");
            return;
        }
        if (!call_team(source_controller, false) || !call_team(source_controller, true, target_team) ||
            player_team(source_player) != target_team) {
            call_team(source_controller, true, *source_team);
            if (other) call_team(other->controller.Get(), true, target_team);
            reply(command, false, "The server rejected the team change");
            return;
        }
        if (other && (!call_team(other->controller.Get(), true, *source_team) ||
                      player_team(other->player.Get()) != *source_team)) {
            call_team(source_controller, false);
            call_team(source_controller, true, *source_team);
            call_team(other->controller.Get(), true, target_team);
            reply(command, false, "The server rejected the team swap");
            return;
        }
        snapshot_dirty.store(true, std::memory_order_release);
        reply(command, true, other ? "Players swapped" : "Player moved to team");
    } catch (...) {
        reply(command, false, "The team change failed in Unreal");
    }
}

void process_commands() {
    std::deque<Command> commands;
    {
        std::lock_guard lock(command_gate);
        commands.swap(pending_commands);
        commands_pending.store(false, std::memory_order_release);
    }
    for (const auto& command : commands) {
        if (command.session != pipe_session.load(std::memory_order_acquire)) continue;
        auto entry = std::find_if(player_entries.begin(), player_entries.end(), [&](const auto& item) {
            return item.second.token == command.player_token;
        });
        if (entry == player_entries.end()) {
            reply(command, false, "Player is no longer connected");
            continue;
        }
        auto* player = entry->second.player.Get();
        auto* controller = entry->second.controller.Get();
        if (!valid(player) || !valid(controller) || !connected_human(player) ||
            object_field(player, STR("Owner")) != controller) {
            reply(command, false, "Player is no longer connected");
            continue;
        }
        if (command.kind != Command::Kind::Kick) {
            process_team_command(command, entry->second);
            continue;
        }
        try {
            auto* function = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
                STR("/Script/Engine.PlayerController:ClientReturnToMainMenu"));
            auto* reason_property = function
                ? function->GetPropertyByNameInChain(STR("ReturnReason")) : nullptr;
            if (!function || function->GetParmsSize() != sizeof(FString) ||
                !reason_property || reason_property->GetClass().GetName() != STR("StrProperty") ||
                reason_property->GetOffset_Internal() != 0) {
                reply(command, false, "Kick is unavailable in this game build");
                continue;
            }
            OwnedString reason(STR("Removed by the server administrator."));
            controller->ProcessEvent(function, &reason.value);
            reply(command, true, "Kick sent to player");
        } catch (...) {
            reply(command, false, "Kick failed in Unreal");
        }
    }
}

bool collect() {
    auto* state = current_match_state();
    if (!state) return false;
    auto* property = state->GetPropertyByNameInChain(STR("PlayerArray"));
    if (!property || property->GetClass().GetName() != STR("ArrayProperty")) return false;
    auto* array = static_cast<FArrayProperty*>(property);
    if (!array->GetInner() || array->GetInner()->GetClass().GetName() != STR("ObjectProperty") ||
        array->GetInner()->GetElementSize() != sizeof(UObject*)) return false;
    const auto* players = array->ContainerPtrToValuePtr<FScriptArray>(state);
    if (!players || players->Num() < 0 || players->Num() > 128 ||
        players->Max() < players->Num() || players->Max() > 1024 ||
        (players->Num() && !players->GetData())) return false;
    nlohmann::json names = nlohmann::json::array();
    std::unordered_set<UObject*> seen;
    auto* data = reinterpret_cast<UObject* const*>(players->GetData());
    for (int index = 0; index < players->Num(); ++index) {
        auto* player = data[index];
        if (!valid(player) || !connected_human(player)) continue;
        auto* name_property = player->GetPropertyByNameInChain(STR("PlayerNamePrivate"));
        if (!name_property || name_property->GetClass().GetName() != STR("StrProperty")) continue;
        const auto& name = *name_property->ContainerPtrToValuePtr<FString>(player);
        const auto length = name.GetCharTArray().Num() - 1;
        if (length <= 0 || length > 128 || !name.GetCharArray()) continue;
        auto readable = utf8(std::wstring_view(name.GetCharArray(), length));
        if (readable.empty()) continue;
        auto* controller = object_field(player, STR("Owner"));
        if (!controller) continue;
        seen.insert(controller);
        auto& entry = connection_entry(controller);
        entry.player = player;
        auto agent = selected_agent(player);
        UScriptStruct* progression_type{};
        const auto* progression = player_progression(player, progression_type);
        const auto account_level = progression
            ? level_field(progression, progression_type->GetPropertyByNameInChain(STR("Level")))
            : std::nullopt;
        const auto agent_level = selected_agent_level(progression, progression_type, agent);
        nlohmann::json details{{"name", std::move(readable)}, {"token", entry.token},
                               {"platform", entry.platform_type.empty()
                                  ? player_platform(player) : entry.platform_type},
                               {"networkId", entry.account_id.empty()
                                  ? player_network_id(player) : entry.account_id},
                               {"platformId", entry.platform_id}};
        if (!agent.empty()) details["character"] = agent == "Socialite" ? "Red" : agent;
        if (account_level) details["accountLevel"] = *account_level;
        if (agent_level) details["characterLevel"] = *agent_level;
        if (auto team = player_team(player)) details["team"] = *team;
        if (auto* dead = player->GetPropertyByNameInChain(STR("bIsDead"));
            dead && dead->GetClass().GetName() == STR("BoolProperty"))
            details["dead"] = static_cast<FBoolProperty*>(dead)->GetPropertyValueInContainer(player);
        if (auto* spy = object_field(player, STR("OwnedSpy")); valid(spy)) {
            if (auto* health = object_field(spy, STR("HealthComponent")); valid(health)) {
                const auto current = health_value(health, STR("GetHealth"));
                const auto maximum = health_value(health, STR("GetMaxHealth"));
                if (current && maximum && *maximum > 0) {
                    details["health"] = *current;
                    details["maxHealth"] = *maximum;
                }
            }
        }
        details["gadgets"] = selected_gadgets(player);
        details["loadout"] = selected_loadout(player, agent);
        details["upgrades"] = selected_upgrades(player);
        names.push_back(std::move(details));
    }
    for (auto it = player_entries.begin(); it != player_entries.end();) {
        if (!seen.contains(it->first)) it = player_entries.erase(it);
        else ++it;
    }
    std::string phase = "Running";
    try {
        auto* property = state->GetPropertyByNameInChain(STR("GamePhase"));
        auto* enumeration = UObjectGlobals::StaticFindObject<UEnum*>(nullptr, nullptr,
            STR("/Script/DeceiveInc.ESpyGamePhase"));
        if (property && property->GetSize() == 1 && enumeration) {
            for (const auto pair : enumeration->ForEachName()) {
                const auto name = pair.Key.ToString();
                if (name == STR("PREGAME") || name.ends_with(STR("::PREGAME"))) {
                    phase = *property->ContainerPtrToValuePtr<std::uint8_t>(state) == pair.Value
                        ? "Lobby" : "In match";
                    break;
                }
            }
        }
    } catch (...) { /* An unknown phase is still a running server. */ }
    std::string map;
    if (auto* world = state->GetWorld()) map = utf8(world->GetName());
    nlohmann::json value{{"schemaVersion", 2}, {"type", "snapshot"},
                         {"processId", GetCurrentProcessId()},
                         {"state", phase}, {"map", map}, {"players", std::move(names)}};
    char instance_id[64]{};
    const auto instance_length = GetEnvironmentVariableA("BRIEFCASE_INSTANCE_ID", instance_id, sizeof(instance_id));
    if (instance_length == 32) value["instanceId"] = std::string(instance_id, instance_length);
    std::lock_guard lock(snapshot_gate);
    auto updated = value.dump() + '\n';
    if (snapshot != updated) {
        snapshot = std::move(updated);
        ++revision;
    }
    return true;
}

void on_event(UObject* context, UFunction* function, void* params) {
    if (!active.load(std::memory_order_relaxed) || !function) return;
    if (function == backend_init_function) {
        try { capture_identity(context, function, params); }
        catch (...) { /* Identity is optional; never obstruct player login. */ }
    }
    if (function->GetName() != STR("ReceiveTick")) return;
    if (commands_pending.load(std::memory_order_acquire)) {
        const std::unique_lock lock(scan_gate, std::try_to_lock);
        if (lock.owns_lock()) process_commands();
    }
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    const auto event_due = snapshot_dirty.load(std::memory_order_acquire) &&
        milliseconds - last_scan_ms.load(std::memory_order_relaxed) >= event_coalesce_ms;
    if (!event_due && milliseconds < next_scan_ms.load(std::memory_order_relaxed)) return;
    const std::unique_lock lock(scan_gate, std::try_to_lock);
    if (!lock.owns_lock()) return;
    snapshot_dirty.exchange(false, std::memory_order_acq_rel);
    last_scan_ms.store(milliseconds, std::memory_order_relaxed);
    try {
        next_scan_ms.store(milliseconds + (collect() ? reconcile_scan_ms : retry_scan_ms),
            std::memory_order_relaxed);
    } catch (...) {
        // A level transition may temporarily invalidate reflected objects.
        next_scan_ms.store(milliseconds + retry_scan_ms, std::memory_order_relaxed);
    }
}

void on_state_changed(UObject*, UFunction* function, void*) {
    if (!active.load(std::memory_order_relaxed) || !function) return;
    // Wait until Unreal finishes the call before reading its resulting state.
    if (function == backend_init_function ||
        std::find(state_change_functions.begin(), state_change_functions.end(), function) !=
            state_change_functions.end())
        snapshot_dirty.store(true, std::memory_order_release);
}

PSECURITY_DESCRIPTOR pipe_security() {
    HANDLE token{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return nullptr;
    DWORD size{};
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<std::uint64_t> buffer((size + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t));
    const bool read = GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != FALSE;
    CloseHandle(token);
    if (!read) return nullptr;
    LPWSTR sid{};
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) return nullptr;
    std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor{};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
            &descriptor, nullptr)) return nullptr;
    return descriptor;
}

bool send_snapshot(HANDLE pipe, const std::string& message, std::stop_token stop) {
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready) return false;
    OVERLAPPED operation{};
    operation.hEvent = ready;
    DWORD written{};
    bool complete = WriteFile(pipe, message.data(), static_cast<DWORD>(message.size()),
        &written, &operation) != FALSE;
    const bool pending = !complete && GetLastError() == ERROR_IO_PENDING;
    if (pending) {
        for (int attempt = 0; attempt < 25 && !stop.stop_requested(); ++attempt) {
            if (WaitForSingleObject(ready, 200) == WAIT_OBJECT_0) {
                complete = GetOverlappedResult(pipe, &operation, &written, FALSE) != FALSE;
                break;
            }
        }
    }
    if (!complete && pending) {
        CancelIoEx(pipe, &operation);
        WaitForSingleObject(ready, INFINITE);
    }
    CloseHandle(ready);
    return complete && written == message.size();
}

bool receive_messages(HANDLE pipe, std::string& buffered,
                      const briefcase::server::IServerTransport::ReceiveMessage& receive_message,
                      std::stop_token stop) {
    DWORD available{};
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) return false;
    if (!available) return true;
    char chunk[4096];
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready) return false;
    OVERLAPPED operation{};
    operation.hEvent = ready;
    DWORD read{};
    bool complete = ReadFile(pipe, chunk, std::min<DWORD>(available, sizeof(chunk)),
        &read, &operation) != FALSE;
    const bool pending = !complete && GetLastError() == ERROR_IO_PENDING;
    if (pending) {
        for (int attempt = 0; attempt < 25 && !stop.stop_requested(); ++attempt) {
            if (WaitForSingleObject(ready, 200) == WAIT_OBJECT_0) {
                complete = GetOverlappedResult(pipe, &operation, &read, FALSE) != FALSE;
                break;
            }
        }
    }
    if (!complete && pending) {
        CancelIoEx(pipe, &operation);
        WaitForSingleObject(ready, INFINITE);
    }
    CloseHandle(ready);
    if (!complete) return false;
    buffered.append(chunk, read);
    for (auto end = buffered.find('\n'); end != std::string::npos; end = buffered.find('\n')) {
        if (end <= 4096) receive_message(std::string_view(buffered).substr(0, end));
        buffered.erase(0, end + 1);
    }
    return buffered.size() <= 4096;
}

class WindowsNamedPipeTransport final : public briefcase::server::IServerTransport {
    std::atomic<HANDLE> pipe_{INVALID_HANDLE_VALUE};
public:
    void run(std::stop_token stop, const NextMessage& next_message,
             const ReceiveMessage& receive_message) override {
        const auto name = std::format(LR"(\\.\pipe\Briefcase.Server.{})", GetCurrentProcessId());
        auto* descriptor = pipe_security();
        if (!descriptor) return;
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
        while (!stop.stop_requested()) {
            HANDLE pipe = CreateNamedPipeW(name.c_str(),
                PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                1, 65536, 65536, 0, &attributes);
            if (pipe == INVALID_HANDLE_VALUE) break;
            pipe_.store(pipe);
            HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!ready) { CloseHandle(pipe); break; }
            OVERLAPPED operation{};
            operation.hEvent = ready;
            const bool immediate = ConnectNamedPipe(pipe, &operation) != FALSE;
            const auto error = immediate ? ERROR_SUCCESS : GetLastError();
            bool connected = immediate || error == ERROR_PIPE_CONNECTED;
            if (!connected && error == ERROR_IO_PENDING) {
                while (!stop.stop_requested()) {
                    if (WaitForSingleObject(ready, 200) == WAIT_OBJECT_0) {
                        DWORD count{};
                        connected = GetOverlappedResult(pipe, &operation, &count, FALSE) != FALSE;
                        break;
                    }
                }
                if (stop.stop_requested() && !connected) {
                    CancelIoEx(pipe, &operation);
                    DWORD count{};
                    GetOverlappedResult(pipe, &operation, &count, TRUE);
                }
            }
            CloseHandle(ready);
            std::uint64_t sent{};
            std::string buffered;
            if (connected) {
                std::lock_guard lock(command_gate);
                pipe_session.fetch_add(1, std::memory_order_release);
                pending_commands.clear();
                responses.clear();
                commands_pending.store(false, std::memory_order_release);
            }
            while (connected && !stop.stop_requested()) {
                if (!receive_messages(pipe, buffered, receive_message, stop)) break;
                std::deque<std::string> outgoing;
                {
                    std::lock_guard lock(command_gate);
                    outgoing.swap(responses);
                }
                bool sent_responses = true;
                for (const auto& response : outgoing)
                    if (!send_snapshot(pipe, response, stop)) { sent_responses = false; break; }
                if (!sent_responses) break;
                auto message = next_message(sent);
                if (!message.empty()) {
                    if (!send_snapshot(pipe, message, stop)) break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            {
                std::lock_guard lock(command_gate);
                pipe_session.fetch_add(1, std::memory_order_release);
                pending_commands.clear();
                responses.clear();
                commands_pending.store(false, std::memory_order_release);
            }
            CancelIoEx(pipe, nullptr);
            DisconnectNamedPipe(pipe);
            pipe_.store(INVALID_HANDLE_VALUE);
            CloseHandle(pipe);
        }
        LocalFree(descriptor);
    }
    void interrupt() noexcept override {
        if (auto pipe = pipe_.load(); pipe != INVALID_HANDLE_VALUE) CancelIoEx(pipe, nullptr);
    }
};

class Bridge final : public RC::CppUserModBase {
    std::unique_ptr<briefcase::server::IServerTransport> transport_ =
        std::make_unique<WindowsNamedPipeTransport>();
    std::jthread worker_;
public:
    Bridge() {
        ModName = STR("Briefcase.ServerBridge");
        ModVersion = STR("1.0.0");
        ModDescription = STR("Local ServerApp state stream");
        ModAuthors = STR("EnoPM");
    }
    void on_unreal_init() override {
        backend_init_function = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
            STR("/Script/DeceiveInc.DeceiveIncPlayerController:ServerInitBackend"));
        for (std::size_t index = 0; index < state_change_paths.size(); ++index)
            state_change_functions[index] = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, state_change_paths[index]);
        active.store(true);
        Hook::RegisterProcessEventPreCallback(on_event);
        Hook::RegisterProcessEventPostCallback(on_state_changed);
        worker_ = std::jthread([this](std::stop_token stop) {
            auto next_heartbeat = std::chrono::steady_clock::time_point{};
            transport_->run(stop, [&next_heartbeat](std::uint64_t& sent) {
                std::lock_guard lock(snapshot_gate);
                if (snapshot.empty()) return std::string{};
                const auto now = std::chrono::steady_clock::now();
                if (revision == sent && now < next_heartbeat) return std::string{};
                sent = revision;
                next_heartbeat = now + std::chrono::seconds(3);
                return snapshot;
            }, receive_command);
        });
    }
    ~Bridge() override {
        active.store(false);
        if (worker_.joinable()) {
            worker_.request_stop();
            transport_->interrupt();
            CancelSynchronousIo(worker_.native_handle());
            worker_.join();
        }
    }
};
}

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod() { return new Bridge; }
extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod) { delete mod; }
