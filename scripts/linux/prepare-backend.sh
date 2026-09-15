#!/usr/bin/env bash
set -euo pipefail
[[ $# == 2 ]] || { echo "Usage: prepare-backend.sh <source-directory> <build-directory>" >&2; exit 2; }
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
source=$(realpath -m -- "$1")
build=$(realpath -m -- "$2")
revision=7894d53f6e13011a16445f28e6f7cd46d58c72cc
if [[ ! -e "$source" ]]; then
  git clone https://github.com/NullPrism/RE-UE4SS-Linux.git "$source"
  git -C "$source" checkout --detach "$revision"
fi
[[ $(git -C "$source" rev-parse HEAD) == "$revision" ]] || { echo "Unexpected backend revision" >&2; exit 1; }
# Requires the caller's authorized access to the upstream Unreal submodule.
git -C "$source" submodule update --init --recursive
apply_patch_once() {
  local repository=$1 patch=$2
  if git -C "$repository" apply --reverse --check "$patch" 2>/dev/null; then return; fi
  git -C "$repository" apply --check "$patch"
  git -C "$repository" apply "$patch"
}
apply_patch_once "$source" "$root/cmake/patches/ue4ss-linux-time.patch"
apply_patch_once "$source/deps/first/patternsleuth" "$root/cmake/patches/ue4ss-linux-resolvers.patch"
cmake -S "$source" -B "$build" -G Ninja -DCMAKE_C_COMPILER=clang-19 \
  -DCMAKE_CXX_COMPILER=clang++-19 -DCMAKE_BUILD_TYPE=Game__Shipping__Linux \
  -DUE4SS_GUI=OFF -DUE4SS_BUILD_TESTS=ON
cmake --build "$build" --parallel "${BRIEFCASE_BUILD_JOBS:-4}" --target Unreal PalworldSignatureTests
