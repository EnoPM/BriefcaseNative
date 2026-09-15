"""The optional offline scanner must never certify an unrelated or invalid image."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

scanner = Path(sys.argv[1]).resolve()


def run(*args):
    return subprocess.run([str(scanner), *map(str, args)], capture_output=True,
                          text=True, timeout=20)


assert run().returncode == 2
with tempfile.TemporaryDirectory() as directory:
    image = Path(directory) / "image"
    assert run(image).returncode == 1
    for content in (b"", b"MZ" + bytes(128), b"\x7fELF\x02\x01\x01" + bytes(13)):
        image.write_bytes(content)
        result = run(image)
        assert result.returncode == 1, result
        assert "Expected an ELF64" in result.stderr

    # A complete ELF header for another architecture is rejected before scanning.
    content = bytearray(64)
    content[:7] = b"\x7fELF\x02\x01\x01"
    content[16] = 2
    content[18] = 183  # AArch64, not x86_64.
    content[20] = 1
    content[52] = 64
    image.write_bytes(content)
    assert run(image).returncode == 1

# A real executable with no Unreal runtime cannot become a valid game profile.
result = run("/bin/true")
assert result.returncode in (1, 3), result
if result.returncode == 3:
    report = json.loads(result.stdout)
    assert report["runtimeValidated"] is False
    assert report["readyForRuntimeValidation"] is False
    assert report["missing"]
print("PASS Linux offline scanner rejects invalid and unrelated executables")
