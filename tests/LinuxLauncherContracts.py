import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

launcher, bootstrap, host, fixture = map(lambda p: Path(p).resolve(), sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="briefcase launcher ") as directory:
    bin = Path(directory) / "DeceiveInc/Binaries/Linux"
    runtime = bin / "Briefcase/Runtime"
    runtime.mkdir(parents=True)
    game = bin / "DeceiveIncServer-Linux-Shipping"
    shutil.copy2(fixture, game)
    shutil.copy2(bootstrap, runtime / "libBriefcase.ServerBootstrap.so")
    shutil.copy2(host, runtime / "libBriefcase.NativeHost.so")
    env = os.environ.copy()
    for name in ("LD_PRELOAD", "BC_TEST_HOST_STARTED", "BC_TEST_REJECT"):
        env.pop(name, None)
    def launch(extra=None):
        return subprocess.run([str(launcher), "--server", str(game), "--", "sentinel"],
                              env=env | (extra or {}), capture_output=True, text=True, timeout=10)
    result = launch()
    assert result.returncode == 0 and "PASS host-before-main" in result.stdout, result
    assert launch({"BC_TEST_REJECT": "1"}).returncode == 78
    assert launch({"LD_PRELOAD": str(bootstrap)}).returncode == 78
    marker=Path(directory)/"restart-pids"
    result=launch({"BC_TEST_RESTART":str(marker)})
    assert result.returncode==0,result
    pids=marker.read_text().splitlines()
    assert len(pids)==2 and pids[0]!=pids[1],pids
    marker.unlink()
    process=subprocess.Popen([str(launcher),"--server",str(game),"--","sentinel"],
        env=env|{"BC_TEST_RESTART":str(marker),"BC_TEST_WAIT":"1"},stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    try:
        deadline=time.monotonic()+8
        while time.monotonic()<deadline:
            if marker.exists() and len(marker.read_text().splitlines())==2:break
            time.sleep(.05)
        else:raise AssertionError("Restart did not complete")
        assert launch().returncode==78,"Concurrent launcher was accepted"
        process.terminate()
        out,err=process.communicate(timeout=8)
        assert process.returncode==0,(out,err)
    finally:
        if process.poll() is None:process.kill();process.wait()
    (runtime / "libBriefcase.NativeHost.so").unlink()
    assert launch().returncode == 78
print("PASS Linux launcher: spaced paths, initialization order, acknowledged restart, duplicate request, exclusive launch, graceful stop, preload cleanup")
