"""Build-time package validation only. Never distributed to a game server."""
import hashlib, json, os, re, stat, tempfile, zipfile
from pathlib import Path
MAX_ARCHIVE=536870912
REQUIRED={"Briefcase.ServerLauncher","Briefcase/Runtime/libBriefcase.NativeHost.so",
 "Briefcase/Runtime/libBriefcase.ServerBootstrap.so","Briefcase/Tools/Briefcase.AdminSetup",
 "Briefcase/Updater/build.json"}
class UpdateError(RuntimeError):pass
def require(value,message):
    if not value:raise UpdateError(message)
def plain(path):
    path=Path(os.path.abspath(path))
    for item in (path,*path.parents):
        try:info=item.lstat()
        except FileNotFoundError:continue
        require(not stat.S_ISLNK(info.st_mode),"Symbolic link refused")
    return path
def relative(root,name):
    require(isinstance(name,str) and re.fullmatch(r"[A-Za-z0-9_.\-/]+",name),"Invalid package path")
    require(all(part not in ("",".","..") for part in name.split("/")),"Unsafe package path")
    return plain(Path(root)/name)
def managed(name):
    return name=="Package.json" or name in REQUIRED or name=="Briefcase/Updater/updater.example.json" or bool(
        re.fullmatch(r"Briefcase/(Docs|Licenses|Localization)/[A-Za-z0-9_./-]+\.(json|txt|md)",name))
def read(path,limit=2097152):
    fd=os.open(plain(path),os.O_RDONLY|os.O_NOFOLLOW|os.O_CLOEXEC|os.O_NONBLOCK)
    with os.fdopen(fd,"rb") as stream:
        info=os.fstat(stream.fileno())
        require(stat.S_ISREG(info.st_mode) and info.st_nlink==1,"Non-regular or linked file refused")
        require(info.st_size<=limit,"File exceeds limit")
        data=stream.read(limit+1);require(len(data)<=limit,"File exceeds limit")
        return data
def pairs(items):
    out={}
    for key,value in items:
        require(key not in out,"Duplicate JSON key");out[key]=value
    return out
def document(path):return json.loads(read(path),object_pairs_hook=pairs)
def digest_file(path):
    fd=os.open(plain(path),os.O_RDONLY|os.O_NOFOLLOW|os.O_CLOEXEC|os.O_NONBLOCK)
    digest=hashlib.sha256()
    with os.fdopen(fd,"rb") as stream:
        info=os.fstat(stream.fileno())
        require(stat.S_ISREG(info.st_mode) and info.st_nlink==1,"Invalid file for hashing")
        while data:=stream.read(1048576):digest.update(data)
    return digest.hexdigest()
def sync_directory(path):
    fd=os.open(path,os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC)
    try:os.fsync(fd)
    finally:os.close(fd)
def atomic(path,data,mode=0o644):
    path=plain(path);path.parent.mkdir(parents=True,exist_ok=True);plain(path.parent)
    if path.exists():
        info=path.stat()
        require(stat.S_ISREG(info.st_mode) and info.st_nlink==1,"Unsafe replacement target")
    fd,temporary=tempfile.mkstemp(prefix="."+path.name+".",dir=path.parent)
    try:
        with os.fdopen(fd,"wb") as stream:
            stream.write(data);stream.flush();os.fchmod(stream.fileno(),mode);os.fsync(stream.fileno())
        plain(path);os.replace(temporary,path);sync_directory(path.parent)
    finally:
        if os.path.exists(temporary):os.unlink(temporary)
def write_json(path,value):atomic(path,(json.dumps(value,indent=2)+"\n").encode(),0o600)
def version(text):
    require(isinstance(text,str) and re.fullmatch(r"v?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)",text),"Invalid stable version")
    return tuple(map(int,text.removeprefix("v").split(".")))
def extract(archive,stage):
    with zipfile.ZipFile(archive) as zipped:
        entries=zipped.infolist();require(len(entries)<=4096,"Too many ZIP entries")
        seen=set();total=0
        for item in entries:
            name=item.filename;relative(stage,name)
            require(name not in seen and not item.is_dir(),"Duplicate/directory ZIP entry")
            require(managed(name),"Unmanaged archive file");seen.add(name)
            mode=(item.external_attr>>16)&0xffff
            require(stat.S_IFMT(mode) in (0,stat.S_IFREG),"ZIP links/special files refused")
            require(not(mode&0o7000),"Privileged file mode refused")
            require(item.compress_type in (zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED),"Unsupported ZIP compression")
            total+=item.file_size
            require(total<=1073741824 and item.file_size<=MAX_ARCHIVE,"Expanded ZIP too large")
        for item in entries:
            path=relative(stage,item.filename);path.parent.mkdir(parents=True,exist_ok=True)
            with zipped.open(item) as source,open(path,"xb") as output:
                count=0
                while block:=source.read(65536):
                    count+=len(block);require(count<=item.file_size,"Invalid ZIP size");output.write(block)
                require(count==item.file_size,"Truncated ZIP entry")
def package_manifest(stage,expected_version,game_hash):
    manifest=document(relative(stage,"Package.json"))
    require(manifest.get("updateSchema")==1 and manifest.get("environment")=="server" and
            manifest.get("platform")=="linux-x64" and manifest.get("frameworkVersion")==expected_version and
            manifest.get("gameSha256")==game_hash,"Incompatible package identity")
    seen=set()
    for item in manifest["files"]:
        name=item["path"];path=relative(stage,name)
        require(name!="Package.json" and managed(name) and name not in seen,"Invalid manifest file");seen.add(name)
        require(type(item.get("bytes")) is int and 0<=item["bytes"]<=MAX_ARCHIVE and
                item.get("mode") in (0o644,0o755) and re.fullmatch("[a-f0-9]{64}",item.get("sha256","")) and
                path.stat().st_size==item["bytes"] and digest_file(path)==item["sha256"],"Package file mismatch")
        if name in {"Briefcase.ServerLauncher","Briefcase/Tools/Briefcase.AdminSetup"} or name.endswith(".so"):
            with path.open('rb') as stream:header=stream.read(20)
            require(item["mode"]==0o755 and len(header)==20 and header[:6]==b'\x7fELF\x02\x01' and header[18:20]==b'\x3e\0',"Expected executable Linux x64 ELF")
    actual={p.relative_to(stage).as_posix() for p in Path(stage).rglob("*") if p.is_file()}
    require(actual==seen|{"Package.json"} and REQUIRED<=seen,"Incomplete or unlisted package content")
    require(document(relative(stage,"Briefcase/Updater/build.json"))["frameworkVersion"]==expected_version,"Version marker mismatch")
    return manifest
