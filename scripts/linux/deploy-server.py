"""Development convenience wrapper around the compiled native installer."""
import argparse, os, subprocess
from pathlib import Path
import package_support as u

def deploy(server, archive, launcher):
    server = u.plain(server)
    u.require(server.is_dir() and server not in (Path('/'), Path.home()), 'Invalid isolated server root')
    u.require(u.read(server/'.briefcase-linux-test', 1024) == b'', 'Missing isolated-installation marker')
    game = u.relative(server, 'DeceiveInc/Binaries/Linux/DeceiveIncServer-Linux-Shipping')
    launcher = u.plain(launcher)
    u.require(launcher.is_file() and os.access(launcher, os.X_OK), 'Build the native launcher first')
    subprocess.run([str(launcher), '--server', str(game), '--install-archive', str(u.plain(archive))], check=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('server', 'archive', 'launcher'): parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args(); deploy(args.server, args.archive, args.launcher)
