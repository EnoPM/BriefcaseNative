# Typed C++ API: Spy

Include `<Briefcase/DeceiveInc/Spy.hpp>` and link the distributed SDK target
`Briefcase::DeceiveInc`. The library compiles into the mod and adds no DLL or public
dependency on UE4SS or ImGui. The existing C ABI remains the only contract between
the mod and runtime.

## Usage

Run this code in a game-thread task after Unreal initialization:

```cpp
using briefcase::deceive_inc::SpyApi;

SpyApi spies(host); // Keep this alive for as long as the mod uses Spy objects.

for (auto& spy : spies.FindAll()) {
    if (spy.IsDead())
        continue;

    bool isBot = spy.IsBot();
    auto position = spy.GetLocation();
    auto view = spy.GetEyesViewPoint();
}
```

For a C# developer, `Spy` is similar to an object that wraps native access.
`SpyApi` centralizes construction and the functions each object uses. Briefcase
hides reflection and conversions. C++ `auto` lets the compiler infer a type in a
way similar to C# `var`.

## Operations

- `IsDead`, `IsBot`, `IsLocallyControlled`, `IsInADS`: Boolean game state.
- `GetLocation`, `GetVelocity`: `Vector3` in centimeters and centimeters/second.
- `GetEyesViewPoint`: position and pitch/yaw/roll rotation in degrees.
- `GetObjectPath`: Unreal instance path without interpreting the agent name.
- `GetController`, `GetWeaponTool`: owned `ObjectHandle` values, possibly empty.
- `IsValid`: whether the reference is valid at the time of the call.
- `Handle`: a borrowed reference for generic ABI operations.
- `Reset`: early, idempotent release.

`FindAll()` finds every live Spy `UObject`, including dead characters.
`FindAll(localSpy.Handle())` limits the search to that character's world. The
backend limit remains 512 objects; overflow produces an error rather than a
silently truncated list. `FromHandle(handle)` verifies the runtime type and retains
a borrowed reference owned by the same mod. The runtime rejects cross-mod handles.

## Lifetime and errors

`Spy` and `ObjectHandle` automatically release references when they leave scope,
similar to an `IDisposable` used with C# `using`. They cannot be copied;
`std::move` transfers the reference and empties the source. Retaining a reference
does not stop Unreal from destroying the character. `IsValid` does not replace
handling invocation errors when an object disappears.

Every `Spy` keeps its session functions alive. Clear stored collections and
references before releasing `SpyApi` in `BriefcaseModUnload`. All such operations,
including destructors, run on the game thread. No global cache mixes mod owners.

Required capabilities are `unreal.reflection`, `unreal.invoke` and `game-thread`
for task scheduling. The typed layer preserves all ABI and backend checks.
