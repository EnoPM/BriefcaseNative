"""Black-box contracts for the compiled updater. Python is test tooling only."""
import hashlib, json, os, stat, subprocess, sys, tempfile, unittest, zipfile
from pathlib import Path
DRIVER = None
if len(sys.argv)>1 and not sys.argv[1].startswith('-'):
    DRIVER=str(Path(sys.argv.pop(1)).resolve())
REQUIRED={'Briefcase.ServerLauncher','Briefcase/Runtime/libBriefcase.NativeHost.so',
          'Briefcase/Runtime/libBriefcase.ServerBootstrap.so','Briefcase/Tools/Briefcase.AdminSetup','Briefcase/Updater/build.json'}
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def write_json(path,value):
    path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(value))
class Contracts(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.base=Path(self.temp.name);self.root=self.base/'game';self.root.mkdir()
        self.game=self.root/'DeceiveIncServer-Linux-Shipping';self.game.write_bytes(b'game');self.hash=digest(self.game)
    def run_native(self,*args,code=0):
        result=subprocess.run([DRIVER,*map(str,args)],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,code,result.stderr);return result.stdout.strip()
    def package(self,version,extra=None):
        stage=self.base/version;stage.mkdir()
        elf=b'\x7fELF\x02\x01'+b'\0'*12+b'\x3e\0'+version.encode()
        contents={name:elf for name in REQUIRED}
        contents['Briefcase/Updater/build.json']=json.dumps({'frameworkVersion':version}).encode();contents.update(extra or {})
        rows=[]
        for name,data in contents.items():
            path=stage/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
            rows.append(dict(path=name,bytes=len(data),sha256=digest(path),mode=0o755 if data.startswith(b'\x7fELF') else 0o644))
        manifest=dict(updateSchema=1,environment='server',platform='linux-x64',frameworkVersion=version,gameSha256=self.hash,files=rows)
        write_json(stage/'Package.json',manifest);return stage,manifest
    def installed(self,stage,manifest):
        self.run_native('manifest',stage,manifest['frameworkVersion'],self.hash);self.run_native('install',self.root,stage)
    def assert_version(self,version):
        self.assertEqual(json.loads((self.root/'Briefcase/Updater/build.json').read_text())['frameworkVersion'],version)
    def archive(self,stage):
        path=stage.with_suffix('.zip')
        with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as zipped:
            for file in stage.rglob('*'):
                if file.is_file():zipped.write(file,file.relative_to(stage).as_posix())
        return path
    def release(self,archive,version='1.1.0'):
        name=f'BriefcaseNative-Server-linux-x64-{version}.zip'
        return dict(tag_name='v'+version,draft=False,prerelease=False,assets=[dict(name=name,state='uploaded',size=archive.stat().st_size,digest='sha256:'+digest(archive),browser_download_url='https://github.com/Example/Framework/releases/download/v'+version+'/'+name)])
    def configure(self):
        write_json(self.root/'Briefcase/updater.json',dict(schemaVersion=1,enabled=True,repository='Example/Framework',timeoutSeconds=1))
    def test_fresh_install_configures_and_checks_official_release(self):
        first,m=self.package('1.0.0');self.installed(first,m);second,_=self.package('1.1.0')
        archive=self.archive(second);release=self.release(archive)
        release['assets'][0]['browser_download_url']=release['assets'][0]['browser_download_url'].replace('Example/Framework','EnoPM/BriefcaseNative')
        path=self.base/'release.json';write_json(path,release)
        self.assertEqual(self.run_native('update',self.root,path,archive,'EnoPM/BriefcaseNative','20'),'installed')
        config=self.root/'Briefcase/updater.json'
        expected=dict(schemaVersion=1,enabled=True,repository='EnoPM/BriefcaseNative',timeoutSeconds=20)
        self.assertEqual(json.loads(config.read_text()),expected);self.assert_version('1.1.0')
        template=Path(__file__).resolve().parents[1]/'scripts/update/updater.example.json'
        self.assertEqual(json.loads(template.read_text()),expected)
        original=config.read_bytes()
        self.assertEqual(self.run_native('update',self.root,path,archive,'EnoPM/BriefcaseNative','20'),'current')
        self.assertEqual(config.read_bytes(),original)
    def test_existing_disabled_or_invalid_config_is_preserved(self):
        first,m=self.package('1.0.0');self.installed(first,m);config=self.root/'Briefcase/updater.json'
        write_json(config,dict(schemaVersion=1,enabled=False,repository='Custom/Repo',timeoutSeconds=47))
        original=config.read_bytes()
        # This driver refuses any network request without a download fixture.
        self.assertEqual(self.run_native('update',self.root),'disabled');self.assertEqual(config.read_bytes(),original)
        config.write_text('{invalid user config')
        self.assertEqual(self.run_native('update',self.root),'failed-kept-installed')
        self.assertEqual(config.read_text(),'{invalid user config');self.assert_version('1.0.0')
    def test_fresh_install_offline_keeps_installed_version(self):
        first,m=self.package('1.0.0');self.installed(first,m)
        self.assertEqual(self.run_native('update',self.root),'failed-kept-installed');self.assert_version('1.0.0')
        self.assertTrue(json.loads((self.root/'Briefcase/updater.json').read_text())['enabled'])
    def test_install_obsolete_and_preserve_user_data(self):
        config=self.root/'Briefcase/Mods/Example/config.json';config.parent.mkdir(parents=True);config.write_bytes(b'private settings')
        first,m=self.package('1.0.0',{'Briefcase/Docs/old.md':b'old'});self.installed(first,m)
        second,m=self.package('1.1.0');self.installed(second,m)
        self.assertFalse((self.root/'Briefcase/Docs/old.md').exists());self.assertEqual(config.read_bytes(),b'private settings')
        self.assertEqual(stat.S_IMODE((self.root/'Briefcase.ServerLauncher').stat().st_mode),0o755);self.assert_version('1.1.0')
    def test_exception_rolls_back(self):
        first,m=self.package('1.0.0');self.installed(first,m);second,_=self.package('1.1.0')
        self.run_native('install',self.root,second,'fail',code=78)
        for row in m['files']:self.assertEqual(digest(self.root/row['path']),row['sha256'])
        self.assert_version('1.0.0')
    def test_process_death_and_corrupt_backup(self):
        first,m=self.package('1.0.0');self.installed(first,m);second,_=self.package('1.1.0')
        self.run_native('install',self.root,second,'crash',code=99)
        journal=json.loads((self.root/'Briefcase/Updates/transaction.json').read_text());row=next(row for row in journal['files'] if row['existed'])
        backup=self.root/f"Briefcase/Updates/{journal['id']}/backup"/row['path']
        original=backup.read_bytes();backup.write_bytes(b'damaged');self.run_native('update',self.root,code=78)
        backup.write_bytes(original);self.run_native('update',self.root);self.assert_version('1.0.0')
    def test_paths_links_duplicate_json(self):
        for name in ('../escape','/absolute','a//b','a/./b','C:/x','a\\b'):
            with self.subTest(name=name):self.run_native('path',self.root,name,code=78)
        config=self.base/'duplicate.json';config.write_text('{"enabled":false,"enabled":true}');self.run_native('document',config,code=78)
        (self.root/'Briefcase').symlink_to(self.base,target_is_directory=True);self.run_native('path',self.root,'Briefcase/Updater/build.json',code=78)
    def test_archives(self):
        cases=[('../escape',0),('Briefcase/Mods/Example/example.so',0),('Briefcase/Updater/updater.py',0),
               ('Briefcase/Updater/build.json',stat.S_IFLNK|0o777),('Briefcase/Updater/build.json',stat.S_IFREG|0o4755)]
        for index,(name,mode) in enumerate(cases):
            archive=self.base/f'bad{index}.zip'
            with zipfile.ZipFile(archive,'w') as zipped:
                info=zipfile.ZipInfo(name);info.external_attr=mode<<16;zipped.writestr(info,b'x')
            self.run_native('extract',archive,self.base/f'stage{index}',code=78)
        stage,m=self.package('1.0.0');(stage/'Briefcase/Updater/build.json').write_bytes(b'tampered')
        self.run_native('manifest',stage,'1.0.0',self.hash,code=78)
    def test_hardlinks_and_manifest_attacks(self):
        first,m=self.package('1.0.0');self.installed(first,m);second,new=self.package('1.1.0')
        outside=self.base/'outside';outside.write_bytes(b'do not change');target=self.root/'Briefcase/Updater/build.json';target.unlink();os.link(outside,target)
        self.run_native('install',self.root,second,code=78);self.assertEqual(outside.read_bytes(),b'do not change')
        new['files'][0]['path']='Briefcase/Mods/Example/settings.json';write_json(second/'Package.json',new)
        self.run_native('install',self.root,second,code=78)
    def test_release_urls_and_offline(self):
        stage,m=self.package('1.0.0');self.installed(stage,m);archive=self.archive(stage);release=self.release(archive)
        path=self.base/'release.json';write_json(path,release)
        self.assertEqual(json.loads(self.run_native('select',path,'Example/Framework','1.0.0'))['version'],'1.1.0')
        self.assertEqual(self.run_native('select',path,'Example/Framework','1.1.0'),'null')
        release['assets'][0]['browser_download_url']='https://evil.invalid/file';write_json(path,release);self.run_native('select',path,'Example/Framework','1.0.0',code=78)
        for url in ('http://github.com/x','https://github.com.evil.invalid/x','https://user@github.com/x','https://github.com:8443/x','file:///etc/passwd'):
            self.run_native('url',url,code=78)
        self.run_native('url','https://release-assets.githubusercontent.com/file');self.configure()
        self.assertEqual(self.run_native('update',self.root),'failed-kept-installed');self.assert_version('1.0.0')
    def test_verified_update_and_checksum_failure(self):
        first,m=self.package('1.0.0');self.installed(first,m);second,_=self.package('1.1.0');archive=self.archive(second);release=self.release(archive)
        path=self.base/'release.json';write_json(path,release);self.configure()
        wrong=dict(release);wrong['assets']=[dict(release['assets'][0],digest='sha256:'+'a'*64)];write_json(path,wrong)
        self.assertEqual(self.run_native('update',self.root,path,archive),'failed-kept-installed');self.assert_version('1.0.0')
        original=(self.root/'Briefcase/updater.json').read_bytes()
        write_json(path,release);self.assertEqual(self.run_native('update',self.root,path,archive,'Example/Framework','1'),'installed');self.assert_version('1.1.0')
        self.assertEqual((self.root/'Briefcase/updater.json').read_bytes(),original)
    def test_retired_files_removed_during_native_install(self):
        first,m=self.package('1.0.0');self.installed(first,m)
        for name in ('StartBriefcaseNativeServer.sh','Briefcase/Updater/supervisor.py','Briefcase/Updater/updater.py'):
            path=self.root/name;path.write_text('legacy');m['files'].append(dict(path=name))
        write_json(self.root/'Package.json',m);second,m=self.package('1.1.0');self.installed(second,m)
        self.assertFalse((self.root/'StartBriefcaseNativeServer.sh').exists());self.assertFalse(list((self.root/'Briefcase/Updater').glob('*.py')))
if __name__=='__main__':
    assert DRIVER,'Native updater test executable required'
    unittest.main()
