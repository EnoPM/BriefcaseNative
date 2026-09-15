#!/usr/bin/env bash
set -euo pipefail
[[ $# -ge 1 ]] || { echo "Usage: launch-server.sh <Shipping executable> [arguments...]" >&2; exit 2; }
script=$(cd -- "$(dirname -- "$0")" && pwd -P)
exec python3 "$script/supervisor.py" "$@"
