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
    runtime = bin / "Briefcase/Core"
    runtime.mkdir(parents=True)
    engine = bin.parent.parent / "Saved/Config/LinuxServer/Engine.ini"
    engine.parent.mkdir(parents=True)
    engine.write_text("[ConsoleVariables]\nsb.DisableEAC=0\nFixtureSetting=7\n")
    game = bin / "DeceiveIncServer-Linux-Shipping"
    shutil.copy2(fixture, game)
    shutil.copy2(bootstrap, runtime / "libBriefcase.ServerBootstrap.so")
    shutil.copy2(host, runtime / "libBriefcase.NativeHost.so")
    env = os.environ.copy()
    for name in ("LD_PRELOAD", "BC_TEST_HOST_STARTED", "BC_TEST_REJECT"):
        env.pop(name, None)
    instance_id = "0123456789abcdef0123456789abcdef"
    def instance(name):
        root = Path(directory) / "instances" / name
        core = root / "Briefcase/Core"
        core.mkdir(parents=True)
        shutil.copy2(bootstrap, core / "libBriefcase.ServerBootstrap.so")
        shutil.copy2(host, core / "libBriefcase.NativeHost.so")
        (root / "TripwireServer.ini").write_text("[Test]\n")
        runtime_ini = root / "GameView/DeceiveInc/TripwireServer.ini"
        runtime_ini.parent.mkdir(parents=True)
        runtime_ini.write_text("[Test]\n")
        (root / "CommunityBalanceProfile.json").write_text("{}\n")
        return root
    def command(root=None):
        args = [str(launcher), "--server", str(game)]
        if root is not None:
            args += ["--instance-root", str(root), "--instance-id", instance_id]
        return args + ["--", "sentinel"]
    def launch(extra=None, root=None):
        variables = env | (extra or {})
        if root is not None:
            variables["BC_TEST_EXPECT_INSTANCE"] = str(root)
        return subprocess.run(command(root),
                              env=variables, capture_output=True, text=True, timeout=10)
    result = launch()
    assert result.returncode == 0 and "PASS host-before-main" in result.stdout, result
    engine_text = engine.read_text()
    assert engine_text.count("sb.DisableEAC=1") == 1 and "FixtureSetting=7" in engine_text, engine_text
    assert launch({"BC_TEST_REJECT": "1"}).returncode == 78
    assert launch({"LD_PRELOAD": str(bootstrap)}).returncode == 78
    first, second = instance("first"), instance("second")
    engine.write_text("[ConsoleVariables]\nsb.DisableEAC=0\nFixtureSetting=7\n")
    assert launch(root=first).returncode == 0
    assert "sb.DisableEAC=0" in engine.read_text(), "Managed launch changed the shared game configuration"
    holder = subprocess.Popen(command(first), env=env | {
        "BC_TEST_EXPECT_INSTANCE": str(first), "BC_TEST_HOLD": "1"},
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        time.sleep(.3)
        assert holder.poll() is None, "Managed instance exited before concurrency check"
        assert launch(root=first).returncode == 78, "A second launcher accepted the same instance"
        assert launch(root=second).returncode == 0, "Separate instance could not share the game executable"
    finally:
        holder.terminate()
        holder.communicate(timeout=8)
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
print("PASS Linux launcher: spaced paths, initialization order, acknowledged restart, managed instances, exclusive launch, graceful stop, preload cleanup")
