"""Black-box contracts for the native Windows updater. Python is test tooling only."""
import hashlib, json, subprocess, sys, tempfile, unittest, zipfile
from pathlib import Path

DRIVER=str(Path(sys.argv.pop(1)).resolve())
REQUIRED={'Briefcase.ServerLauncher.exe','Briefcase/Core/Briefcase.NativeHost.dll',
 'Briefcase/Core/Briefcase.ServerBootstrap.dll','Briefcase/Core/Tools/Briefcase.AdminSetup.exe',
 'Briefcase/Core/Tools/Briefcase.ServerRestart.exe','Briefcase/Core/Tools/Briefcase.ServerUpdater.exe',
 'Briefcase/Core/Updater/build.json'}
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def write_json(path,value):
 path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(value),encoding='utf-8')
def pe(suffix=b''):
 data=bytearray(0x88+len(suffix));data[0:2]=b'MZ';data[0x3c]=0x80;data[0x80:0x86]=b'PE\0\0\x64\x86';data[0x88:]=suffix;return bytes(data)

class Contracts(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
  self.base=Path(self.temp.name);self.root=self.base/'game';self.root.mkdir()
  self.game=self.root/'DeceiveIncServer-Win64-Shipping.exe';self.game.write_bytes(b'game');self.hash=digest(self.game)
 def run_native(self,*args,code=0):
  result=subprocess.run([DRIVER,*map(str,args)],capture_output=True,text=True,timeout=20)
  self.assertEqual(result.returncode,code,result.stderr);return result.stdout.strip()
 def package(self,version,extra=None):
  stage=self.base/('package-'+version.replace('.','-'));stage.mkdir()
  contents={name:pe(version.encode()) for name in REQUIRED}
  contents['Briefcase/Core/Updater/build.json']=json.dumps({'frameworkVersion':version}).encode()
  contents['Briefcase/Core/Updater/updater.example.json']=b'{"schemaVersion":1}'
  contents.update(extra or {})
  rows=[]
  for name,data in contents.items():
   path=stage/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
   rows.append(dict(path=name,bytes=len(data),sha256=digest(path),mode=493 if name.endswith(('.exe','.dll')) else 420))
  manifest=dict(updateSchema=1,environment='server',platform='windows-x64',frameworkVersion=version,gameSha256=self.hash,files=rows)
  write_json(stage/'Package.json',manifest);return stage,manifest
 def install(self,stage,manifest): self.run_native('manifest',stage,manifest['frameworkVersion'],self.hash);self.run_native('install',self.root,stage)
 def archive(self,stage):
  target=stage.with_suffix('.zip')
  with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED) as archive:
   for file in stage.rglob('*'):
    if file.is_file(): archive.write(file,file.relative_to(stage).as_posix())
  return target
 def release(self,archive,version='1.1.0'):
  name=f'BriefcaseNative-Server-windows-x64-{version}.zip';repository='Example/Framework'
  return dict(tag_name='v'+version,draft=False,prerelease=False,assets=[dict(name=name,state='uploaded',size=archive.stat().st_size,
   digest='sha256:'+digest(archive),browser_download_url=f'https://github.com/{repository}/releases/download/v{version}/{name}')])
 def test_install_update_and_rollback(self):
  first,manifest=self.package('1.0.0');self.install(first,manifest)
  second,_=self.package('1.1.0')
  self.run_native('install',self.root,second,'fail',code=78)
  self.assertEqual(json.loads((self.root/'Briefcase/Core/Updater/build.json').read_text())['frameworkVersion'],'1.0.0')
  archive=self.archive(second);release=self.base/'release.json';write_json(release,self.release(archive))
  write_json(self.root/'Briefcase/updater.json',dict(schemaVersion=1,enabled=True,updateMods=True,repository='Example/Framework',timeoutSeconds=7))
  self.assertEqual(self.run_native('update',self.root,release,archive,'Example/Framework','7'),'installed')
  self.assertEqual(json.loads((self.root/'Briefcase/Core/Updater/build.json').read_text())['frameworkVersion'],'1.1.0')
 def test_paths_json_archive_and_release_identity(self):
  for name in ('../escape','/absolute','a//b','a/./b','C:/x','a\\b','Briefcase/CON/file'):
   with self.subTest(name=name): self.run_native('path',self.root,name,code=78)
  duplicate=self.base/'duplicate.json';duplicate.write_text('{"enabled":false,"enabled":true}')
  self.run_native('document',duplicate,code=78)
  bad=self.base/'bad.zip'
  with zipfile.ZipFile(bad,'w') as archive: archive.writestr('../escape',b'x')
  self.run_native('extract',bad,self.base/'stage',code=78)
  stage,manifest=self.package('1.0.0');archive=self.archive(stage);release=self.base/'release.json';write_json(release,self.release(archive))
  selected=json.loads(self.run_native('select',release,'Example/Framework','1.0.0'));self.assertEqual(selected['version'],'1.1.0')
  content=json.loads(release.read_text());content['assets'][0]['browser_download_url']='https://evil.invalid/file';write_json(release,content)
  self.run_native('select',release,'Example/Framework','1.0.0',code=78)
  for url in ('http://github.com/x','https://github.com.evil.invalid/x','https://user@github.com/x','https://github.com:8443/x'):
   self.run_native('url',url,code=78)
 def test_legacy_scripts_are_retired(self):
  first,manifest=self.package('1.0.0');self.install(first,manifest)
  retired=('StartBriefcaseNativeServer.ps1','Briefcase/Updater/Updater.ps1','Briefcase/Updater/Restart-Server.ps1','Briefcase/Updater/Launch-Server.ps1')
  installed=json.loads((self.root/'Package.json').read_text())
  for name in retired:
   path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('legacy')
   installed['files'].append(dict(path=name.replace('/','\\')))
  write_json(self.root/'Package.json',installed)
  second,new=self.package('1.1.0');self.install(second,new)
  for name in retired:self.assertFalse((self.root/name).exists())
 def test_completed_update_work_is_cleaned_without_touching_active_or_unknown_data(self):
  updates=self.root/'Briefcase/Updates';completed='1'*32;active='2'*32
  (updates/completed/'stage').mkdir(parents=True);(updates/completed/'stage/file').write_text('temporary')
  (updates/('install-'+'3'*32)/'stage').mkdir(parents=True)
  (updates/active/'backup').mkdir(parents=True);(updates/active/'backup/file').write_text('required')
  (updates/'keep-me').mkdir(parents=True);(updates/'keep-me/user.txt').write_text('keep')
  write_json(updates/'transaction.json',dict(state='installing',id=active,files=[]))
  write_json(updates/'mod-transaction.json',dict(state='installed',id='4'*32,files=[]))
  self.run_native('cleanup',self.root)
  self.assertFalse((updates/completed).exists());self.assertFalse((updates/('install-'+'3'*32)).exists())
  self.assertTrue((updates/active/'backup/file').exists());self.assertTrue((updates/'keep-me/user.txt').exists())
  self.assertTrue((updates/'transaction.json').exists());self.assertFalse((updates/'mod-transaction.json').exists())
 def test_mod_update_preserves_configuration(self):
  mod='test.early';repository='Example/Test.Early';prefix=Path('Briefcase/Mods')/mod
  installed=dict(schemaVersion=1,id=mod,name='Early',author='Test',version='1.0.0',entry='Test.Early.dll',minimumApi=1,
   loadPhase='startup',environment='server',capabilities=['log'],dependencies=[],update=dict(provider='github-releases',repository=repository))
  write_json(self.root/prefix/'briefcase.mod.json',installed);(self.root/prefix/'Test.Early.dll').write_bytes(pe(b'old'))
  write_json(self.root/prefix/'Data/config.json',dict(local=True))
  stage=self.base/'mod-stage';packaged=dict(installed);packaged['version']='1.1.0';write_json(stage/prefix/'briefcase.mod.json',packaged)
  (stage/prefix/'Test.Early.dll').write_bytes(pe(b'new'));write_json(stage/prefix/'Data/config.json',dict(local=False))
  rows=[]
  for file in sorted((stage/'Briefcase').rglob('*')):
   if file.is_file():
    name=file.relative_to(stage).as_posix();row=dict(path=name,bytes=file.stat().st_size,sha256=digest(file),mode=493 if file.suffix=='.dll' else 420)
    if name==f'{prefix.as_posix()}/Data/config.json':row['preserve']=True
    rows.append(row)
  package=dict(updateSchema=1,kind='briefcase-mod',platform='windows-x64',modId=mod,version='1.1.0',repository=repository,files=rows)
  write_json(stage/'ModPackage.json',package);self.run_native('modmanifest',stage,mod,'1.1.0',repository)
  archive=self.archive(stage);name='Test.Early-windows-x64-1.1.0.zip'
  release=dict(tag_name='v1.1.0',draft=False,prerelease=False,assets=[dict(name=name,state='uploaded',size=archive.stat().st_size,digest='sha256:'+digest(archive),
   browser_download_url='https://github.com/Example/Test.Early/releases/download/v1.1.0/'+name)])
  release_path=self.base/'mod-release.json';write_json(release_path,release)
  write_json(self.root/'Briefcase/updater.json',dict(schemaVersion=1,enabled=True,updateMods=True,repository='',timeoutSeconds=7))
  result=json.loads(self.run_native('mods',self.root,release_path,archive,repository,'7'));self.assertEqual(result['updated'],1)
  self.assertEqual(json.loads((self.root/prefix/'Data/config.json').read_text()),dict(local=True))
  self.assertEqual(json.loads((self.root/prefix/'briefcase.mod.json').read_text())['version'],'1.1.0')

if __name__=='__main__': unittest.main()
