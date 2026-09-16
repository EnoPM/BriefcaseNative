"""Verify the exact reviewed source inventory before building or publishing."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

def verify(root, tracked):
    inventory_path = "scripts/release/source-inventory.json"
    inventory = json.loads((root / inventory_path).read_text(encoding="utf-8"))
    expected = {row["path"]: row["sha256"] for row in inventory["files"]}
    if set(tracked) != set(expected) | {inventory_path}:
        raise ValueError("Git source tree differs from the reviewed inventory")
    sample_manifests = {
        "samples/Briefcase.NativeHello/briefcase.mod.json",
        "samples/Briefcase.NativeOverlaySample/briefcase.mod.json",
        "samples/Briefcase.RuntimeProbe/briefcase.mod.json",
    }
    for name, digest in expected.items():
        path = Path(name)
        if path.is_absolute() or ".." in path.parts or path.parts[0] not in inventory["roots"]:
            raise ValueError("Source path outside framework inventory")
        if path.name == "briefcase.mod.json" and name not in sample_manifests:
            raise ValueError("Only approved educational samples may declare mod packages")
        data = (root / name).read_bytes().replace(b"\r\n", b"\n")
        # VERSION is the one deliberately editable release input. Keep its path
        # in the reviewed inventory, but validate its grammar instead of a hash.
        if name == 'VERSION':
            if digest is not None or not re.fullmatch(rb'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)',data.strip()):
                raise ValueError('Invalid VERSION release input')
        elif hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("Source changed since review: " + name)
    return len(expected)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    root = args.root.resolve()
    tracked = subprocess.check_output(
        ["git", "-c", "safe.directory=" + root.as_posix(), "ls-files", "--cached", "--others",
         "--exclude-standard", "-z"], cwd=root
    ).decode().split("\0")
    print("PASS reviewed framework source inventory:",
          verify(root, [p for p in tracked if p and (root / p).is_file()]))
