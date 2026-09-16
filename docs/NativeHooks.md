# Validated native hooks

The public `briefcase.native-hooks` service, version 1, complements reflected hooks.
It requires the `native.hooks`, `unreal.hooks` and `unreal.reflection` capabilities.
A mod first resolves a `UFunction` with its exact signature, then supplies a
build-specific `BcNativeSite`.

The runtime verifies the server SHA-256, UHT function address, complete executable
window (5–512 bytes), Zydis instruction boundary for the relative `CALL` or `JMP`,
its destination and the target prologue bytes (16–128 bytes). Every address must be
inside an executable section. Any mismatch prevents installation; no heuristic
address selection is allowed.

Initial signatures cover x64 methods returning `void` with no parameter or one
`float`, `bool` or `UObject*` parameter. C++ references, return values and structures
are unsupported. The private backend uses MinHook 1.3.4. Relocation and the brief
suspension of other threads occur only while installing or removing a hook. No game
DLL is changed on disk. Dependency licenses and hashes ship with the package.

`BcHookEvent` PRE and POST callbacks run on the game thread with `NATIVE` transport.
They read arguments through `read_argument` and objects through public handles. A
call made outside the game thread runs the original function only. The original is
always called once. PRE may change an argument through `write_argument` when the mod
has `unreal.write`; suppressing the original is not exposed.

Reflected calls may also pass through a native body. Use one transport for a given
modification; separate observers may document both.

`unhook` removes one owner registration. Removing the final registration restores
the code. If removal occurs inside a callback, restoration waits until that call
returns. Trampolines and DLLs remain mapped until process exit. Server shutdown also
removes forgotten registrations. Limits are 32 targets, 64 registrations per owner,
512 registrations overall and 32 nested callbacks.

Property reads accept bounded structure paths such as `HeatState.HeatCount`: at
most eight segments, no implicit pointer traversal, no collections and every offset
checked against its container. Primitive results are copied and object results are
owned handles. An owner may validate or release any of its handles regardless of
which API created them.

`NativeHookContracts` verifies windows, limits, instruction boundaries and paths,
then installs a real float detour in a test process. It checks one original call,
an unchanged value, removal and exact byte restoration. A mod must also validate
the service against the dedicated server signatures before it is considered loaded.
