#include <Briefcase/DeceiveInc/Spy.hpp>
#include <Briefcase/UnrealApi.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <type_traits>
using namespace briefcase::deceive_inc;
using Json = nlohmann::json;
static unsigned checks{};
static void check(bool value, const char *label) {
    ++checks;
    if (!value)
        throw std::runtime_error(label);
}
template <class F> static void rejects(F action, const char *label) {
    bool failed{};
    try {
        action();
    } catch (const std::runtime_error &) {
        failed = true;
    }
    check(failed, label);
}
struct Backend {
    BcApi api{};
    BcUnrealApi unreal{};
    Json contracts = Json::parse(
        std::ifstream(std::filesystem::path(BRIEFCASE_SOURCE) / "tests/data/spy-unreal-contracts.json"));
    std::map<BcHandle, int> refs;
    std::map<BcHandle, std::string> functions;
    std::set<BcHandle> dead_handles;
    BcHandle next = 100, last_world{};
    unsigned resolutions{}, invocations{}, ownership_errors{};
    bool game_thread = true, local_dead{}, ads = true, no_weapon{}, wrong_kind{}, overflow{};
    unsigned fail_resolve{};
    BcResult invoke_error = BC_OK;
    BcHandle owned(BcHandle h) {
        if (h)
            ++refs[h];
        return h;
    }
    BcResult validate(BcHandle h) {
        if (!game_thread)
            return BC_WRONG_THREAD;
        return refs[h] > 0 && !dead_handles.contains(h) ? BC_OK : BC_STALE_HANDLE;
    }
    bool empty() const {
        for (const auto &[h, count] : refs)
            if (count)
                return false;
        return ownership_errors == 0;
    }
    static Backend &get(void *c) { return *static_cast<Backend *>(c); }
    Backend() {
        api.size = sizeof(api);
        api.version = BC_API_VERSION;
        api.context = this;
        api.get_service = [](void *c, const char *name, uint32_t, const void **out) -> BcResult {
            if (std::strcmp(name, BC_UNREAL_SERVICE))
                return BC_NOT_FOUND;
            *out = &get(c).unreal;
            return BC_OK;
        };
        api.validate_handle = [](void *c, BcHandle h) { return get(c).validate(h); };
        api.release_handle = [](void *c, BcHandle h) -> BcResult {
            auto &b = get(c);
            if (!b.game_thread || b.refs[h] <= 0) {
                ++b.ownership_errors;
                return BC_INVALID_ARGUMENT;
            }
            --b.refs[h];
            return BC_OK;
        };
        unreal.size = sizeof(unreal);
        unreal.version = 1;
        unreal.resolve_function = [](void *c, const char *path, const char *signature,
                                     BcHandle *out) -> BcResult {
            auto &b = get(c);
            if (!b.game_thread)
                return BC_WRONG_THREAD;
            ++b.resolutions;
            if (b.resolutions == b.fail_resolve || !b.contracts.contains(path) ||
                b.contracts.at(path) != Json::parse(signature))
                return BC_VERSION_MISMATCH;
            *out = b.owned(b.next++);
            b.functions[*out] = path;
            return BC_OK;
        };
        unreal.retain = [](void *c, BcHandle h) -> BcResult {
            auto &b = get(c);
            auto code = b.validate(h);
            if (code != BC_OK)
                return code;
            b.owned(h);
            return BC_OK;
        };
        unreal.is_a = [](void *c, BcHandle h, const char *path, uint32_t *out) -> BcResult {
            auto &b = get(c);
            auto code = b.validate(h);
            if (code != BC_OK)
                return code;
            *out = (h == 1 || h == 2) && std::string_view(path) == "/Script/DeceiveInc.Spy";
            return BC_OK;
        };
        unreal.object_info = [](void *c, BcHandle h, BcObjectInfo *out) -> BcResult {
            auto code = get(c).validate(h);
            if (code != BC_OK)
                return code;
            strcpy_s(out->path, "World.BPSpy_Squire_C_1");
            return BC_OK;
        };
        unreal.enumerate_actors = [](void *c, BcHandle world, const char *path, BcHandle *out, uint32_t cap,
                                     uint32_t *count) -> BcResult {
            auto &b = get(c);
            if (!b.game_thread)
                return BC_WRONG_THREAD;
            b.last_world = world;
            if (world && b.validate(world) != BC_OK)
                return BC_STALE_HANDLE;
            if (std::string_view(path) != "/Script/DeceiveInc.Spy")
                return BC_INVALID_ARGUMENT;
            if (b.overflow || cap < 2) {
                *count = 0;
                return BC_LIMIT;
            }
            *count = 2;
            out[0] = b.owned(1);
            out[1] = b.owned(2);
            return BC_OK;
        };
        unreal.invoke = [](void *c, BcHandle self, BcHandle fn, const BcNamedValue *, uint32_t n,
                           BcValue *out) -> BcResult {
            auto &b = get(c);
            auto code = b.validate(self);
            if (code != BC_OK)
                return code;
            if (b.invoke_error != BC_OK)
                return b.invoke_error;
            if (n || !b.functions.contains(fn))
                return BC_INVALID_ARGUMENT;
            ++b.invocations;
            auto path = b.functions.at(fn);
            auto method = path.substr(path.find(':') + 1);
            *out = {};
            if (b.wrong_kind) {
                out->kind = BC_VALUE_OBJECT;
                out->data.object = b.owned(90);
                return BC_OK;
            }
            if (method == "IsDead" || method == "IsBot" || method == "IsLocallyControlled" ||
                method == "IsInADS") {
                out->kind = BC_VALUE_BOOL;
                out->data.integer = method == "IsDead"                ? b.local_dead
                                    : method == "IsBot"               ? self == 2
                                    : method == "IsLocallyControlled" ? self == 1
                                                                      : b.ads;
            } else if (method == "GetController" || method == "GetWeaponTool") {
                out->kind = BC_VALUE_OBJECT;
                out->data.object =
                    b.owned(method == "GetController" ? (self == 1 ? 10 : 0) : (b.no_weapon ? 0 : 20));
            } else if (method == "K2_GetActorLocation" || method == "GetVelocity") {
                out->kind = BC_VALUE_VECTOR;
                out->data.vector[0] = method == "GetVelocity" ? 120 : 1000;
                out->data.vector[1] = -20;
                out->data.vector[2] = 40;
            } else
                return BC_NOT_FOUND;
            return BC_OK;
        };
        unreal.invoke_outputs = [](void *c, BcHandle self, BcHandle fn, const BcNamedValue *, uint32_t n,
                                   BcNamedValue *out, uint32_t count, BcValue *result) -> BcResult {
            auto &b = get(c);
            auto code = b.validate(self);
            if (code != BC_OK)
                return code;
            if (!b.functions.at(fn).ends_with(":GetActorEyesViewPoint") || n || count != 2 ||
                std::string_view(out[0].name) != "OutLocation" ||
                std::string_view(out[1].name) != "OutRotation")
                return BC_INVALID_ARGUMENT;
            out[0].value.kind = BC_VALUE_VECTOR;
            out[0].value.data.vector[0] = 10;
            out[0].value.data.vector[1] = 20;
            out[0].value.data.vector[2] = 170;
            out[1].value.kind = BC_VALUE_ROTATOR;
            out[1].value.data.vector[0] = 5;
            out[1].value.data.vector[1] = 90;
            out[1].value.data.vector[2] = -3;
            *result = {};
            return BC_OK;
        };
    }
};
int main() {
    try {
        static_assert(!std::is_copy_constructible_v<Spy>);
        static_assert(std::is_nothrow_move_constructible_v<Spy>);
        static_assert(!std::is_copy_constructible_v<briefcase::ObjectHandle>);
        static_assert(std::is_nothrow_move_assignable_v<briefcase::ObjectHandle>);
        Backend b;
        {
            SpyApi api(&b.api);
            check(b.resolutions == 9, "Nine pinned functions resolved once");
            auto spies = api.FindAll();
            check(spies.size() == 2 && b.last_world == 0, "Typed global discovery");
            auto &local = spies[0];
            check(local.Handle() == 1 && local.IsValid() && local.IsLocallyControlled(), "Local Spy");
            check(!local.IsDead() && !local.IsBot() && local.IsInADS(), "Typed flags");
            check(spies[1].IsBot() && !spies[1].IsLocallyControlled(), "Bot flags");
            b.local_dead = true;
            b.ads = false;
            check(local.IsDead() && !local.IsInADS(), "Flags are live, not cached values");
            auto position = local.GetLocation();
            check(position.x == 1000 && position.y == -20 && position.z == 40, "Position units/conversion");
            check(local.GetVelocity().x == 120, "Velocity conversion");
            auto eyes = local.GetEyesViewPoint();
            check(eyes.location.x == 10 && eyes.location.y == 20 && eyes.location.z == 170 &&
                      eyes.rotation.pitch == 5 && eyes.rotation.yaw == 90 && eyes.rotation.roll == -3,
                  "Named eyes outputs");
            check(local.GetObjectPath() == "World.BPSpy_Squire_C_1", "Object path");
            {
                auto controller = local.GetController(), weapon = local.GetWeaponTool();
                check(controller.Handle() == 10 && weapon.Handle() == 20, "Owned related handles");
                check(!spies[1].GetController(), "Null controller");
                b.no_weapon = true;
                check(!local.GetWeaponTool(), "Null weapon");
            }
            check(b.refs[10] == 0 && b.refs[20] == 0, "Related handles released");
            {
                auto world = api.FindAll(local.Handle());
                check(b.last_world == local.Handle() && b.refs[1] == 2, "World-scoped discovery");
                auto retained = api.FromHandle(local.Handle());
                check(b.refs[1] == 3, "Borrowed Spy retained");
                Spy moved = std::move(retained);
                check(!retained && moved.Handle() == 1, "Move transfers reference");
                rejects([&] { retained.IsDead(); }, "Moved-from Spy rejects calls");
                moved = std::move(world[1]);
                check(moved.Handle() == 2 && b.refs[1] == 2 && !world[1],
                      "Move assignment releases old reference");
                moved.Reset();
                moved.Reset();
                check(!moved && !moved.IsValid(), "Reset is idempotent");
            }
            check(b.refs[1] == 1 && b.refs[2] == 1, "Scoped references released");
            {
                auto foreign = briefcase::ObjectHandle::Adopt(&b.api, b.owned(70));
                rejects([&] { api.FromHandle(foreign.Handle()); }, "Non-Spy rejected before retain");
                check(b.refs[70] == 1, "Failed cast preserves caller ownership");
            }
            rejects([&] { api.FromHandle(0); }, "Null borrowed handle");
            rejects([&] { api.FromHandle(80); }, "Stale borrowed handle");
            b.dead_handles.insert(1);
            check(!local.IsValid(), "Despawn invalidates Spy");
            rejects([&] { local.IsDead(); }, "Despawned Spy cannot invoke");
            rejects([&] { api.FindAll(local.Handle()); }, "Stale world rejected");
            b.dead_handles.clear();
            b.game_thread = false;
            rejects([&] { local.IsDead(); }, "Wrong-thread invocation rejected");
            rejects([&] { api.FindAll(); }, "Wrong-thread enumeration rejected");
            b.game_thread = true;
            b.overflow = true;
            rejects([&] { api.FindAll(); }, "Overflow rejected, not truncated");
            b.overflow = false;
            b.invoke_error = BC_DENIED;
            rejects([&] { local.IsDead(); }, "Capabilities still enforced");
            b.invoke_error = BC_OK;
            b.wrong_kind = true;
            rejects([&] { local.IsDead(); }, "Incorrect result type rejected");
            check(b.refs[90] == 0, "Malformed owned result released");
            b.wrong_kind = false;
            for (int i = 0; i < 100; ++i)
                local.IsDead();
            check(b.resolutions == 9, "No per-call signature lookup");
            check(b.ownership_errors == 0, "No duplicate releases");
        }
        check(b.empty(), "Session releases objects and functions");
        for (unsigned failure = 1; failure <= 9; ++failure) {
            Backend broken;
            broken.fail_resolve = failure;
            rejects([&] { SpyApi api(&broken.api); }, "Partial initialization rejected");
            check(broken.empty(), "Partial initialization fully unwound");
        }
        {
            Backend changed;
            changed.contracts["/Script/DeceiveInc.Spy:IsDead"]["parameters"][0]["type"] = "float";
            rejects([&] { SpyApi api(&changed.api); }, "Live signature drift rejected");
            check(changed.empty(), "Signature drift leaks no handles");
            changed.unreal.invoke_outputs = nullptr;
            rejects([&] { SpyApi api(&changed.api); }, "Missing optional service rejected");
            changed.unreal.size = 8;
            rejects([&] { SpyApi api(&changed.api); }, "Short ABI table rejected");
            changed.api.version = 100;
            rejects([&] { SpyApi api(&changed.api); }, "Wrong host version rejected");
            rejects([&] { SpyApi api(nullptr); }, "Missing host rejected");
        }
        {
            Backend other;
            Spy survivor;
            {
                SpyApi first(&other.api);
                auto found = first.FindAll();
                survivor = std::move(found[0]);
            }
            check(survivor.IsValid() && !survivor.IsDead(), "Spy keeps scoped bindings alive");
            check(b.empty(), "No cache shared between mod owners");
            survivor.Reset();
            check(other.empty(), "Last Spy releases shared bindings");
        }
        std::cout << "PASS " << checks << " Spy contracts\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
