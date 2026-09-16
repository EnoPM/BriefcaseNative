# Milestone 1 — native prototype

This document records the original Windows server prototype. Later milestones have
extended the architecture and packaging; current installation instructions are in
the repository README.

## Result

The Release x64 prototype was deployed to the authorized dedicated test copy. Its
local `StartBriefcaseNativeServer.ps1` sat beside
`DeceiveIncServer-Win64-Shipping.exe` and always supplied that Win64 directory as
the process working directory.

The initial package contained `version.dll`, `Briefcase.NativeHost.dll` and the
educational `Briefcase.NativeHello.dll`, plus its launcher, manifest and licenses.
Deployment generated local launcher configuration. No game file, PDB, C++ source or
header entered the package. SHA-256 comparison found all 163 deployed files
identical to their packaged copies.

## Identified build

- Executable: `DeceiveIncServer-Win64-Shipping.exe`, 92,886,528 bytes
- SHA-256: `78afe1dbeecb09027c274def4f0ac855b447dc52ffe3cd9482c1be4341b0dae6`
- PE timestamp: `0x6A966107`; `SizeOfImage`: `0x05B60000`
- Unreal log identity: `4.27.2-72378+++deceive+dev+main`
- Advertised server version: `1.12.02.72378`
- Enabled profile: server only; the ABI and packaging model were designed to be shared

The runtime rejected another executable name, PE identity, SHA-256 or working
directory that differed from the executable directory.

## Functional validation

- 36 native checks covered versions, manifests, dependencies, cycles,
  capabilities, ambiguous JSON, handle isolation, simulated destruction, address
  reuse and release.
- 15 PowerShell launcher checks captured real `Start-Process` arguments, validated
  without launch and rejected wrong directories and duplicate startup. Fixtures
  never executed the game server.
- An independent C translation unit compiled the ABI and asserted `BcResult`,
  `BcHandle`, `BcApi` and `BcBuild` sizes and the context offset.
- Both CTest targets and every PowerShell contract passed.
- All 17 proxy exports were verified. The runtime imported no Lua, GLFW or D3D;
  UE4SS GUI, console and mod-loader targets were excluded.
- `NativeHello` verified framework/build identity, rejected an undersized output
  structure and worker-thread search, found `/Script/DeceiveInc.Spy` on the game
  thread and rejected a released handle.
- The game loaded Silver Reef and reported `ServerStatus=Lobby`.

## Measured initialization

Times from runtime entry during the final prototype launch:

| Stage | Time |
| --- | ---: |
| Game identity checked and bootstrap armed | 77 ms |
| Initialization began on the game thread | 6,553 ms |
| UE4SS initialization itself | 158 ms |
| Backend ready | 6,711 ms |
| NativeHello loaded and identity checked | 6,811 ms |
| Unreal callback and handle validation | 6,812 ms |

The initial five-second delay was deliberate. The bootstrap patched only the
authorized executable's `Sleep` import. Map-load times in the game log do not
measure framework overhead.

`scripts/test/Measure-Server.ps1` writes whole-process stability samples to
`artifacts/stability-*.json`. Short observations cannot prove the absence of leaks
during a multi-hour session.

## Issues corrected

- The CMake on `PATH` was too old, so scripts selected the CMake and Ninja bundled
  with the Visual Studio installation found by `vswhere`.
- PolyHook exported include paths incompatible with current CMake; the integration
  added `BUILD_INTERFACE` expressions.
- Targeted reproducible UE4SS patches fixed an implicit `byte` alias, a templated
  `find` call and a missing enum declaration.
- AsmJit/Unreal macro collisions were isolated behind backend includes.
- A rejected `Sleep` detour was replaced by one atomic import-table slot change in
  the authorized server executable; no system DLL was patched.
- One intermediate process disappeared before its next sample without a captured
  exit code or crash report. Identical later runs did not reproduce the event, so
  extended soak testing remained necessary.

## Original manual test procedure

1. Check `Win64/Briefcase/Logs/BriefcaseNative.log` for backend readiness, sample
   load, identity validation, Spy lookup and released-handle rejection.
2. Check `DeceiveInc/Saved/Logs/DeceiveInc.log` for Silver Reef and Lobby.
3. Run `./scripts/test/Measure-Server.ps1 -DurationSeconds 3600 -SampleSeconds 60`.
4. Stop, build, test, preview deployment, deploy, validate launch and start through
   the scripts under `scripts/deploy`, `scripts/build` and `scripts/test`.
5. Invoke the installed launcher from another directory and confirm that Win64
   remains the working directory.

No firewall rule was modified during the prototype. Real client connectivity was
left to the manual playtest. Client profiles, old-mod ports, hot reload and
long-term stability guarantees were outside this milestone.

## Final observation

The prototype ran for 180.01 seconds with 13 post-startup samples. Whole-process
CPU averaged 15.03% of one logical core. Private memory changed from 795.84 to
796.62 MiB, Windows handles from 667 to 657 and threads from 77 to 74. No stop was
observed and the main-window handle remained null.

These values include the server, bots and networking and cannot isolate Briefcase
overhead or prove long-term leak freedom. Detailed data was recorded in
`artifacts/stability-20260913-203141.json`.
