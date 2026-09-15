# BriefcaseNative

Native client and dedicated-server mod framework for Deceive Inc.

This repository contains the shared runtime, Unreal backend, public ABI and SDK,
client menu, server administration, launcher, updater, tests and educational samples.
Application mods live in independent repositories and are not bundled here.

## Build

Windows x64, Visual Studio C++ tools, CMake, PowerShell 7 and Rust are required.
The Unreal dependency requires authorized GitHub access; see docs/UE4SS.md.

    ./scripts/build/Fetch-Dependencies.ps1
    ./scripts/build/Build.ps1
    ./scripts/test/Test.ps1
    ./scripts/client/Package-Client.ps1

Packages are written to dist/Client and dist/Server. Public packages contain no PDB files.
The client package includes only the native overlay sample. The server package contains no mods.

## Development installation

Copy local.settings.example.json to local.settings.json and configure the dedicated test paths.
Deployment validates the destination and preserves installed mod configuration.
See [server launcher](docs/ServerLauncher.md), [client setup](docs/ClientMilestone.md)
and [administration](docs/Administration.md).

## SDK and samples

The SDK provides a stable C ABI and C++ wrappers. See [ModApi](docs/UnrealApi.md),
[ClientModApi](docs/ClientApi.md) and [typed game API](docs/SpyApi.md).
The samples demonstrate logging, runtime inspection and rendering text through the public API.

## Releases

The manually triggered release workflow builds and tests the framework, verifies packages
and creates the server and SDK release assets. See [updates](docs/Updates.md).
Source publication is checked against an explicit file inventory.
