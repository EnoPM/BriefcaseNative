"""Release publication uses local archive fixtures and a fake GitHub CLI."""
import importlib.util,json,sys,unittest
from pathlib import Path
from unittest.mock import patch
from LinuxUpdaterContracts import Contracts
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts/linux"))
import package_support as u
spec=importlib.util.spec_from_file_location("publisher",Path(__file__).resolve().parents[1]/"scripts/linux/publish-release.py")
publisher=importlib.util.module_from_spec(spec);spec.loader.exec_module(publisher)
import zipfile

class ReleaseContracts(Contracts):
    def test_publication(self):
        self.hash="b0b275eac71bb8314b8afb5b36368d882faefafc993d5eac05bb5956a7334ef7"
        stage,manifest=self.package("1.0.0")
        project=self.base/"project";(project/"dist/Releases").mkdir(parents=True)
        (project/"VERSION").write_text("1.0.0\n")
        archive=project/"dist/Releases/BriefcaseNative-Server-linux-x64-1.0.0.zip"
        with zipfile.ZipFile(archive,"w") as z:
            for file in stage.rglob("*"):
                if file.is_file():z.write(file,file.relative_to(stage).as_posix())
        checksum=archive.with_suffix(".zip.sha256");checksum.write_text(u.digest_file(archive)+"  "+archive.name+"\n")
        commit="a"*40;api="https://api.github.com/repos/Example/Framework/releases/12";calls=[]
        release=dict(tag_name="v1.0.0",draft=True,prerelease=False,assets=[],html_url="https://github.com/Example/Framework/releases/tag/v1.0.0")
        def cli(*args):
            calls.append(args)
            if args[0]=="git":return commit
            if args[:3]==("gh","release","view"):return api
            if args[:2]==("gh","api"):return json.dumps(release)
            if args[:3]==("gh","release","upload"):
                release["assets"]=[dict(name=file.name,state="uploaded",size=file.stat().st_size,digest="sha256:"+u.digest_file(file)) for file in (archive,checksum)]
                return ""
            if args[:3]==("gh","release","edit"):return ""
            raise AssertionError(args)
        with patch.object(publisher,"command",side_effect=cli):
            publisher.publish(project,"Example/Framework","1.0.0",commit,True)
            self.assertEqual(calls[-1][:3],("gh","release","edit"))
            calls.clear()
            with self.assertRaises(u.UpdateError):publisher.publish(project,"Example/Framework","1.0.0",commit)
            self.assertFalse(any(call[:3]==("gh","release","upload") for call in calls))
            calls.clear();checksum.write_text("wrong")
            with self.assertRaises(u.UpdateError):publisher.publish(project,"Example/Framework","1.0.0",commit)
            self.assertFalse(any(call[0]=="gh" for call in calls))

if __name__=="__main__":
    # Only the publication-specific test; updater contracts have their own CTest entry.
    result=unittest.TextTestRunner().run(unittest.TestSuite([ReleaseContracts("test_publication")]))
    sys.exit(not result.wasSuccessful())
