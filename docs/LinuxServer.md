# Linux dedicated server

Linux targets the dedicated server only; the client remains Windows-only. The
Linux host shares the Windows loader, public C ABI, configuration, reflection,
hook dispatch and TLS administration services. Startup patches, native detours,
supervised restart and updates before launch are implemented.

Automated tests and actual headless startup/restart passed under WSL2. Connected
player sessions and extended production use still need validation. Version 0.5.0
replaces the Python launcher/update runtime shipped in 0.4.0 with native C++.

## Architecture

| Component | Linux implementation |
| --- | --- |
| Launcher | Native C++ executable; direct Shipping execution in Binaries/Linux |
| Bootstrap | libBriefcase.ServerBootstrap.so intercepts main through ELF preload |
| Host | libBriefcase.NativeHost.so, common mod loader and services |
| Unreal | Pinned headless UE4SS libraries and engine-thread scheduling |
| Native hooks | PolyHook2 x64 detours with SysV calling convention |
| Startup | ELF executable ranges, exact bytes, decoded instructions and transactions |
| Administration | Shared Mbed TLS protocol, POSIX sockets/files and private supervisor channel |
| Mods | Independent .so packages using the public C ABI |

No UE4SSProgram, Lua loader, ImGui, input services or graphics libraries are linked.
The host loads after C++ static initialization, before game main. Startup mods
finish before game initialization; Unreal discovery runs on a worker. Unreal
calls and callbacks execute on the engine thread. The bootstrap clears LD_PRELOAD
before the game starts other programs.

The host and SDK-built mods locally bind their static C++ runtime, preventing the
game's exported allocator from handling memory owned by another runtime. C++
owning objects never cross the public C ABI.

The executable SHA-256 is checked before patching or discovery. BcBuild keeps its
layout; PE timestamp/image-size fields are zero on Linux. Native RVAs are relative
to the first offset-zero PT_LOAD segment. Linux requires its own exact profiles.

BcStartupApi appends stage_code after its existing fields. Old mods retain the old
prefix; new mods request the larger service size. Bounded, same-length code
windows permit validated integer instructions and local forward conditional
branches. Calls, memory operands and stack changes are refused. Transactions check
overlaps/expected bytes, back up INI files, verify writes and roll back on failure.
Code patches affect memory only. Client packages are skipped; server packages
require .so entries rather than Windows DLLs.

## Build

Tested: Ubuntu 24.04 x86_64, Clang/LLD 19, CMake 3.28+, Ninja, Python 3,
Rust 1.97.1 and GCC 13 runtime files. Older distributions have not been validated.

~~~sh
sudo apt-get install --no-install-recommends build-essential clang-19 lld-19 cmake ninja-build git python3 libssl-dev pkg-config libcurl4-openssl-dev libarchive-dev
rustup toolchain install 1.97.1 --profile minimal
export RUSTUP_TOOLCHAIN=1.97.1
bash scripts/linux/prepare-backend.sh artifacts/linux/ue4ss-reference artifacts/linux/ue4ss-build
bash scripts/linux/build-server-host.sh artifacts/linux/ue4ss-reference artifacts/linux/ue4ss-build artifacts/linux/ue4ss-build/_deps
~~~

The caller needs authorized access to the upstream Unreal submodule. Preparation
fetches pinned sources and applies the reviewed changes in cmake/patches.
Restricted dependencies stay outside the public source inventory.

- UE4SS Linux: NullPrism/RE-UE4SS-Linux, 7894d53f6e13011a16445f28e6f7cd46d58c72cc.
- patternsleuth: 23d13d7471c854fb15b586deb2f2678a1b7bc690.
- Required local changes: cmake/patches/README.md.

BRIEFCASE_BUILD_JOBS limits parallelism; BRIEFCASE_LINUX_HOST_BUILD_DIR selects
the output directory. Never reuse a Windows CMake build directory.
The portable foundation also builds without Unreal sources:

~~~sh
bash scripts/linux/build-server-core.sh
~~~

## Download, package and deploy

The server distribution contains no Python, shell scripts, or managed runtime.
Launch `./Briefcase.ServerLauncher` directly from the installation's Binaries/Linux
directory (launching its absolute path elsewhere also uses Binaries/Linux as the
game's working directory). Python remains a development/CI requirement only.

The native launcher dynamically links the distribution's maintained libcurl and
libarchive libraries. On Ubuntu 24.04, install runtime libraries with:

~~~sh
sudo apt-get install --no-install-recommends libcurl4t64 libarchive13t64 ca-certificates
~~~

These are native shared libraries, not external curl/unzip commands. HTTPS uses
certificate and hostname verification, and validates each GitHub redirect.

~~~sh
bash scripts/linux/download-server.sh /path/to/steamcmd.sh /path/to/isolated-server
python3 scripts/linux/package-server.py --build build/linux-server-host --backend-source artifacts/linux/ue4ss-reference --backend-deps artifacts/linux/ue4ss-build/_deps --output dist/Releases
python3 scripts/linux/package-sdk.py --json-source build/linux-server-host/_deps/json-src --output dist/LinuxSDK
~~~

For an overridden FetchContent JSON directory, pass --json-source to the server
packager too. The ZIP contains stripped native binaries, licenses, translations,
launcher and updater, with an exact manifest of paths, sizes, hashes and modes.
It contains no application mods, game files, source dependencies, secrets, PDBs
or client components. Independent mods use the public SDK in their own repositories.

The development deploy helper requires an isolated installation marked by the
download helper. It rejects linked paths and a running server, verifies the game
hash, and installs only managed framework files:

~~~sh
python3 scripts/linux/deploy-server.py --server /path/to/isolated-server --launcher build/linux-server-host/Briefcase.ServerLauncher --archive dist/Releases/BriefcaseNative-Server-linux-x64-0.5.0.zip
cd /path/to/isolated-server/DeceiveInc/Binaries/Linux
./Briefcase.ServerLauncher
~~~

The native executable also installs an archive directly, without Python:

~~~sh
/path/to/extracted/Briefcase.ServerLauncher --server /path/to/DeceiveIncServer-Linux-Shipping --install-archive /path/to/package.zip
~~~

This validates the game identity and package, holds the installation lock, refuses
a running game, and preserves mods and user configuration. Old framework scripts
listed in the installed manifest are removed transactionally. A 0.4.0 installation
must use this manual installation once: its older updater cannot accept the new
package layout. New installations simply extract the native archive into Binaries/Linux.
Package versions follow CMakeLists.txt; published assets must never be overwritten.

## Supervision and updates

The launcher uses Binaries/Linux as cwd and passes unattended, null-RHI,
no-console, no-splash and no-sound flags. Extra arguments are preserved without
shell evaluation. A per-installation lock prevents concurrent launchers.
Unexpected game exits are reported without an automatic crash/restart loop.
SIGINT/SIGTERM stops the child, with bounded escalation if necessary.

Briefcase/updater.json configures updates. Deployment copies the example only if
the file is absent. With enabled=true, the supervisor checks the configured GitHub
repository before first launch and before administration restarts. A newer stable
Linux asset is installed before the game starts. Network failure or a release
without Linux assets keeps the installed version.

The updater verifies the GitHub digest, ZIP inventory, executable identity and
per-file hashes. Mods and user configuration are outside its managed scope.
Durable backups and a journal allow recovery after interrupted installation.
Recovery runs even with updates disabled. A damaged backup blocks launch rather
than starting mixed versions. After installation the supervisor reloads its
updated executable before starting the game. No interpreter or shell is invoked.

Remote restart uses an inherited Unix sequenced-packet socket. The game verifies
the parent's PID/UID, then receives acknowledgement before replying to the admin
client. Commit follows transmission of the TLS response. The supervisor waits
for termination, checks updates and starts a new process with the same arguments.
A directly launched game cannot request an unsupervised restart.

Logs are Briefcase/Logs/launcher.log and Briefcase/Logs/BriefcaseNative.log.

## Administration

~~~sh
./Briefcase/Tools/Briefcase.AdminSetup "$PWD/Briefcase" 127.0.0.1 50002 127.0.0.1:50002
~~~

Setup generates a strong password without printing it. The password stays in
plaintext in Admin/server.json as requested. The Linux TLS key uses der-v1:
encoding in that same mode-600 private file. It does not depend on DPAPI.
Provision a new identity when moving from Windows and pair its new fingerprint.
Admin/pairing.json contains public connection information only.

Use a Linux filesystem or a WSL mount with metadata so private permissions can
be enforced. Setup refuses to write secrets otherwise. The same Windows client
manages Linux status, mods, configuration, balance, logs and restart over TLS.
Server settings use DeceiveInc/Saved/Config/LinuxServer/TripwireServer.ini.
Administration does not automatically follow a custom game INI override.
Without Admin/server.json, administration is disabled.

## Manual GitHub release

linux-server-core.yml checks the portable foundation. linux-server-release.yml
builds/tests the full host and adds Linux assets to an existing release for the
same commit. Existing assets are never replaced.

1. Configure UPSTREAM_READ_TOKEN with read access to the upstream Unreal submodule.
2. Increment the version and publish the reviewed source changes normally.
3. Run the Windows release workflow with draft=true to create the release and SDK.
4. Run Add Linux server release at that same commit/version. Keep publish=false
   for review, or enable it to publish the verified existing draft.

Both workflows share a concurrency group. Build jobs have contents:read;
contents:write is confined to publication. Dependencies and mod sources are not
uploaded. The 0.4.0 Windows/Linux release workflows passed on GitHub. New source
changes require their own builds and tests before publication; local publication
contracts substitute the CLI and do not write to GitHub.

## Validation and remaining playtest

Examined Steam public build 25107754, Linux depot 5007712:
- ELF build ID: da82586eeb5f7e80.
- SHA-256: b0b275eac71bb8314b8afb5b36368d882faefafc993d5eac05bb5956a7334ef7.
- Unreal: 4.27.2-72378+++deceive+dev+main.

Tests cover executable patch validation/execution, rejected patches, reflection,
ABI, TLS interoperability, update rollback/recovery, hostile archives, release
checks, exclusive launch and acknowledged restart. Native updater contracts also
terminate a real installer process mid-transaction to exercise recovery. Release
CI installs and runs the launcher in an isolated root with no Python or shell,
including a complete acknowledged restart. RuntimeProbe passed its 20
engine checks in the actual game. A Windows CPython/OpenSSL peer authenticated
over TLS 1.3, inspected the server, requested restart, reconnected and verified
all five local test modules reloaded.

One local run reached bootstrap completion in about 1.0 s, Unreal/mod readiness
around 6 s and TLS readiness around 6.5 s. These are single-machine observations,
not performance guarantees.

Still test a connected-player session, actual gameplay effects, admin edits,
disconnect/reconnect, and restart after a completed match. Extended uptime and
a clean Ubuntu installation remain to validate. Startup and armed hooks alone
do not prove an independent mod's player-facing behavior.
