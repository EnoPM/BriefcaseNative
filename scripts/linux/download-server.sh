#!/usr/bin/env bash
set -euo pipefail
# Explicit paths keep SteamCMD and game data outside the published source tree.
if [[ $# != 2 ]]; then
  echo 'Usage: bash download-server.sh /path/to/steamcmd.sh /path/to/isolated-server' >&2
  exit 2
fi
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || { echo 'Linux x86_64 required' >&2; exit 1; }
steamcmd=$(realpath -e -- "$1")
server=$(realpath -m -- "$2")
[[ -f "$steamcmd" && "$server" != / && "$server" != "$HOME" ]] || exit 2
# Restrict this development helper to a marked test installation or an empty directory.
mkdir -p -- "$server"
if [[ ! -f "$server/.briefcase-linux-test" ]] && [[ -n $(find "$server" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
  echo 'Destination must be empty or already marked as a Briefcase Linux test installation.' >&2
  exit 2
fi
touch "$server/.briefcase-linux-test"
log=$(mktemp)
trap 'rm -f -- "$log"' EXIT
"$steamcmd" +force_install_dir "$server" +login anonymous +app_update 5007710 validate +quit | tee "$log"
grep -Fq "Success! App '5007710' fully installed." "$log" || {
  echo 'SteamCMD did not confirm a successful installation.' >&2
  exit 1
}
# SteamCMD exit status alone does not reliably indicate installation success.
binary="$server/DeceiveInc/Binaries/Linux/DeceiveIncServer-Linux-Shipping"
[[ -f "$binary" ]] || { echo 'SteamCMD did not install the expected Linux server executable.' >&2; exit 1; }
python3 - "$binary" <<'PY'
import struct, sys
with open(sys.argv[1], 'rb') as stream:
    header = stream.read(20)
if len(header) < 20 or header[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', header, 18)[0] != 62:
    raise SystemExit('Expected a Linux x86_64 ELF executable')
print('Linux x86_64 server downloaded:', sys.argv[1])
PY
