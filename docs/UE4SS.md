# Pinned UE4SS dependencies

Upstream source: https://github.com/UE4SS-RE/RE-UE4SS/releases/tag/v3.0.1

Release v3.0.1, commit `d935b5b23bac03b65c14ae38382b02007204cc2e`.
The experimental local clone is neither copied nor modified.

Exact submodules:

- `deps/first/Unreal` (UEPseudo): `d09b7218bfe7392adeffb500fdeee0b42ca1cd27`
- `deps/first/patternsleuth`: `33e731e99f2a6bb7f65a8e95e89fd1c06ce9d1d2`

CMake dependencies:

- nlohmann/json v3.11.3: `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`, MIT
- Zydis v4.1.0: `569320ad3c4856da13b9dbf1f0d9e20bda63870e`, MIT
- PolyHook2: `fd2a88f09c8ae89440858fc52573656141013c7f`, MIT
- Transitive submodule versions come from these commits.
- patternsleuth: MIT OR Apache-2.0; crates are locked by upstream `Cargo.lock`.

Build options are Release x64, C++23, dynamic CRT, `UE_GAME`,
`UE_BUILD_SHIPPING`, `PLATFORM_WINDOWS`, non-case-preserving Unreal names, static
UE4SS libraries, profiling disabled and `BRIEFCASE_UE4SS_HEADLESS=ON`. No UE4SS
application is linked. The UObject traversal cache is disabled. Production does
not retry initialization in a loop after failure.

The local fork is produced from a clean detached checkout by a reproducible patch
script, without a local commit or publication. The parent repository ignores the
checkout; only the fetch script and pins are versioned. No upstream binary from a
reference installation is reused. PDBs and source remain development artifacts.

Keep these adaptations in `scripts/build/Patch-UE4SS.ps1`:

- One scan attempt and one required-class check, with no long wait on the game thread.
- Disable console, struct-link and construction hooks unused by the framework.
- Replace the implicit `byte` alias with `uint8_t`.
- Use standard `unordered_map::find` without an explicit template argument.
- Forward-declare the upstream-missing `EAspectRatioAxisConstraint` enum.
- Adjust external CMake configuration for PolyHook include paths, library
  selection, static-link definitions and console-device exclusion.

Transitively linked submodules:

- AsmJit: `3577608cab0bc509f856ebf6e41b2f9d9f71acc4`
- AsmTK: `6e25b8983fbd8bf455c01ed7c5dd40c99b789565`
- External Zycore v1.5.0: `74620eefd233bec20daeb66e78e744ff06e273b7`

Git fetches the Zydis submodule embedded in PolyHook, but it is not linked. The
build exclusively uses the separately pinned external Zydis version above.
