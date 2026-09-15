# Linux dependency patches

These small changes are applied to pinned upstream sources by
scripts/linux/prepare-backend.sh. They do not contain Unreal headers or the
restricted Unreal implementation.

- ue4ss-linux-time.patch: fmt 11 chrono compatibility in the UE4SS helper library.
  Upstream: NullPrism/RE-UE4SS-Linux at
  7894d53f6e13011a16445f28e6f7cd46d58c72cc; MIT licence retained in
  UE4SS-LICENSE.txt.
- ue4ss-linux-resolvers.patch: four ELF resolver patterns examined against the
  Deceive Inc. dedicated server identified in docs/LinuxServer.md.
  Upstream: the deps/first/patternsleuth submodule at
  23d13d7471c854fb15b586deb2f2678a1b7bc690; its Cargo workspace declares
  MIT OR Apache-2.0 and author trumank.

CMake verifies both source revisions and that the patches are applied. Rebuild the
dependencies after changing them; pre-existing archives are not independently
attested by this check. These patches and development binaries are not an upstream
compatibility or redistribution guarantee.
