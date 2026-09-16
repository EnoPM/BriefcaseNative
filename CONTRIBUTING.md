# Contributing to BriefcaseNative

Thank you for helping improve BriefcaseNative. This document covers source builds,
tests and releases. Player installation belongs in the [README](README.md).

## Repository boundaries

This repository contains the shared host, Unreal backend, public APIs, launchers,
administration services, SDK, tests and educational samples. Application mods live
in independent repositories and must not be copied into this repository or bundled
with a framework release. The only approved mod packages here are the educational
samples already present under `samples/`.

Keep common client/server code in the shared runtime. Graphics, ImGui and input
code must remain client-only, and a server package must never gain a graphical
dependency. Public ABI structures are append-only and no C++ owning object or
exception may cross a DLL/shared-library boundary.

See [repository boundaries](docs/Repositories.md), [architecture](docs/Architecture.md)
and [the Unreal backend notes](docs/UE4SS.md) before changing those areas.

## Requirements

Windows builds require:

- Windows x64 and Visual Studio C++ tools;
- CMake, Ninja and PowerShell 7;
- Rust 1.97.1;
- authorized read access to the pinned Unreal dependency described in
  [docs/UE4SS.md](docs/UE4SS.md).

The full Linux server build is tested on Ubuntu 24.04 x86_64 with Clang/LLD 19,
CMake 3.28 or newer, Ninja, Python 3, Rust 1.97.1, OpenSSL development files,
libcurl and libarchive. Detailed dependency commands and the pinned upstream
revisions are in [docs/LinuxServer.md](docs/LinuxServer.md).

## Build and test on Windows

Fetch the pinned dependencies, build Release x64, run the tests and prepare the
verified packages:

```powershell
./scripts/build/Fetch-Dependencies.ps1
./scripts/build/Build.ps1
./scripts/test/Test.ps1
./scripts/client/Package-Client.ps1
```

Local game deployment is optional. Copy `local.settings.example.json` to the
ignored `local.settings.json`, then configure only dedicated test installations.
Deployment helpers validate those paths and preserve installed mod data. Never
commit local paths, generated credentials, game files, build output or PDBs.

## Build and test the Linux server

```bash
sudo apt-get install --no-install-recommends build-essential clang-19 lld-19 cmake ninja-build git python3 libssl-dev pkg-config libcurl4-openssl-dev libarchive-dev
rustup toolchain install 1.97.1 --profile minimal
export RUSTUP_TOOLCHAIN=1.97.1
bash scripts/linux/prepare-backend.sh artifacts/linux/ue4ss-reference artifacts/linux/ue4ss-build
bash scripts/linux/build-server-host.sh artifacts/linux/ue4ss-reference artifacts/linux/ue4ss-build artifacts/linux/ue4ss-build/_deps
```

The portable foundation can be checked separately with:

```bash
bash scripts/linux/build-server-core.sh
```

Do not reuse a Windows CMake directory for Linux. The published Linux package must
remain fully native and contain no Python, PowerShell or shell runtime files.

## Tests and documentation

Add tests for behavior and boundaries that could regress. Avoid tests that merely
repeat implementation details. Run the relevant focused contracts while working,
then the complete platform suite before proposing a release.

Update user-facing documentation when behavior changes. Keep the README focused on
installing and running published packages; contributor workflows and implementation
details belong here or under `docs/`.

The public repository is protected by `scripts/release/source-inventory.json`.
Run this check before committing:

```powershell
python scripts/release/Assert-Source.py
```

New public source files and reviewed changes must be deliberately added to that
inventory. Never regenerate it blindly from an unreviewed working tree.

## Versions and releases

`VERSION` is the sole framework version source and accepts stable
`MAJOR.MINOR.PATCH` values. CMake, package manifests, SDK metadata and archive names
derive from it. Do not replace assets belonging to an existing release.

A push to `main` that changes `VERSION` automatically builds and tests Windows and
Linux, creates a draft, verifies every uploaded digest and publishes the complete
release. A push that does not change `VERSION` does not publish. The same workflow
can be started manually and kept as a draft.

The release workflow requires the repository secret `UPSTREAM_READ_TOKEN` with read
access to the pinned private Unreal submodule. Publication itself uses GitHub's
scoped `GITHUB_TOKEN`.

## Change checklist

Before submitting a change:

1. Keep the change within the framework repository boundary.
2. Build the affected Windows and/or Linux targets.
3. Run the relevant contracts and package checks.
4. Confirm server packages contain no client graphics components or application mods.
5. Update documentation and the reviewed source inventory when required.
6. Check `git diff --check` and review the complete diff for secrets and generated files.
