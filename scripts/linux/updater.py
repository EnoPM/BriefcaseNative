"""Linux implementation of the common GitHub release/update protocol."""
import hashlib, json, os, re, stat, tempfile, time, urllib.parse, urllib.request, uuid, zipfile
from pathlib import Path
MAX_ARCHIVE=536870912
REQUIRED={"StartBriefcaseNativeServer.sh","Briefcase/Runtime/libBriefcase.NativeHost.so",
 "Briefcase/Runtime/libBriefcase.ServerBootstrap.so","Briefcase/Tools/Briefcase.AdminSetup",
 "Briefcase/Updater/supervisor.py","Briefcase/Updater/updater.py","Briefcase/Updater/build.json"}
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
def select_asset(release,repository,current):
    require(re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+",repository),"Invalid GitHub repository")
    if release.get("draft") or release.get("prerelease"):return None
    tag=release["tag_name"];candidate=version(tag)
    if candidate<=version(current):return None
    number=".".join(map(str,candidate));name=f"BriefcaseNative-Server-linux-x64-{number}.zip"
    assets=[item for item in release["assets"] if item["name"]==name]
    require(len(assets)==1,"Release has no unique Linux server asset")
    asset=assets[0];url=f"https://github.com/{repository}/releases/download/{tag}/{name}"
    require(asset.get("browser_download_url")==url and asset.get("state")=="uploaded" and
            type(asset.get("size")) is int and 0<asset["size"]<=MAX_ARCHIVE and
            re.fullmatch(r"sha256:[a-f0-9]{64}",asset.get("digest","")),"Invalid GitHub asset metadata")
    return dict(version=number,url=url,size=asset["size"],sha256=asset["digest"][7:])
def allowed_url(url):
    parsed=urllib.parse.urlsplit(url)
    require(parsed.scheme=="https" and parsed.hostname in {
        "api.github.com","github.com","release-assets.githubusercontent.com","objects.githubusercontent.com"
    } and parsed.username is None and parsed.password is None and parsed.port in (None,443),"Untrusted download URL")
class Redirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,request,fp,code,message,headers,newurl):
        allowed_url(newurl)
        return super().redirect_request(request,fp,code,message,headers,newurl)
def download(url,path,timeout,maximum):
    allowed_url(url);deadline=time.monotonic()+timeout
    request=urllib.request.Request(url,headers={"User-Agent":"BriefcaseNative","Accept":"application/vnd.github+json"})
    with urllib.request.build_opener(Redirects()).open(request,timeout=timeout) as response:
        length=response.headers.get("Content-Length")
        if length:require(int(length)<=maximum,"Download too large")
        total=0
        with open(path,"xb") as stream:
            while block:=response.read(65536):
                total+=len(block)
                require(total<=maximum and time.monotonic()<deadline,"Download limit exceeded")
                stream.write(block)
            stream.flush();os.fsync(stream.fileno())
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
    actual={p.relative_to(stage).as_posix() for p in Path(stage).rglob("*") if p.is_file()}
    require(actual==seen|{"Package.json"} and REQUIRED<=seen,"Incomplete or unlisted package content")
    require(document(relative(stage,"Briefcase/Updater/build.json"))["frameworkVersion"]==expected_version,"Version marker mismatch")
    return manifest
def recover(root):
    journal_path=relative(root,"Briefcase/Updates/transaction.json")
    if not journal_path.exists():return
    journal=document(journal_path)
    require(journal.get("state") in ("installing","installed","rolled-back"),"Invalid recovery state")
    if journal["state"]!="installing":return
    require(re.fullmatch("[a-f0-9]{32}",journal.get("id","")),"Invalid recovery identifier")
    seen=set()
    for item in journal["files"]:
        name=item["path"]
        require(managed(name) and name not in seen,"Unmanaged recovery target");seen.add(name);relative(root,name)
        if item["existed"]:
            backup=relative(root,f"Briefcase/Updates/{journal['id']}/backup/{name}")
            require(digest_file(backup)==item["sha256"] and item["mode"] in (0o644,0o755,0o600),"Damaged recovery backup")
    for item in journal["files"]:
        target=relative(root,item["path"])
        if item["existed"]:
            backup=relative(root,f"Briefcase/Updates/{journal['id']}/backup/{item['path']}")
            atomic(target,read(backup,MAX_ARCHIVE),item["mode"])
        elif target.exists():
            read(target,MAX_ARCHIVE);target.unlink();sync_directory(target.parent)
    journal["state"]="rolled-back";write_json(journal_path,journal)
def install(root,stage,manifest,transaction_id=None,after_write=None):
    root=plain(root);stage=plain(stage);transaction_id=transaction_id or uuid.uuid4().hex
    require(re.fullmatch("[a-f0-9]{32}",transaction_id),"Invalid transaction identifier")
    names={item["path"] for item in manifest["files"]}|{"Package.json"}
    old_path=relative(root,"Package.json")
    if old_path.exists():
        for item in document(old_path)["files"]:
            require(managed(item["path"]),"Unmanaged installed manifest");names.add(item["path"])
    backups=[]
    for name in sorted(names):
        target=relative(root,name);row=dict(path=name,existed=target.exists())
        if row["existed"]:
            data=read(target,MAX_ARCHIVE)
            row.update(sha256=hashlib.sha256(data).hexdigest(),mode=stat.S_IMODE(target.stat().st_mode)&0o777)
            require(row["mode"] in (0o644,0o755,0o600),"Unsupported installed mode")
            atomic(relative(root,f"Briefcase/Updates/{transaction_id}/backup/{name}"),data,row["mode"])
        backups.append(row)
    journal=dict(state="installing",id=transaction_id,files=backups)
    journal_path=relative(root,"Briefcase/Updates/transaction.json");write_json(journal_path,journal)
    try:
        wanted={item["path"]:item for item in manifest["files"]}
        for index,name in enumerate(sorted(names-{"Package.json"})):
            target=relative(root,name)
            if name in wanted:
                item=wanted[name];atomic(target,read(relative(stage,name),MAX_ARCHIVE),item["mode"])
                require(digest_file(target)==item["sha256"],"Installed file readback mismatch")
            elif target.exists():
                read(target,MAX_ARCHIVE);target.unlink();sync_directory(target.parent)
            if after_write:after_write(index)
        atomic(old_path,read(relative(stage,"Package.json")),0o644)
        journal["state"]="installed";write_json(journal_path,journal)
    except BaseException:
        recover(root);raise
def update(root,log=print):
    # Caller holds the supervisor lock and has verified that the game has exited.
    recover(root) # Corrupt recovery is fatal even when updates are disabled.
    config_path=relative(root,"Briefcase/updater.json")
    if not config_path.exists():return "not-configured"
    try:
        config=document(config_path)
        require(config.get("schemaVersion")==1 and type(config.get("enabled")) is bool and
                type(config.get("timeoutSeconds")) is int and 1<=config["timeoutSeconds"]<=120,"Invalid updater settings")
        if not config["enabled"]:return "disabled"
        repository=config.get("repository","")
        if not repository:return "not-configured"
        require(re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+",repository),"Invalid repository")
        current=document(relative(root,"Briefcase/Updater/build.json"))["frameworkVersion"]
        directory=relative(root,"Briefcase/Updates/"+uuid.uuid4().hex);directory.mkdir(parents=True)
        download(f"https://api.github.com/repos/{repository}/releases/latest",directory/"release.json",config["timeoutSeconds"],2097152)
        asset=select_asset(document(directory/"release.json"),repository,current)
        if asset is None:return "current"
        archive=directory/"release.zip"
        download(asset["url"],archive,config["timeoutSeconds"],asset["size"])
        require(archive.stat().st_size==asset["size"] and digest_file(archive)==asset["sha256"],"Archive digest mismatch")
        stage=directory/"stage";extract(archive,stage)
        manifest=package_manifest(stage,asset["version"],digest_file(root/"DeceiveIncServer-Linux-Shipping"))
    except Exception as error:
        log("Update unavailable; installed version retained: "+type(error).__name__)
        return "failed-kept-installed"
    try:install(root,stage,manifest)
    except Exception:
        recover(root) # Failed recovery escapes and prevents launch.
        log("Update failed and was rolled back");return "failed-kept-installed"
    return "installed"
