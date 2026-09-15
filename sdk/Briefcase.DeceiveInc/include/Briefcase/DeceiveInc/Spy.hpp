#pragma once
#include <Briefcase/ObjectHandle.hpp>
#include <memory>
#include <string>
#include <vector>

namespace briefcase::deceive_inc {
namespace detail {
struct SpyBindings;
}
struct Vector3 {
    double x{}, y{}, z{};
};
struct Rotator {
    double pitch{}, yaw{}, roll{};
};
struct EyeViewPoint {
    Vector3 location;
    Rotator rotation;
};

class SpyApi;

// A typed, owned reference to a live Spy, including inherited Pawn/Actor operations.
// All calls, moves and destruction belong on the game thread. No engine pointer is exposed.
// A reference does not keep a game character alive; calls on despawned objects fail.
class Spy {
    friend class SpyApi;
    std::shared_ptr<detail::SpyBindings> bindings_;
    ObjectHandle object_;
    Spy(std::shared_ptr<detail::SpyBindings> bindings, ObjectHandle object) noexcept;
    const detail::SpyBindings &Bindings() const;

  public:
    Spy() = default;
    Spy(const Spy &) = delete;
    Spy &operator=(const Spy &) = delete;
    Spy(Spy &&) noexcept = default;
    Spy &operator=(Spy &&) noexcept = default;
    ~Spy() = default;

    explicit operator bool() const noexcept { return bool(object_); }
    BcHandle Handle() const noexcept { return object_.Handle(); } // Borrowed; do not release.
    bool IsValid() const { return object_.IsValid(); }
    void Reset() noexcept;

    bool IsDead() const;
    bool IsBot() const;
    bool IsLocallyControlled() const;
    bool IsInADS() const;
    Vector3 GetLocation() const;           // Unreal units: centimetres.
    Vector3 GetVelocity() const;           // Centimetres per second.
    EyeViewPoint GetEyesViewPoint() const; // Rotation in degrees.
    std::string GetObjectPath() const;

    // Owned references for interoperability until typed Controller/Weapon wrappers exist.
    ObjectHandle GetController() const;
    ObjectHandle GetWeaponTool() const;
};

// One session per mod. Construct on the game thread after Unreal is ready.
// Resolves and validates signatures once. No global cache or dependency on UE4SS/ImGui.
// Spy instances share the session's lifetime; release them during mod unload.
// Errors throw within the mod. Catch them before returning from a C ABI callback.
class SpyApi {
    std::shared_ptr<detail::SpyBindings> bindings_;

  public:
    explicit SpyApi(const BcApi *api);
    // World context is a borrowed handle owned by this mod; 0 searches live instances globally.
    // The backend rejects collections exceeding 512; it never silently truncates.
    std::vector<Spy> FindAll(BcHandle worldContext = 0) const;
    // Checks the actual Unreal type and retains a borrowed handle owned by this mod.
    Spy FromHandle(BcHandle borrowed) const;
};
} // namespace briefcase::deceive_inc
