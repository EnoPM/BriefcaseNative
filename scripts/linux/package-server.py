"""Create a framework-only, reproducible Linux server release archive."""
import argparse, hashlib, json, os, re, shutil, stat, struct, subprocess, tempfile, zipfile
from pathlib import Path
import package_support as u
GAME_HASH="b0b275eac71bb8314b8afb5b36368d882faefafc993d5eac05bb5956a7334ef7"

def package(project,build,backend,deps,output,json_source=None):
    version=u.source_version(project)
    output=u.plain(output);output.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix="server-linux-",dir=output));modes={}
    def put(name,data,mode=0o644):
        target=u.relative(stage,name);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data);target.chmod(mode);modes[name]=mode
    def copy(name,source):put(name,source.read_bytes())
    for name,target in (("Briefcase.ServerLauncher",None),("libBriefcase.NativeHost.so","Runtime"),("libBriefcase.ServerBootstrap.so","Runtime"),("Briefcase.AdminSetup","Tools")):
        data=(build/name).read_bytes()
        u.require(data[:6]==b"\x7fELF\x02\x01" and struct.unpack_from("<H",data,18)[0]==62,"Expected Linux x64 ELF")
        destination="Briefcase/"+target+"/"+name if target else name;put(destination,data,0o755)
        subprocess.run(["strip","--strip-unneeded",str(stage/destination)],check=True)
        dependencies=subprocess.check_output(["readelf","-d",str(stage/destination)],text=True)
        u.require(not re.search(r"imgui|libX11|libGL\.|libvulkan|libSDL|d3d|dxgi",dependencies,re.I),"Graphical dependency in server")
    put("Briefcase/Updater/build.json",(json.dumps(dict(frameworkVersion=version))+"\n").encode())
    put("Briefcase/Updater/updater.example.json",(json.dumps(dict(schemaVersion=1,enabled=True,updateMods=True,repository="EnoPM/BriefcaseNative",timeoutSeconds=20),indent=2)+"\n").encode())
    copy("Briefcase/Docs/LinuxServer.md",project/"docs/LinuxServer.md")
    for source in sorted((project/"resources/Localization").glob("*.json")):copy("Briefcase/Localization/"+source.name,source)
    licenses={
        "UE4SS":backend/"LICENSE",
        "MbedTLS":project/"third_party/mbedtls-3.6.7/LICENSE",
        "Json":(json_source or build/"_deps/json-src")/"LICENSE.MIT", "PolyHook2":deps/"polyhook2-src/LICENSE",
        "Zydis":deps/"zydis-src/LICENSE", "Zycore":deps/"zydis-src/dependencies/zycore/LICENSE",
        "AsmJit":deps/"polyhook2-src/asmjit/LICENSE.md", "AsmTK":deps/"polyhook2-src/asmtk/LICENSE.md",
        "Fmt":deps/"fmt-src/LICENSE", "GCC-runtime":Path("/usr/share/doc/gcc-13-base/copyright"),
        "GPL-3":Path("/usr/share/common-licenses/GPL-3"),
        "libcurl":Path("/usr/share/doc/libcurl4t64/copyright"),
        "libarchive":Path("/usr/share/doc/libarchive13t64/copyright")}
    for name,source in licenses.items():copy("Briefcase/Licenses/"+name+".txt",source)
    metadata=json.loads(subprocess.check_output(["cargo","metadata","--locked","--format-version","1","--manifest-path",str(backend/"deps/first/patternsleuth_bind/Cargo.toml")],text=True))
    records=[]
    for item in metadata["packages"]:
        records.append({key:item.get(key) for key in ("name","version","license","source","repository")})
        for source in sorted(Path(item["manifest_path"]).parent.iterdir()):
            if source.is_file() and re.match("^(LICENSE|LICENCE|COPYING|NOTICE)",source.name):
                copy(f"Briefcase/Licenses/Rust/{item['name']}-{item['version']}/{source.name}.txt",source)
    put("Briefcase/Licenses/RustDependencies.json",(json.dumps(records,indent=2)+"\n").encode())
    put("Briefcase/Licenses/ThirdPartyNotices.txt",b"UE4SS Linux revision: 7894d53f6e13011a16445f28e6f7cd46d58c72cc (MIT).\npatternsleuth revision: 23d13d7471c854fb15b586deb2f2678a1b7bc690 (MIT OR Apache-2.0).\npatternsleuth_bind is part of UE4SS. License declarations for Rust crates are in RustDependencies.json.\nGCC runtime libraries are covered by the included GCC license notices and runtime exception.\n")
    rows=[]
    for name,mode in sorted(modes.items()):
        file=stage/name;rows.append(dict(path=name,bytes=file.stat().st_size,sha256=u.digest_file(file),mode=mode))
    put("Package.json",(json.dumps(dict(updateSchema=1,environment="server",platform="linux-x64",frameworkVersion=version,gameSha256=GAME_HASH,files=rows),indent=2)+"\n").encode())
    u.package_manifest(stage,version,GAME_HASH)
    archive=output/f"BriefcaseNative-Server-linux-x64-{version}.zip"
    with zipfile.ZipFile(archive,"w",zipfile.ZIP_DEFLATED,compresslevel=9) as zipped:
        for name,mode in sorted(modes.items()):
            info=zipfile.ZipInfo(name,(2020,1,1,0,0,0));info.create_system=3;info.external_attr=(stat.S_IFREG|mode)<<16;info.compress_type=zipfile.ZIP_DEFLATED
            zipped.writestr(info,(stage/name).read_bytes())
    return archive

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    for name in ("build","backend-source","backend-deps","output"):parser.add_argument("--"+name,type=Path,required=True)
    parser.add_argument("--json-source",type=Path)
    args=parser.parse_args();print(package(Path(__file__).resolve().parents[2],args.build,args.backend_source,args.backend_deps,args.output,args.json_source))
