"""Rewrite the publication inventory after a human review of the complete diff."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

INVENTORY = "scripts/release/source-inventory.json"

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--reviewed", action="store_true",
                        help="confirm that the complete source diff has been reviewed")
    args = parser.parse_args()
    if not args.reviewed:
        raise SystemExit("Refusing to rewrite a security inventory without --reviewed")
    root = args.root.resolve()
    previous = json.loads((root / INVENTORY).read_text(encoding="utf-8"))
    roots = set(previous["roots"])
    output = subprocess.check_output(
        ["git", "-c", "safe.directory=" + root.as_posix(), "ls-files", "--cached", "--others",
         "--exclude-standard", "-z"], cwd=root
    ).decode().split("\0")
    paths = sorted(path for path in output if path and path != INVENTORY and (root / path).is_file())
    records = []
    for name in paths:
        path = Path(name)
        if path.is_absolute() or ".." in path.parts or path.parts[0] not in roots:
            raise SystemExit("Source path outside approved roots: " + name)
        data = (root / path).read_bytes().replace(b"\r\n", b"\n")
        records.append({"path": name, "sha256": None if name == "VERSION" else hashlib.sha256(data).hexdigest()})
    document = {"schemaVersion": 1, "roots": previous["roots"], "files": records}
    (root / INVENTORY).write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(f"Recorded {len(records)} reviewed source files")

if __name__ == "__main__":
    main()
