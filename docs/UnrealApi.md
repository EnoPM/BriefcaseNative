# Public `briefcase.unreal` service v1

The API is a C ABI with fixed-size, versioned structures. See `UnrealApi.h` and
the `Unreal.hpp` wrappers.

Unreal pointers remain private to the backend. Mods declare `unreal.reflection`,
`unreal.invoke`, `unreal.hooks` and `unreal.lifecycle` separately. Access to a
service table never bypasses each call's capability check. All operations run on
the game thread after initialization; use `post_game_thread` from
`BriefcaseModLoad`.

## Reflection and invocation

`resolve_function` accepts a complete `/Script` path and exact JSON signature:
`parameterSize`, `parameters` (`name`, `type`, `offset`, optional `return`) and
optional flags. Every name, type, offset and size is compared with live metadata.
`describe_function` exposes that metadata without returning a pointer.

Version 1 accepts compiled `/Script` classes whose lifetime covers the server.
Dynamic Blueprint function metadata is not cached.

Supported values are `int32`, finite `float`, `bool`, one-byte byte/enum, object
and `FVector`. Argument names must be unique, types exact and values representable.
Returns use `BcValue`; unsupported output/reference parameters fail. Invocation
uses an aligned local buffer limited to 4,096 bytes and exposes no raw buffer.

Properties may be read by name with type and container-bound validation. Version 1
does not provide unrestricted property access.

## Handles

Handles belong to one mod. `retain` extends a reference and `release_handle`
releases it. Destroying an object invalidates every handle even if Unreal later
reuses its address.

Hook `self` and function handles are borrowed; retain `self` before storing it.
Object values returned by reads and invocation are owned and must be released.
`on_deleted` reports invalidated handles on the game thread and cannot read the
destroyed object. `ObjectInfo.flags` exposes only `ClassDefaultObject` (16) and
`ArchetypeObject` (32).

## Reflected hooks

`hook` observes a prefix or postfix without suppressing the original call.
`ProcessEvent` and reflected Func thunks are covered with deduplication. Direct
calls to a function's C++ body do not use this transport.

The callback receives an ephemeral call context for `read_argument`. Parameters
are available for `ProcessEvent` and native frames whose `Code` is null. A
non-materialized bytecode frame remains observable through `self`, but argument
reading rejects its memory. The runtime never guesses VM stack layout.

Each registration suppresses its own reentrancy and may remove itself during a
callback. An exception disables the failing callback and still allows the original
call.

Limits are 4,096 handles, 256 metadata records, 128 thunk targets, 512 hooks overall,
64 hooks per mod and 32 nested callbacks carrying argument context. Dispatch is
indexed by `UFunction` and performs no periodic global search or `GUObjectArray`
traversal.

## Shutdown

`BriefcaseModUnload` runs on the game thread when cleanup is requested. The runtime
then removes all owner hooks, subscriptions and tasks and invalidates its handles.
Original thunks are restored after their final registration is removed. Native
libraries remain mapped and hot unload is unsupported.

The stop helper requests cleanup through a PID-specific local event, waits for an
acknowledgment and terminates only the configured installation's executable. An
external forced termination cannot guarantee callback execution.

## Validation

`ReflectionContracts` covers incompatible signatures, ownership, address reuse,
retention, removal during dispatch, reentrancy suppression and exceptions.
`RuntimeProbe` uses only the public SDK to invoke `Abs_Int(-7)` on the
`KismetMathLibrary` CDO, observe one PRE and POST call, verify removal and reject
invalid parameters. It does not simulate a gameplay phase transition.

Handle release and validation use the owning context and do not require
`unreal.find`. Typed reads accept bounded structure paths such as
`HeatState.HeatCount`. Direct C++ calls can be observed through the separate
[native hooks contract](NativeHooks.md).

## Typed writes

The extended `BcUnrealApi` keeps its first 96 bytes and appends `write_property`
and `write_argument`, for 112 bytes total. Existing packages remain compatible;
new packages must check service size. `Services::service` performs this check.

`unreal.write` is required together with `unreal.reflection` for a property or
`unreal.hooks` for an argument. Owner and game-thread checks remain active.

`write_property` accepts the scalar and vector types supported by `read_property`
on a live instance, including bounded structure paths. It rejects CDOs, archetypes,
object pointers, collections and unsupported types. Type, size, numeric bounds and
finite values are checked first. It does not automatically trigger replication or
`OnRep`; the mod must select the correct game event.

`write_argument` works only during PRE on a materialized input parameter. POST,
returns, references/output parameters and VM frames without a buffer fail. The
write is transient and owner-bound. For a native hook, the trampoline receives the
changed argument exactly once and the mod cannot suppress the original call.

`RuntimeProbe` verifies `Abs_Int(-3)` becoming `Abs_Int(-21)` through a PRE write,
then returning 3 after removal. It rejects a wrong type, return write, POST write,
CDO target, expired call and false owner without modifying gameplay state.
