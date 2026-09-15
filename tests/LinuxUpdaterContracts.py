"""Exercise transactions and hostile packages without network or a game install."""
import hashlib, importlib.util, json, os, stat, sys, tempfile, unittest, zipfile
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts/linux"))
import updater as u

class Contracts(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.base=Path(self.temp.name);self.root=self.base/"game";self.root.mkdir()
        self.game=self.root/"DeceiveIncServer-Linux-Shipping";self.game.write_bytes(b"game")
        self.hash=u.digest_file(self.game)
    def package(self,version,extra=None):
        stage=self.base/version;stage.mkdir()
        contents={name:(name+version).encode() for name in u.REQUIRED}
        contents["Briefcase/Updater/build.json"]=json.dumps({"frameworkVersion":version}).encode()
        contents.update(extra or {})
        rows=[]
        for name,data in contents.items():
            path=u.relative(stage,name);path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
            rows.append(dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),mode=0o755 if name.endswith(".sh") else 0o644))
        (stage/"Package.json").write_text(json.dumps(dict(updateSchema=1,environment="server",platform="linux-x64",frameworkVersion=version,gameSha256=self.hash,files=rows)))
        return stage,u.package_manifest(stage,version,self.hash)
    def test_install_and_obsolete_preserve_user_data(self):
        config=self.root/"Briefcase/Mods/Example/config.json";config.parent.mkdir(parents=True);config.write_bytes(b"private settings")
        first,manifest=self.package("1.0.0",{"Briefcase/Docs/old.md":b"old"});u.install(self.root,first,manifest)
        second,manifest=self.package("1.1.0");u.install(self.root,second,manifest)
        self.assertFalse((self.root/"Briefcase/Docs/old.md").exists())
        self.assertEqual(config.read_bytes(),b"private settings")
        self.assertEqual(stat.S_IMODE((self.root/"StartBriefcaseNativeServer.sh").stat().st_mode),0o755)
        self.assertEqual(u.document(self.root/"Briefcase/Updater/build.json")["frameworkVersion"],"1.1.0")
    def test_exception_rolls_back(self):
        first,m=self.package("1.0.0");u.install(self.root,first,m)
        second,m=self.package("1.1.0")
        def fail(index):raise OSError("simulated write failure")
        with self.assertRaises(OSError):u.install(self.root,second,m,after_write=fail)
        for row in u.document(first/"Package.json")["files"]:
            self.assertEqual(u.digest_file(self.root/row["path"]),row["sha256"])
        self.assertEqual(u.document(self.root/"Briefcase/Updates/transaction.json")["state"],"rolled-back")
    def test_interrupted_transaction_recovery_and_corrupt_backup(self):
        first,m=self.package("1.0.0");u.install(self.root,first,m)
        second,m=self.package("1.1.0")
        # Suppress the in-process recovery to model power loss after a durable journal.
        def fail(index):raise OSError("power loss")
        with patch.object(u,"recover"),self.assertRaises(OSError):u.install(self.root,second,m,after_write=fail)
        journal=u.document(self.root/"Briefcase/Updates/transaction.json")
        row=next(row for row in journal["files"] if row["existed"])
        backup=self.root/f"Briefcase/Updates/{journal['id']}/backup"/row["path"]
        original=backup.read_bytes();backup.write_bytes(b"damaged")
        with self.assertRaises(u.UpdateError):u.update(self.root,lambda _:None)
        backup.write_bytes(original);u.recover(self.root)
        self.assertEqual(u.document(self.root/"Briefcase/Updater/build.json")["frameworkVersion"],"1.0.0")
    def test_paths_and_links(self):
        for name in ("../escape","/absolute","a//b","a/./b","C:/x","a\\b"):
            with self.subTest(name=name),self.assertRaises(u.UpdateError):u.relative(self.root,name)
        (self.root/"Briefcase").symlink_to(self.base,target_is_directory=True)
        with self.assertRaises(u.UpdateError):u.relative(self.root,"Briefcase/Updater/build.json")
    def test_archives(self):
        cases=[("../escape",0), ("Briefcase/Mods/Example/example.so",0),
               ("Briefcase/Updater/build.json",stat.S_IFLNK|0o777),
               ("Briefcase/Updater/build.json",stat.S_IFREG|0o4755)]
        for index,(name,mode) in enumerate(cases):
            archive=self.base/f"{index}.zip"
            with zipfile.ZipFile(archive,"w") as z:
                info=zipfile.ZipInfo(name);info.external_attr=mode<<16;z.writestr(info,b"x")
            with self.assertRaises(u.UpdateError):u.extract(archive,self.base/f"stage{index}")
        stage,m=self.package("1.0.0");(stage/"Briefcase/Updater/build.json").write_bytes(b"tampered")
        with self.assertRaises(u.UpdateError):u.package_manifest(stage,"1.0.0",self.hash)
    def test_release_and_offline(self):
        name="BriefcaseNative-Server-linux-x64-1.1.0.zip"
        release=dict(tag_name="v1.1.0",draft=False,prerelease=False,assets=[dict(name=name,state="uploaded",size=123,digest="sha256:"+"a"*64,browser_download_url="https://github.com/Example/Framework/releases/download/v1.1.0/"+name)])
        self.assertEqual(u.select_asset(release,"Example/Framework","1.0.0")["version"],"1.1.0")
        self.assertIsNone(u.select_asset(release,"Example/Framework","1.1.0"))
        release["assets"][0]["browser_download_url"]="https://evil.invalid/file"
        with self.assertRaises(u.UpdateError):u.select_asset(release,"Example/Framework","1.0.0")
        for url in ("http://github.com/x","https://github.com.evil.invalid/x","https://user@github.com/x"):
            with self.assertRaises(u.UpdateError):u.allowed_url(url)
        stage,m=self.package("1.0.0");u.install(self.root,stage,m)
        u.write_json(self.root/"Briefcase/updater.json",dict(schemaVersion=1,enabled=True,repository="Example/Framework",timeoutSeconds=1))
        with patch.object(u,"download",side_effect=OSError("offline")):
            self.assertEqual(u.update(self.root,lambda _:None),"failed-kept-installed")
    def test_verified_download_install(self):
        old,m=self.package("1.0.0");u.install(self.root,old,m)
        new,m=self.package("1.1.0")
        archive=self.base/"new.zip"
        with zipfile.ZipFile(archive,"w") as z:
            for p in new.rglob("*"):
                if p.is_file():z.write(p,p.relative_to(new).as_posix())
        name="BriefcaseNative-Server-linux-x64-1.1.0.zip"
        release=dict(tag_name="v1.1.0",assets=[dict(name=name,state="uploaded",size=archive.stat().st_size,digest="sha256:"+u.digest_file(archive),browser_download_url="https://github.com/Example/Framework/releases/download/v1.1.0/"+name)])
        u.write_json(self.root/"Briefcase/updater.json",dict(schemaVersion=1,enabled=True,repository="Example/Framework",timeoutSeconds=1))
        def download(url,path,*args):path.write_bytes(json.dumps(release).encode() if url.endswith("latest") else archive.read_bytes())
        with patch.object(u,"download",side_effect=download):self.assertEqual(u.update(self.root,lambda _:None),"installed")
        self.assertEqual(u.document(self.root/"Briefcase/Updater/build.json")["frameworkVersion"],"1.1.0")

if __name__=="__main__":unittest.main()
