# BriefcaseNative 0.3.0 — first client variant

This document records the Release x64 client milestone built and deployed on
September 14, 2026. Automated validation passed and later user playtests completed
the input and connection checks.

## Architecture

| Component | Role and main files |
| --- | --- |
| NativeHost | Shared identity, manifests, dependencies, capabilities, loading and shutdown: `GameProfile.hpp`, `ClientBuild.json`, `ClientStartup.hpp`, `ClientHost.inc`, `ClientServices.inc` |
| UnrealBackend | Headless UE4SS integration linked into NativeHost; `ObjectDeleteListener.hpp` manages Unreal lifetime |
| Client.Rendering | Client-only DLL: `ClientBridge.h`, `Rendering.cpp`; D3D11, single ImGui context, resources and callbacks |
| Client.Input | Internal `Input.hpp`, `Input.cpp` and `InputPolicy.hpp` library |
| Client.Menu | Internal `Menu.hpp` and `Menu.cpp`; dark purple Home and Mods UI |
| ModApi | Existing common C ABI |
| ClientModApi | `sdk/Briefcase.ClientModApi/include/Briefcase/ClientModApi.h`; see [Client API](ClientApi.md) |
| NativeOverlaySample | `samples/Briefcase.NativeOverlaySample/Overlay.cpp` and client manifest |

NativeHost is the same binary in both Windows packages and imports no D3D, DXGI or
ImGui. Rendering loads dynamically only in client mode, with Input, Menu and ImGui
linked into that DLL. The server never loads these components.

Dear ImGui **1.91.9b** is pinned under `third_party/imgui-1.91.9b`, with its MIT
license and SHA-256 inventory in `SOURCE.json`. The framework owns one context;
mods do not ship private copies. Segoe UI loads from Windows with an embedded-font
fallback and is not redistributed. Statically linked third-party sources remain
separate and their licenses ship under `Briefcase/Licenses`.

The earlier `Briefcase.Native.Rendering` reference was inspected read-only. Its
small WARP swap chain discovers DXGI methods, MinHook installs hooks, the process's
Unreal window is filtered and graphics state is restored. The client variant keeps
that model with lazy resource creation and separate input handling.

## Startup and resources

Real-game testing confirmed **Direct3D 11 on an RTX 4070 Ti**. The detected Unreal
build was 4.27.2-72378 with backend family 4.27.

- Executable: `DeceiveInc-Win64-Shipping.exe`
- PE timestamp: `6A96564B`; image size: `06283000`
- SHA-256: `b753b51f4d51adc41f7577e7e01521a62f941711ec68330cf22546f81c788a87`

The proxy prepares exports and starts the client host on a worker without blocking
the executable entry point. Graphics discovery uses a lower-priority worker.

Startup creates no ImGui context, font atlas or graphics resource. Creation waits
until F1 is open, at least 30 frames and two seconds have rendered, and shader
compilation has completed. A bounded log reader on the host worker recognizes
`PrecompileCompleted` or arrival at the login screen and ignores entries older than
the process. Without that signal, the menu remains deferred.

Opening F1 then triggers Unreal discovery on the game thread. Client mods that need
no Unreal capability may load earlier. The overlay sample waits for the context and
continues drawing after the menu closes.

The backend handles `Present`, `ResizeBuffers` and swap-chain destruction. It frees
the render target before resize and recreates it afterward. A new swap chain or
device rebuilds resources. `DEVICE_REMOVED` and `DEVICE_RESET` invalidate rendering
until the game recreates the device. All render and depth targets are restored in
addition to ImGui's DX11 state. At smaller resolutions, the menu remains inside the
viewport and its content scrolls.

## Input

F1 toggles once per press, including while a modifier is held. Alt+Tab preserves an
open menu.

With the menu closed, messages and imported functions pass through to the game. A
key still held while closing may remain suppressed until release to avoid accidental
movement. Opening the menu emits releases for gameplay keys already passed through.

With the menu open, Briefcase intercepts messages needed by ImGui. `WM_INPUT` is
completed by `DefWindowProc` without reaching gameplay, while Raw Input registration
is preserved. Alt+Tab, Alt+F4, Shift+Tab and system modifiers pass through.

Only executable imports for keyboard, `ClipCursor`, `SetCursor`, `GetCursorPos` and
`SetCursorPos` are patched. Requested clip, position and cursor shape are remembered.
The game keeps a coherent virtual position while the menu uses the physical cursor,
and the requested state returns on gameplay resume. There is no synthetic
`ShowCursor` counter and no interception in other applications or overlay DLLs.

## Packages and development scripts

The milestone produced separate `dist/Client` and `dist/Server` trees. The client
contained the proxy, client launcher, shared host, rendering DLL and educational
overlay sample. The server package contained no graphics component.

Local paths come from ignored `local.settings.json` or explicit parameters. A
deployment destination must exactly match the authorized Win64 directory. Path
traversal, prefix collisions and junctions fail before replacement. The client must
be stopped. Deployment preserves `loader.json`, skips identical files, backs up
replaced files and verifies copied hashes without cleaning unrelated mods or data.

The installed launcher starts Shipping directly with Win64 as working directory,
even when invoked elsewhere.

Logs and metrics:

- `Briefcase/Briefcase.log`: framework log
- `Briefcase/Logs/DeceiveInc-client.log`: game log from the launcher
- `Briefcase/Logs/client-metrics.json`: requested or shutdown snapshot
- `Briefcase/Logs/last-launch.json`: executable, working directory, PID and arguments

Per-frame averages accumulate without frame-by-frame logging. Resource events,
menu toggles and requested snapshots are logged.

```powershell
.\scripts\client\Build-Client.ps1
.\scripts\client\Test-Client.ps1
.\scripts\client\Package-Client.ps1
.\scripts\client\Stop-Client.ps1
.\scripts\client\Deploy-Client.ps1
.\scripts\client\Start-Client.ps1
.\scripts\client\Measure-Client.ps1 -Seconds 10
```

Packaging runs tests before producing both packages. The stop script also checks a
normal game exit so a crash cannot be reported as successful shutdown.

## Measurements

A real session on the corrected binary opened F1 and then exited normally. Data was
recorded in `artifacts/client-fixed-live-metrics.json` and
`client-fixed-framework.log`.

| Measurement | Value |
| --- | ---: |
| Complete worker bootstrap, including executable hash | 86.048 ms |
| Worker hook installation | 92.723 ms |
| Unreal initialization on first open | 176.503 ms |
| ImGui context | 0.039 ms |
| Font atlas | 1.588 ms |
| ImGui GPU resources | 3.326 ms |
| Before first open, 6,393 frames | 2.453 µs/frame |
| Closed with sample, 772 frames | 35.569 µs/frame |
| Open, 19,662 frames | 57.736 µs/frame |
| Sample callback average at shutdown | 5.386 µs |

Proxy export forwarding under loader lock measured 0.222 ms in the first session.
Earlier sessions measured 2.627/35.566/63.120 µs and
2.372/34.114/61.295 µs for pre-open, closed and open rendering. These CPU values
cover framework work before the original `Present`, including callbacks but
excluding wait time and GPU cost. First-time resource creation is separate.

With an existing cache, shader completion measured 6.917 s vanilla and 6.985 s
with Briefcase, a 68 ms difference for that pair. This is not a cold-cache result.
WARP fixture measurements validate resource cycles but do not replace real-GPU data.

## Tests and shutdown crash correction

All 12 CTest suites and 50 PowerShell checks passed. The D3D11 fixture covers lazy
context creation, public-ABI sample rendering, owner checks, removal during dispatch,
self-removal, contained exceptions, sample cleanup, real DXGI resize, multiple render
target restoration, device/swap-chain destruction and recreation, return to the
original `Present` and virtual cursor imports.

The reported shutdown crash was real. The crash report stated that every UObject
delete listener must be unregistered before UObject-array shutdown. The production
listener had not removed itself from `OnUObjectArrayShutdown`.
`ObjectDeleteListener` now unregisters before owner cleanup, matching the UE4SS
contract. `ObjectListenerShutdown` uses the production observer and verifies the
list is empty even after mods have already stopped.

A real corrected session shut down normally after F1 and Unreal initialization.
The observer logged its removal, the game reached `LogExit: Exiting.`, and no
critical error or matching crash report appeared. Evidence was stored in
`artifacts/client-shutdown-verification.json`, `client-fixed-framework.log` and
`client-fixed-game.log`.

## Manual playtest checklist

1. Wait for shaders, open F1 and inspect Home, Mods, backend status and timings.
2. Test mouse, clicks and scrolling; no click should reach gameplay.
3. Close F1, confirm the sample remains visible and test camera and movement.
4. Hold a movement key, open, release and close; verify no stuck movement. Repeat
   while holding through closure, then release and press again.
5. Alt+Tab with the menu open, return and verify menu, mouse and F1. Confirm Shift+Tab
   works only when intentionally pressed.
6. Change resolution and switch windowed/fullscreen, then retest rendering and input.
7. Use Alt+F4 and confirm a normal exit without a crash report or UE4SS console.
