"""Produce the OS-neutral public SDK, including exact file hashes."""
import argparse, hashlib, json, re, shutil, tempfile, zipfile
from pathlib import Path
import package_support as u
def package(project, json_source, output):
    version=u.source_version(project)
    output.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix="sdk-",dir=output))
    for name in ("Briefcase.ModApi","Briefcase.ClientModApi","Briefcase.DeceiveInc"):
        shutil.copytree(project/"sdk"/name/"include",stage/"include",dirs_exist_ok=True)
    for name in ("Spy.cpp","SpyContracts.hpp"):
        target=stage/"src/DeceiveInc"/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(project/"sdk/Briefcase.DeceiveInc/src"/name,target)
    shutil.copytree(json_source/"include/nlohmann",stage/"third_party/include/nlohmann")
    (stage/"Licenses").mkdir()
    shutil.copyfile(json_source/"LICENSE.MIT",stage/"Licenses/nlohmann-json.txt")
    (stage/"cmake").mkdir()
    (stage/"cmake/BriefcaseNativeSDKConfig.cmake").write_text((project/"sdk/BriefcaseNativeSDKConfig.cmake.in").read_text().replace("@VERSION@",version))
    (stage/"cmake/BriefcaseNativeSDKConfigVersion.cmake").write_text(
        'set(PACKAGE_VERSION "'+version+'")\nif(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)\n set(PACKAGE_VERSION_EXACT TRUE)\n set(PACKAGE_VERSION_COMPATIBLE TRUE)\nendif()\n')
    files=[]
    for file in sorted(stage.rglob("*")):
        if not file.is_file():continue
        data=file.read_bytes().replace(b"\r\n",b"\n");file.write_bytes(data)
        files.append(dict(path=file.relative_to(stage).as_posix(),sha256=hashlib.sha256(data).hexdigest(),bytes=len(data)))
    (stage/"SDK.json").write_text(json.dumps(dict(schemaVersion=1,version=version,abiVersion=1,files=files),indent=2)+"\n")
    archive=output/("BriefcaseNative-SDK-"+version+".zip")
    with zipfile.ZipFile(archive,"w",zipfile.ZIP_DEFLATED) as zipped:
        for file in sorted(stage.rglob("*")):
            if file.is_file():
                info=zipfile.ZipInfo(file.relative_to(stage).as_posix(),(2020,1,1,0,0,0))
                zipped.writestr(info,file.read_bytes())
    return stage
if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--json-source",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    print(package(Path(__file__).resolve().parents[2],args.json_source,args.output))
