"""Headless Linux game supervisor; restart requests use an inherited private socket."""
import fcntl, os, re, select, signal, socket, stat, subprocess, sys, time
from pathlib import Path
import updater
GAME="DeceiveIncServer-Linux-Shipping"
def log(root,message):
    path=updater.relative(root,"Briefcase/Logs/launcher.log");path.parent.mkdir(parents=True,exist_ok=True)
    fd=os.open(path,os.O_WRONLY|os.O_APPEND|os.O_CREAT|os.O_NOFOLLOW|os.O_CLOEXEC,0o644)
    with os.fdopen(fd,"a") as stream:stream.write(time.strftime("%Y-%m-%d %H:%M:%S")+" "+message+"\n")
def game_running(game):
    for process in Path("/proc").iterdir():
        if not process.name.isdigit():continue
        try:
            if os.path.samefile(process/"exe",game):return True
        except (FileNotFoundError,PermissionError,ProcessLookupError):continue
    return False
def launch(game,arguments,channel):
    root=game.parent;runtime=root/"Briefcase/Runtime"
    host=updater.plain(runtime/"libBriefcase.NativeHost.so")
    bridge=updater.plain(runtime/"libBriefcase.ServerBootstrap.so")
    updater.require(host.is_file() and bridge.is_file(),"Runtime libraries missing")
    bridge_fd=os.open(bridge,os.O_RDONLY|os.O_NOFOLLOW)
    env=os.environ.copy()
    env.update(BRIEFCASE_SERVER_EXECUTABLE=str(game),BRIEFCASE_NATIVE_HOST=str(host),
               BRIEFCASE_BOOTSTRAP_FD=str(bridge_fd),BRIEFCASE_SUPERVISOR_FD=str(channel.fileno()),
               BRIEFCASE_SUPERVISOR_PID=str(os.getpid()),LD_PRELOAD="/proc/self/fd/"+str(bridge_fd))
    env["LD_LIBRARY_PATH"]=str(root.parents[2])+(":"+env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
    try:
        return subprocess.Popen([str(game),"DeceiveInc","-unattended","-NoSplash","-NOCONSOLE","-nullrhi","-nosound",*arguments],
                                cwd=root,env=env,pass_fds=(bridge_fd,channel.fileno()),start_new_session=True)
    finally:os.close(bridge_fd)
def run(game,arguments):
    game=updater.plain(game)
    updater.require(game.name==GAME and game.is_file() and os.access(game,os.X_OK),"Invalid dedicated-server executable")
    updater.require(not os.environ.get("LD_PRELOAD"),"An existing LD_PRELOAD is not supported")
    root=game.parent;lock_path=updater.relative(root,"Briefcase/Updates/launch.lock")
    lock_path.parent.mkdir(parents=True,exist_ok=True)
    lock=os.open(lock_path,os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW|os.O_CLOEXEC,0o600)
    try:
        info=os.fstat(lock)
        updater.require(stat.S_ISREG(info.st_mode) and info.st_nlink==1,"Invalid supervisor lock")
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    except BaseException:os.close(lock);raise
    stopping=False;child=None
    def stop(signum,frame):
        nonlocal stopping
        stopping=True
        if child is not None and child.poll() is None:child.send_signal(signal.SIGINT)
    previous={sig:signal.signal(sig,stop) for sig in (signal.SIGINT,signal.SIGTERM)}
    try:
        while not stopping:
            updater.require(not game_running(game),"Dedicated server already running")
            result=updater.update(root,lambda message:log(root,message))
            if stopping:break
            if result=="installed":
                target=updater.relative(root,"Briefcase/Updater/supervisor.py")
                fcntl.flock(lock,fcntl.LOCK_UN);os.close(lock);lock=-1
                os.execv(sys.executable,[sys.executable,str(target),str(game),*arguments])
            parent,channel=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET)
            with parent,channel:
                child=launch(game,arguments,channel);channel.close()
                log(root,"Server started pid="+str(child.pid))
                prepared=None;prepared_at=0;restart=False;stop_at=None;channel_open=True
                while child.poll() is None:
                    if stopping or restart:
                        if stop_at is None:
                            stop_at=time.monotonic();child.send_signal(signal.SIGINT)
                        elif time.monotonic()-stop_at>30:
                            log(root,"Graceful stop timed out; requesting termination");child.terminate()
                            try:child.wait(timeout=10)
                            except subprocess.TimeoutExpired:child.kill();child.wait()
                    ready,_,_=select.select([parent] if channel_open else [],[],[],0.1)
                    if ready:
                        message=parent.recv(256)
                        if not message:channel_open=False;continue
                        match=re.fullmatch(r"(prepare|commit) ([a-f0-9]{32})",message.decode("ascii",errors="replace"))
                        if not match:continue
                        verb,request=match.groups()
                        if verb=="prepare" and not stopping and not restart:
                            prepared=request;prepared_at=time.monotonic()
                            parent.send(("ready "+request).encode())
                        elif verb=="commit" and request==prepared and time.monotonic()-prepared_at<5 and not stopping:
                            restart=True;log(root,"Acknowledged administration restart")
                code=child.wait();log(root,"Server exited code="+str(code));child=None
                if not restart or stopping:return code if code>=0 else 128-code
        return 0
    finally:
        if child is not None and child.poll() is None:
            child.send_signal(signal.SIGINT)
            try:child.wait(timeout=30)
            except subprocess.TimeoutExpired:
                child.terminate()
                try:child.wait(timeout=10)
                except subprocess.TimeoutExpired:child.kill();child.wait()
        for sig,handler in previous.items():signal.signal(sig,handler)
        if lock>=0:os.close(lock)
if __name__=="__main__":
    if len(sys.argv)<2:raise SystemExit("Usage: supervisor.py <Shipping executable> [arguments...]")
    try:raise SystemExit(run(Path(os.path.abspath(sys.argv[1])),sys.argv[2:]))
    except Exception as error:
        print("Briefcase launcher: "+str(error),file=sys.stderr)
        raise SystemExit(78)
