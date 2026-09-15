#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 3 ]]; then
  echo "Usage: build-server-host.sh <ue4ss-source> <ue4ss-build> <ue4ss-deps> [cmake-options...]" >&2
  exit 2
fi
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
source=$(realpath -e -- "$1")
backend=$(realpath -e -- "$2")
deps=$(realpath -e -- "$3")
shift 3
build=${BRIEFCASE_LINUX_HOST_BUILD_DIR:-"$root/build/linux-server-host"}
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 \
  -DBRIEFCASE_LINUX_SERVER_HOST=ON \
  -DBRIEFCASE_UE4SS_LINUX_SOURCE="$source" \
  -DBRIEFCASE_UE4SS_LINUX_BUILD="$backend" \
  -DBRIEFCASE_UE4SS_LINUX_DEPS="$deps" "$@"
cmake --build "$build" --parallel "${BRIEFCASE_BUILD_JOBS:-4}"
ctest --test-dir "$build" --output-on-failure
