"""Source publication must reject additions, alterations and application packages."""
from pathlib import Path
import importlib.util
import tempfile
import json
import hashlib
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("source_guard", root/"scripts/release/Assert-Source.py")
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)
checks = 0
with tempfile.TemporaryDirectory() as temp:
    stage = Path(temp)
    inventory = "scripts/release/source-inventory.json"
    def setup(files):
        records = []
        for name, data in files.items():
            p=stage/name; p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
            records.append({"path":name,"sha256":None if name=='VERSION' else hashlib.sha256(data.replace(b"\r\n",b"\n")).hexdigest()})
        p=stage/inventory;p.parent.mkdir(parents=True,exist_ok=True)
        p.write_text(json.dumps({"roots":["VERSION","runtime","samples","scripts"],"files":records}))
        return list(files)+[inventory]
    def reject(tracked):
        global checks
        try: guard.verify(stage,tracked)
        except (ValueError,FileNotFoundError):checks+=1;return
        raise AssertionError("Unsafe source accepted")
    tracked=setup({"runtime/example.cpp":b"// example\r\n"})
    assert guard.verify(stage,tracked)==1;checks+=1
    reject(tracked+["private/settings.json"])
    reject([inventory])
    (stage/"runtime/example.cpp").write_text("// changed")
    reject(tracked)
    tracked=setup({"samples/Extra/briefcase.mod.json":b"{}"})
    reject(tracked)
    tracked=setup({"mods/application/source.cpp":b"// application"})
    reject(tracked)
    tracked=setup({'VERSION':b'1.2.3\n','runtime/example.cpp':b'// reviewed'})
    assert guard.verify(stage,tracked)==2;checks+=1
    (stage/'VERSION').write_text('1.2.4\n')
    assert guard.verify(stage,tracked)==2;checks+=1
    for invalid in ('v1.2.3','1.2.3-beta','01.2.3','1.2.3\ninjected',''):
        (stage/'VERSION').write_text(invalid);reject(tracked)
    (stage/'VERSION').write_text('1.2.4\n')
    (stage/'runtime/example.cpp').write_text('// unreviewed')
    reject(tracked)
print(f"PASS {checks} source publication checks")
