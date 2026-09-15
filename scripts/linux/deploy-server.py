"""Deploy only to an explicitly marked, isolated Linux server installation."""
import argparse, fcntl, os, tempfile
from pathlib import Path
import supervisor, updater as u

def deploy(server,archive):
    server=u.plain(server)
    u.require(server.is_dir() and server!=Path("/") and server!=Path.home(),"Invalid isolated server root")
    u.require(u.read(server/".briefcase-linux-test",1024)==b"","Missing isolated-installation marker")
    root=u.relative(server,"DeceiveInc/Binaries/Linux");game=root/supervisor.GAME
    u.require(game.is_file() and os.access(game,os.X_OK),"Missing Linux dedicated server")
    lock_path=u.relative(root,"Briefcase/Updates/launch.lock");lock_path.parent.mkdir(parents=True,exist_ok=True)
    fd=os.open(lock_path,os.O_CREAT|os.O_RDWR|os.O_NOFOLLOW|os.O_CLOEXEC,0o600)
    try:
        u.require(os.fstat(fd).st_nlink==1,"Linked launch lock")
        fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        u.require(not supervisor.game_running(game),"Stop the isolated server before deployment")
        u.recover(root)
        with tempfile.TemporaryDirectory(prefix="deploy-",dir=lock_path.parent) as temporary:
            stage=Path(temporary);u.extract(archive,stage)
            version=u.document(stage/"Package.json")["frameworkVersion"];u.version(version)
            manifest=u.package_manifest(stage,version,u.digest_file(game));u.install(root,stage,manifest)
        config=root/"Briefcase/updater.json"
        if not config.exists():u.atomic(config,u.read(root/"Briefcase/Updater/updater.example.json"),0o600)
        print("Deployed framework to",root)
    finally:os.close(fd)

if __name__=="__main__":
    parser=argparse.ArgumentParser();parser.add_argument("--server",type=Path,required=True);parser.add_argument("--archive",type=Path,required=True)
    args=parser.parse_args();deploy(args.server,args.archive)
