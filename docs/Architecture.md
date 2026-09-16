# Architecture decisions

BriefcaseNative keeps the `version.dll` proxy and bootstrap outside the loader
lock, PE build profiles, manifest inventory before loading, topological dependency
ordering and timestamped diagnostics. It replaces the former managed host with a
native C++ host. Reference projects were inspected read-only.

On Windows, the proxy forwards all 17 `version.dll` exports to the System32 DLL,
captures the initial thread and loads `Briefcase.NativeHost.dll` on a worker. The
host identifies the game build before installing a backend. Linux uses the same
host and ABI through its native launcher and preload bootstrap.

The mod ABI is a C header with sizes, versions, error codes, function tables,
capabilities and opaque handles. Its C++ wrapper is header-only. STL types, UE4SS
types and memory ownership across CRT boundaries never cross the ABI. Native mods
remain trusted code; capabilities do not sandbox a hostile DLL or shared library.

The Unreal backend compiles libraries from RE-UE4SS v3.0.1 rather than the UE4SS
application. Upstream GUI, Lua, console, dumper and mod-loader source remains in the
checkout but is excluded from the distributed target. The Rust `patternsleuth`
scanner remains an internal UE4SS dependency; mods and the public API are C++.

## Threads and lifetime

The Windows prototype starts Unreal initialization from `Sleep` on the game's
initial thread after a five-second delay, allowing the game to construct its
classes before synchronous UE4SS initialization. This is a prototype bootstrap
bridge rather than a general lifecycle API. It never reads Unreal objects from a
worker thread.

After initialization, `ProcessEvent` and the bootstrap bridge drain a bounded
queue: at most 1,024 pending callbacks and 32 processed per pass. An empty pass
only checks thread identity and atomic flags. There is no global object traversal
per frame. Mod callbacks must remain short and contain all exceptions.

Handles belong to one mod, are never reused within a process, are invalidated by
the UObject deletion listener and are checked through UE4SS before use. The limit
is 4,096 handles. A handle does not keep its Unreal object alive. Logical unload
invalidates callbacks and owned resources; native libraries remain mapped until
process exit, and runtime hot reload is unsupported.

## Packages and dependencies

Each mod has one directory named by its ID, a manifest and a platform-native entry
library. Versions are strict numeric triples. A dependency declares an inclusive
minimum version. Duplicate IDs, cycles and missing or outdated dependencies
invalidate the inventory before loading. Failure to load a library prevents its
dependents from starting.

Manifests declare `client`, `server` or `both`; the opposite environment never
loads the package. Client rendering, ImGui and input modules are absent from server
packages.

## SDK boundaries

Typed wrappers such as `SpyApi` hide reflected Unreal calls behind public handles.
Build-specific native hooks require exact executable identity and byte validation.
No SDK generated for another build is presented as compatible with the current
server.

CMake orchestrates C++, MASM, UE4SS libraries and the internal scanner. Visual
Studio and CLion can open the root `CMakeLists.txt` directly.
