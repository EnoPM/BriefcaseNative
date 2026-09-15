#!/usr/bin/env bash
set -euo pipefail
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build=${BRIEFCASE_LINUX_BUILD_DIR:-"$project/build/linux-server-core"}
if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
  echo 'Linux x86_64 is required (WSL2 Ubuntu is supported).' >&2
  exit 1
fi
cmake -S "$project" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBRIEFCASE_LINUX_SERVER_CORE_ONLY=ON "$@"
cmake --build "$build" --parallel "${BRIEFCASE_BUILD_JOBS:-4}"
ctest --test-dir "$build" --output-on-failure
