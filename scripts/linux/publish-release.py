"""Attach verified Linux assets to an existing release for the same source commit."""
import argparse, json, re, subprocess, tempfile
from pathlib import Path
import updater as u

def command(*args):return subprocess.check_output(list(args),text=True).strip()
def publish(project,repository,version,commit,publish_draft=False):
    u.version(version)
    u.require(re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+",repository) and re.fullmatch(r"[a-f0-9]{40}",commit),"Invalid release identity")
    source_version=re.search(r"project\(BriefcaseNative VERSION (\d+\.\d+\.\d+)",(project/"CMakeLists.txt").read_text())[1]
    u.require(version==source_version and command("git","rev-parse","HEAD")==commit,"Release/source mismatch")
    archive=project/f"dist/Releases/BriefcaseNative-Server-linux-x64-{version}.zip";checksum=archive.with_suffix(".zip.sha256")
    u.require(checksum.read_text().strip()==u.digest_file(archive)+"  "+archive.name,"Release checksum mismatch")
    with tempfile.TemporaryDirectory() as directory:
        stage=Path(directory);u.extract(archive,stage)
        u.package_manifest(stage,version,"b0b275eac71bb8314b8afb5b36368d882faefafc993d5eac05bb5956a7334ef7")
    tag="v"+version
    api=command("gh","release","view",tag,"--repo",repository,"--json","apiUrl","--jq",".apiUrl")
    u.require(re.fullmatch("https://api.github.com/repos/"+re.escape(repository)+r"/releases/[0-9]+",api),"Invalid release URL")
    release=json.loads(command("gh","api",api))
    u.require(release["tag_name"]==tag and not release["prerelease"],"Unexpected release state")
    try:target=command("git","rev-parse","--verify",f"refs/tags/{tag}^{{commit}}")
    except subprocess.CalledProcessError:
        u.require(release["draft"],"Published release tag is missing locally")
        target=command("git","rev-parse",release["target_commitish"]+"^{commit}")
    u.require(target==commit,"Existing release targets a different commit")
    files=(archive,checksum)
    u.require(not any(item["name"] in {file.name for file in files} for item in release["assets"]),"Linux assets already exist; replacement refused")
    command("gh","release","upload",tag,*(str(file) for file in files),"--repo",repository)
    uploaded=json.loads(command("gh","api",api))
    for file in files:
        assets=[item for item in uploaded["assets"] if item["name"]==file.name]
        u.require(len(assets)==1 and assets[0]["state"]=="uploaded" and assets[0]["size"]==file.stat().st_size and assets[0]["digest"]=="sha256:"+u.digest_file(file),"GitHub asset verification failed")
    if publish_draft and uploaded["draft"]:command("gh","release","edit",tag,"--repo",repository,"--draft=false","--latest")
    print("Verified Linux release assets:",uploaded["html_url"])

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    for name in ("repository","version","commit"):parser.add_argument("--"+name,required=True)
    parser.add_argument("--publish",action="store_true");args=parser.parse_args()
    publish(Path(__file__).resolve().parents[2],args.repository,args.version,args.commit,args.publish)
