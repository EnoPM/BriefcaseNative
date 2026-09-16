"""Run install, launch and restart inside a root with no interpreter or shell.

Requires root for chroot/proc mounting; used explicitly by Linux release CI.
Python constructs the fixture outside the root, never inside the distribution.
"""
import hashlib, json, os, re, shutil, subprocess, sys, tempfile, zipfile
from pathlib import Path

assert os.geteuid()==0, 'Run this isolation contract with sudo'
build=Path(sys.argv[1]).resolve()
project=Path(__file__).resolve().parents[1]
version=(project/'VERSION').read_text().strip()
assert re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)',version)
mount,umount,chroot=(shutil.which(name) for name in ('mount','umount','chroot'))
assert mount and umount and chroot
base=project/'artifacts/native-distribution';base.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='root-',dir=base) as temporary:
    root=Path(temporary)
    def copy(source,target):
        destination=root/target.lstrip('/');destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,destination)
    binaries=['Briefcase.ServerLauncher','Briefcase.LinuxGameFixture','libBriefcase.ServerBootstrap.so','libBriefcase.LinuxHostFixture.so']
    for binary in binaries:
        output=subprocess.check_output(['ldd',str(build/binary)],text=True)
        assert 'not found' not in output,output
        for library in re.findall(r'(/[^\s()]+)',output):copy(library,library)
    copy(build/'Briefcase.ServerLauncher','/tools/Briefcase.ServerLauncher')
    folder='server/DeceiveInc/Binaries/Linux'
    copy(build/'Briefcase.LinuxGameFixture',folder+'/DeceiveIncServer-Linux-Shipping')
    data={
        'Briefcase.ServerLauncher':(build/'Briefcase.ServerLauncher').read_bytes(),
        'Briefcase/Core/libBriefcase.ServerBootstrap.so':(build/'libBriefcase.ServerBootstrap.so').read_bytes(),
        'Briefcase/Core/libBriefcase.NativeHost.so':(build/'libBriefcase.LinuxHostFixture.so').read_bytes(),
        'Briefcase/Core/Tools/Briefcase.AdminSetup':(build/'Briefcase.LinuxGameFixture').read_bytes(),
        'Briefcase/Core/Updater/build.json':json.dumps(dict(frameworkVersion=version)).encode(),
        'Briefcase/Core/Updater/updater.example.json':json.dumps(dict(schemaVersion=1,enabled=False,repository='',timeoutSeconds=1)).encode(),
    }
    digest=lambda content:hashlib.sha256(content).hexdigest()
    manifest=dict(updateSchema=1,environment='server',platform='linux-x64',frameworkVersion=version,
                  gameSha256=digest((build/'Briefcase.LinuxGameFixture').read_bytes()),
                  files=[dict(path=name,bytes=len(content),sha256=digest(content),mode=0o755 if content.startswith(b'\x7fELF') else 0o644) for name,content in data.items()])
    data['Package.json']=json.dumps(manifest).encode()
    with zipfile.ZipFile(root/'release.zip','w',zipfile.ZIP_DEFLATED) as archive:
        for name,content in data.items():archive.writestr(name,content)
    (root/'proc').mkdir()
    environment={key:value for key,value in os.environ.items() if key not in ('LD_PRELOAD','LD_LIBRARY_PATH') and not key.startswith('BC_TEST_')}
    environment['PATH']='/no-external-programs'
    subprocess.run([mount,'-t','proc','proc',str(root/'proc')],check=True)
    try:
        def run(*args,extra=None):
            result=subprocess.run([chroot,str(root),*args],env=environment|(extra or {}),capture_output=True,text=True,timeout=20)
            assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
            return result
        run('/tools/Briefcase.ServerLauncher','--server','/'+folder+'/DeceiveIncServer-Linux-Shipping','--install-archive','/release.zip')
        launcher='/'+folder+'/Briefcase.ServerLauncher'
        assert 'PASS host-before-main' in run(launcher,'--','sentinel').stdout
        run(launcher,'--','sentinel',extra={'BC_TEST_RESTART':'/restart-pids'})
        pids=(root/'restart-pids').read_text().splitlines();assert len(pids)==2 and pids[0]!=pids[1]
        assert not (root/'bin/sh').exists() and not (root/'usr/bin/python3').exists()
        print('PASS native-only distribution: install, default game discovery from another cwd, bootstrap, administration restart; no Python or shell')
    finally:subprocess.run([umount,str(root/'proc')],check=True)
