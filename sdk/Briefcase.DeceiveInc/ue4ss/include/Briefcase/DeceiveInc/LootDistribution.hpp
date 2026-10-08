#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace RC::Unreal { class UObject; }

namespace briefcase::deceive {

struct LootCandidate {
    std::string object_type;
    float weight{};
    std::uint32_t crc{};
};
struct LootObject {
    std::string object_type;
    float occurrence_factor{}, score{};
    bool max_one_per_room{}, can_fill_room{}, private_lobbies{}, spawn_on_start{};
    std::int32_t priority{};
};
struct LootCount {
    std::string object_type;
    std::uint32_t count{};
};
struct LootPoint {
    RC::Unreal::UObject *object{};
    std::string path, implementation, point_type, selection;
    std::uint32_t room_crc{};
    bool disabled{};
    std::vector<LootCandidate> candidates;
};
struct LootRoom {
    std::uint32_t crc{};
    std::uint8_t security{};
    bool vault{};
};

// A Deceive Inc-specific facade: mods never need to calculate Unreal offsets or
// manipulate a TArray/FString directly. All operations belong on the game thread.
class LootDistribution {
  public:
    static bool is_manager(RC::Unreal::UObject *object);
    // Template access is opt-in: chest reward components may exist only as
    // Blueprint defaults before their live actors are created.
    static bool is_point(RC::Unreal::UObject *object, bool include_templates = false);
    static std::vector<LootObject> objects(RC::Unreal::UObject *manager);
    static std::vector<LootCount> counts(RC::Unreal::UObject *manager);
    // Count data selected by the game mode, which may differ from the manager's array.
    static std::vector<LootCount> selected_counts(RC::Unreal::UObject *manager);
    static bool update_selected_count(RC::Unreal::UObject *manager, const LootCount &value);
    static bool update_object(RC::Unreal::UObject *manager, const LootObject &value);
    static bool update_count(RC::Unreal::UObject *manager, const LootCount &value);
    static std::optional<LootPoint> point(RC::Unreal::UObject *object, bool include_templates = false);
    // Reads the active game's room preset rather than embedding a cooked candidate table.
    static std::optional<std::vector<LootCandidate>> preset_candidates(bool vault,
        std::uint8_t security, const std::string &point_type);
    static void replace_candidates(RC::Unreal::UObject *point,
                                   const std::vector<LootCandidate> &candidates,
                                   bool include_templates = false);
    static std::vector<LootRoom> rooms();
    static std::uint32_t object_type_crc(const std::string &object_type);
};
} // namespace briefcase::deceive
