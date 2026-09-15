"""Exercise the download helper with a mock SteamCMD; never contact Steam."""
from pathlib import Path
import os
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
helper = root / "scripts/linux/download-server.sh"
with tempfile.TemporaryDirectory(prefix="briefcase-linux-download-") as temporary:
    directory = Path(temporary)
    steamcmd = directory / "mock steamcmd"
    steamcmd.write_text("""#!/usr/bin/env python3
import os, pathlib, struct, sys
assert sys.argv[1:] == ['+force_install_dir', sys.argv[2], '+login', 'anonymous', '+app_update', '5007710', 'validate', '+quit']
if os.environ.get('MOCK_FAIL'):
    print('ERROR! Failed to install app (Missing configuration)')
    sys.exit(0)
p = pathlib.Path(sys.argv[2]) / 'DeceiveInc/Binaries/Linux/DeceiveIncServer-Linux-Shipping'
p.parent.mkdir(parents=True, exist_ok=True)
header = bytearray(20)
header[:6] = b'\\x7fELF\\x02\\x01'
struct.pack_into('<H', header, 18, 62)
p.write_bytes(header)
print("Success! App '5007710' fully installed.")
""")
    steamcmd.chmod(0o755)
    server = directory / "isolated server"

    def run(target, fail=False):
        env = os.environ.copy()
        env.pop("MOCK_FAIL", None)
        if fail:
            env["MOCK_FAIL"] = "1"
        return subprocess.run(["bash", str(helper), str(steamcmd), str(target)],
                              env=env, capture_output=True, text=True).returncode

    assert run(server) == 0
    assert (server / ".briefcase-linux-test").exists()
    assert run(server) == 0  # Update a marked test copy.
    assert run(server, fail=True) != 0  # Stale executable must not mask a failed update.
    unowned = directory / "existing installation"
    unowned.mkdir()
    (unowned / "keep.txt").write_text("keep")
    assert run(unowned) != 0
    assert sorted(p.name for p in unowned.iterdir()) == ["keep.txt"]
    assert run(Path("/")) != 0
    binary = server / "DeceiveInc/Binaries/Linux/DeceiveIncServer-Linux-Shipping"
    assert struct.unpack_from("<H", binary.read_bytes(), 18)[0] == 62
print("PASS 8 Linux download helper checks")
