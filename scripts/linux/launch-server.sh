#!/usr/bin/env bash
set -euo pipefail
[[ $# -ge 1 ]] || { echo "Usage: launch-server.sh <Shipping executable> [arguments...]" >&2; exit 2; }
game=$(realpath -e -- "$1")
shift
exec "$(dirname -- "$game")/Briefcase.ServerLauncher" --server "$game" -- "$@"
