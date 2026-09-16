Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot
. (Join-Path $project 'scripts/update/Updater.ps1')
$fixture = Join-Path $project ('artifacts/updater-tests-' + [guid]::NewGuid().ToString('N'))
$win64 = Join-Path $fixture 'DeceiveInc/Binaries/Win64'
$stage = Join-Path $fixture 'stage'
$checks = 0
function Check([bool]$Value, [string]$Message) { if (-not $Value) { throw $Message }; $script:checks++ }
function Reject([scriptblock]$Action, [string]$Message) {
    $failed=$false; try { & $Action | Out-Null } catch { $failed=$true }
    Check $failed $Message
}
function Put([string]$Path,[string]$Value) {
    New-Item -ItemType Directory -Path (Split-Path $Path) -Force | Out-Null
    [IO.File]::WriteAllText($Path,$Value)
}
function Read([string]$Relative) { Get-Content -LiteralPath (Join-Path $win64 $Relative) -Raw }
Put (Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe') 'never executed game fixture'
$gameHash = (Get-FileHash -LiteralPath (Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe')).Hash.ToLowerInvariant()
foreach ($name in @('Briefcase.ServerLauncher.exe','Briefcase/Runtime/Briefcase.ServerBootstrap.dll','StartBriefcaseNativeServer.ps1','Briefcase/Runtime/Briefcase.NativeHost.dll',
    'Briefcase/Updater/Updater.ps1','Briefcase/Updater/Restart-Server.ps1','Briefcase/Updater/Launch-Server.ps1','Briefcase/Tools/Briefcase.ServerRestart.exe',
    'Briefcase/Docs/Fixture.md')) { Put (Join-Path $stage $name) 'new' }
Put (Join-Path $stage 'Briefcase/Updater/build.json') '{"frameworkVersion":"0.4.0"}'

$records = @(Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
    @{path=$_.FullName.Substring($stage.Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
})
$manifest = @{updateSchema=1;environment='server';platform='windows-x64';frameworkVersion='0.4.0';gameSha256=$gameHash;files=$records}
Write-UpdateJson (Join-Path $stage 'Package.json') $manifest
$validated = Read-UpdatePackage $stage '0.4.0' $gameHash
Check ($validated.files.Count -eq $records.Count) 'Valid release rejected.'
# A framework release must never claim files owned by an independent mod.
$intruder='Briefcase/Mods/test.server/Server.dll'
Put (Join-Path $stage $intruder) 'unwanted bundled mod'
$invalid=Get-Content -LiteralPath (Join-Path $stage 'Package.json') -Raw|ConvertFrom-Json
$invalid.files+=@{path=$intruder;bytes=(Get-Item -LiteralPath (Join-Path $stage $intruder)).Length;sha256=(Get-FileHash -LiteralPath (Join-Path $stage $intruder)).Hash.ToLowerInvariant()}
Write-UpdateJson (Join-Path $stage 'Package.json') $invalid
Reject {Read-UpdatePackage $stage '0.4.0' $gameHash} 'Framework package containing a mod accepted.'
Remove-Item -LiteralPath (Join-Path $stage $intruder)
Write-UpdateJson (Join-Path $stage 'Package.json') $manifest

Reject { Read-UpdatePackage $stage '0.5.0' $gameHash } 'Wrong version accepted.'
Reject { Read-UpdatePackage $stage '0.4.0' ('0'*64) } 'Wrong game accepted.'
Put (Join-Path $stage 'extra.dll') 'unlisted'
Reject { Read-UpdatePackage $stage '0.4.0' $gameHash } 'Unlisted file accepted.'
Remove-Item -LiteralPath (Join-Path $stage 'extra.dll')
Put (Join-Path $stage 'Briefcase.ServerLauncher.exe') 'tampered'
Reject { Read-UpdatePackage $stage '0.4.0' $gameHash } 'Tampering accepted.'
Put (Join-Path $stage 'Briefcase.ServerLauncher.exe') 'new'
foreach ($path in @('../escape','/rooted','C:/rooted','Briefcase/x:stream','Briefcase/../bad','Briefcase//bad','Briefcase/CON.txt','Briefcase/a.')) {
    Reject { Resolve-UpdatePath $stage $path } "Unsafe path accepted: $path"
}
foreach ($path in @('DeceiveIncServer-Win64-Shipping.exe','Briefcase/Admin/server.json','Briefcase/settings.json',
    'Briefcase/updater.json','Briefcase/Mods/test.server/Data/config.json','Briefcase/Updates/transaction.json','Briefcase/Core/Client.dll')) {
    Check (-not (Test-UpdateManagedPath $path)) "Local data considered managed: $path"
}
Check (-not (Test-UpdateDefaultPath 'Briefcase/Mods/test.server/Data/config.json')) 'Framework must not install mod defaults.'
foreach ($version in @('1.2.3-beta','1.2','01.2.3','v1.2.3.4')) { Reject { ConvertTo-ReleaseVersion $version } 'Invalid version accepted.' }
Check ((ConvertTo-ReleaseVersion 'v1.10.0') -gt (ConvertTo-ReleaseVersion 'v1.9.0')) 'Lexical version comparison.'
$release = [pscustomobject]@{tag_name='v0.4.0';draft=$false;prerelease=$false;assets=@(
    [pscustomobject]@{name='BriefcaseNative-Server-windows-x64-0.4.0.zip';state='uploaded';size=1;digest=('sha256:'+('a'*64));
        browser_download_url='https://github.com/example/repo/releases/download/v0.4.0/BriefcaseNative-Server-windows-x64-0.4.0.zip'})}
Check ((Select-UpdateAsset $release 'example/repo' '0.3.0').version -eq '0.4.0') 'Stable asset rejected.'
Check ($null -eq (Select-UpdateAsset $release 'example/repo' '0.4.0')) 'Same version selected.'
Check ($null -eq (Select-UpdateAsset $release 'example/repo' '0.5.0')) 'Downgrade selected.'
$release.prerelease=$true
Check ($null -eq (Select-UpdateAsset $release 'example/repo' '0.3.0')) 'Prerelease selected.'
$release.prerelease=$false
$url=$release.assets[0].browser_download_url
$release.assets[0].browser_download_url='https://evil.invalid/payload.zip'
Reject { Select-UpdateAsset $release 'example/repo' '0.3.0' } 'External asset accepted.'
$release.assets[0].browser_download_url=$url
$release.assets[0].digest=''
Reject { Select-UpdateAsset $release 'example/repo' '0.3.0' } 'Missing GitHub digest accepted.'
Reject { Get-UpdateDownload 'http://github.com/release' (Join-Path $fixture 'no-download') 1 1 } 'Non-TLS accepted.'

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=Join-Path $fixture 'release.zip'
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$archive)
$expanded=Join-Path $fixture 'expanded'
Expand-UpdateArchive $archive $expanded
$null=Read-UpdatePackage $expanded '0.4.0' $gameHash
Check (Test-Path -LiteralPath (Join-Path $expanded 'Briefcase.ServerLauncher.exe')) 'ZIP failed to round-trip.'
foreach ($names in @(@('../outside'), @('same','SAME'), @('dir/../../outside'))) {
    $bad=Join-Path $fixture ([guid]::NewGuid().ToString('N')+'.zip')
    $zip=[IO.Compression.ZipFile]::Open($bad,[IO.Compression.ZipArchiveMode]::Create)
    foreach ($name in $names) { $null=$zip.CreateEntry($name) }
    $zip.Dispose()
    Reject { Expand-UpdateArchive $bad (Join-Path $fixture ([guid]::NewGuid().ToString('N'))) } 'Unsafe ZIP accepted.'
}
Put (Join-Path $win64 'Briefcase.ServerLauncher.exe') 'old'
Put (Join-Path $win64 'Briefcase/Mods/test.server/Data/config.json') 'local'
Put (Join-Path $win64 'Briefcase/Mods/private.mod/Private.dll') 'private'
Put (Join-Path $win64 'Briefcase/Admin/server.json') 'local secret fixture'
Put (Join-Path $win64 'Briefcase/Runtime/Obsolete.dll') 'obsolete'
Write-UpdateJson (Join-Path $win64 'Package.json') @{files=@(@{path='Briefcase/Runtime/Obsolete.dll'},@{path='Briefcase/Mods/private.mod/Private.dll'})}
Install-UpdatePackage $win64 $stage $validated ([guid]::NewGuid().ToString('N'))
Check ((Read 'Briefcase.ServerLauncher.exe') -eq 'new') 'New launcher missing.'
Check ((Read 'Briefcase/Mods/test.server/Data/config.json') -eq 'local') 'Local mod configuration lost.'
Check ((Read 'Briefcase/Mods/private.mod/Private.dll') -eq 'private') 'Private mod lost.'
Check ((Read 'Briefcase/Admin/server.json') -eq 'local secret fixture') 'Admin config overwritten.'
Check (-not (Test-Path -LiteralPath (Join-Path $win64 'Briefcase/Runtime/Obsolete.dll'))) 'Obsolete managed file retained.'
Check ((Read 'Briefcase/Updates/transaction.json'|ConvertFrom-Json).state -eq 'committed') 'Transaction not committed.'

# Inject a single write failure after the first mutation. Real disk copies and real recovery.
Put (Join-Path $win64 'Briefcase.ServerLauncher.exe') 'previous'
$script:copyImplementation=${function:Copy-UpdateFile}
$script:failOnce=$true
function Copy-UpdateFile([string]$Source,[string]$Destination) {
    if ($script:failOnce -and $Source.StartsWith($stage) -and $Destination.EndsWith('StartBriefcaseNativeServer.ps1')) {
        $script:failOnce=$false; throw 'Simulated write failure'
    }
    & $script:copyImplementation $Source $Destination
}
Reject { Install-UpdatePackage $win64 $stage $validated ([guid]::NewGuid().ToString('N')) } 'Write failure hidden.'
Check ((Read 'Briefcase.ServerLauncher.exe') -eq 'previous') 'Previous DLL not restored.'
Check ((Read 'Briefcase/Updates/transaction.json'|ConvertFrom-Json).state -eq 'rolled-back') 'Recovery not journalled.'
${function:Copy-UpdateFile}=$script:copyImplementation

# Simulate power loss after a mutation: recovery must happen even with no updater configuration.
$id=[guid]::NewGuid().ToString('N')
$backup=Join-Path $win64 "Briefcase/Updates/$id/backup/Briefcase.ServerLauncher.exe"
Put $backup 'previous'
Put (Join-Path $win64 'Briefcase.ServerLauncher.exe') 'interrupted'
Write-UpdateJson (Join-Path $win64 'Briefcase/Updates/transaction.json') @{id=$id;state='installing';files=@(
    @{path='Briefcase.ServerLauncher.exe';existed=$true;previousHash=(Get-FileHash -LiteralPath $backup).Hash})}
function Get-UpdateDownload { throw 'Simulated GitHub outage' }
Check ((Invoke-ServerUpdate $win64 3>$null) -eq 'failed-kept-installed') 'Offline recovery did not retain the installed version.'
Check ((Read 'Briefcase.ServerLauncher.exe') -eq 'previous') 'Interrupted transaction not recovered.'
$journal=Read 'Briefcase/Updates/transaction.json'|ConvertFrom-Json
$journal.state='installing';Write-UpdateJson (Join-Path $win64 'Briefcase/Updates/transaction.json') $journal
Put $backup 'damaged'
Reject { Invoke-ServerUpdate $win64 } 'Corrupt recovery must block launch.'
$journal.state='rolled-back';Write-UpdateJson (Join-Path $win64 'Briefcase/Updates/transaction.json') $journal

# Actual startup orchestration with a substituted transport, never internet or a real server.
Put (Join-Path $win64 'Briefcase/Updater/build.json') '{"frameworkVersion":"0.3.0"}'
Write-UpdateJson (Join-Path $win64 'Briefcase/updater.json') @{schemaVersion=1;enabled=$true;repository='';timeoutSeconds=20}
$script:requests=0
$script:fixtureArchive=$archive
function Get-UpdateDownload([string]$Url,[string]$Destination,[int]$TimeoutSeconds,[long]$MaxBytes) {
    $script:requests++
    if ($Url.EndsWith('/latest')) { Write-UpdateJson $Destination $release }
    else { Copy-Item -LiteralPath $script:fixtureArchive -Destination $Destination }
}
Check ((Invoke-ServerUpdate $win64) -eq 'not-configured' -and $script:requests -eq 0) 'Empty repo made a request.'
Write-UpdateJson (Join-Path $win64 'Briefcase/updater.json') @{schemaVersion=1;enabled=$true;repository='example/repo';timeoutSeconds=20}
$customConfig = Read 'Briefcase/updater.json'
$release.assets[0].size=(Get-Item -LiteralPath $archive).Length
$release.assets[0].digest='sha256:'+(Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant()
Check ((Invoke-ServerUpdate $win64) -eq 'installed 0.4.0') 'Full automatic update failed.'
Check ($script:requests -eq 2) 'Expected one metadata and one archive request.'
Check ((Read 'Briefcase/updater.json') -ceq $customConfig) 'Custom updater configuration overwritten.'
Check ((Invoke-ServerUpdate $win64) -eq 'current') 'Installed version not retained.'

# A user extracting the ZIP and launching it must get the official feed immediately.
$configPath = Join-Path $win64 'Briefcase/updater.json'
Remove-Item -LiteralPath $configPath
Put (Join-Path $win64 'Briefcase/Updater/build.json') '{"frameworkVersion":"0.3.0"}'
$release.assets[0].browser_download_url=$release.assets[0].browser_download_url.Replace('example/repo','EnoPM/BriefcaseNative')
$script:requests=0
function Get-UpdateDownload([string]$Url,[string]$Destination,[int]$TimeoutSeconds,[long]$MaxBytes) {
    $script:requests++
    Check ($TimeoutSeconds -eq 20) 'Default timeout not used.'
    if ($Url.EndsWith('/latest')) {
        Check ($Url -ceq 'https://api.github.com/repos/EnoPM/BriefcaseNative/releases/latest') 'Official repository not selected.'
        Write-UpdateJson $Destination $release
    } else { Copy-Item -LiteralPath $script:fixtureArchive -Destination $Destination }
}
Check ((Invoke-ServerUpdate $win64) -eq 'installed 0.4.0') 'Fresh installation did not check and install an update.'
Check ($script:requests -eq 2) 'Fresh installation skipped its update check.'
$defaults=Read 'Briefcase/updater.json'|ConvertFrom-Json
$template=Get-Content -LiteralPath (Join-Path $project 'scripts/update/updater.example.json') -Raw|ConvertFrom-Json
foreach ($key in @('schemaVersion','enabled','updateMods','repository','timeoutSeconds')) {
    Check ($defaults.$key -ceq $template.$key) "First-launch default differs from shipped example: $key"
}
$original=Read 'Briefcase/updater.json'
Check ((Invoke-ServerUpdate $win64) -eq 'current') 'Fresh config not reusable.'
Check ((Read 'Briefcase/updater.json') -ceq $original) 'Existing defaults rewritten.'

Write-UpdateJson $configPath @{schemaVersion=1;enabled=$false;repository='Custom/Repo';timeoutSeconds=47}
$original=Read 'Briefcase/updater.json';$before=$script:requests
Check ((Invoke-ServerUpdate $win64) -eq 'disabled') 'Explicit opt-out ignored.'
Check ($script:requests -eq $before) 'Disabled updater made a request.'
Check ((Read 'Briefcase/updater.json') -ceq $original) 'Disabled updater settings overwritten.'
Put $configPath '{invalid user config'
Check ((Invoke-ServerUpdate $win64 3>$null) -eq 'failed-kept-installed') 'Invalid user config not handled.'
Check ((Read 'Briefcase/updater.json') -ceq '{invalid user config') 'Invalid existing configuration silently replaced.'
$invalidModResult=Invoke-ModUpdates $win64 3>$null
Check ($invalidModResult.state -ceq 'failed-kept-installed') 'Invalid config blocked startup during mod update checks.'

Write-UpdateJson $configPath @{schemaVersion=1;enabled=$true;repository='Custom/Repo';timeoutSeconds=47}
$original=Read 'Briefcase/updater.json'
function Get-UpdateDownload([string]$Url,[string]$Destination,[int]$TimeoutSeconds,[long]$MaxBytes) {
    Check ($Url -ceq 'https://api.github.com/repos/Custom/Repo/releases/latest' -and $TimeoutSeconds -eq 47) 'Existing custom settings not used.'
    Write-UpdateJson $Destination $release
}
Check ((Invoke-ServerUpdate $win64) -eq 'current') 'Custom settings ignored.'
Check ((Read 'Briefcase/updater.json') -ceq $original) 'Custom settings rewritten.'
Remove-Item -LiteralPath $configPath
function Get-UpdateDownload { throw 'Simulated GitHub outage' }
Check ((Invoke-ServerUpdate $win64 3>$null) -eq 'failed-kept-installed') 'Offline launch must keep installed version.'
Check ((Read 'Briefcase/updater.json'|ConvertFrom-Json).enabled -eq $true) 'Offline first launch did not save defaults.'
Check ((Read 'Briefcase.ServerLauncher.exe') -eq 'new') 'Offline attempt changed binary.'

# Installed mods opt in through their manifest. They update before launch while
# local configuration remains untouched.
$modId='test.early';$modRepository='Example/Test.Early';$modRoot=Join-Path $win64 "Briefcase/Mods/$modId"
$installedMod=@{schemaVersion=1;id=$modId;name='Early';author='Test';version='1.0.0';entry='Test.Early.dll';minimumApi=1;loadPhase='startup';environment='server';capabilities=@('log');dependencies=@();update=@{provider='github-releases';repository=$modRepository}}
Write-UpdateJson (Join-Path $modRoot 'briefcase.mod.json') $installedMod
Put (Join-Path $modRoot 'Test.Early.dll') 'MZold'
Put (Join-Path $modRoot 'Data/config.json') '{"local":true}'
$modStage=Join-Path $fixture 'mod-stage';$modPrefix="Briefcase/Mods/$modId"
$packagedMod=$installedMod.Clone();$packagedMod.version='1.1.0'
Write-UpdateJson (Join-Path $modStage "$modPrefix/briefcase.mod.json") $packagedMod
Put (Join-Path $modStage "$modPrefix/Test.Early.dll") 'MZnew'
Put (Join-Path $modStage "$modPrefix/Data/config.json") '{"local":false}'
Put (Join-Path $modStage "$modPrefix/Licenses/Test.txt") 'license'
$modRows=@(Get-ChildItem -LiteralPath (Join-Path $modStage 'Briefcase') -Recurse -File|ForEach-Object{
    $relative=$_.FullName.Substring($modStage.Length+1).Replace('\','/')
    $row=@{path=$relative;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant();mode=420}
    if($relative -ceq "$modPrefix/Data/config.json"){$row.preserve=$true};$row
})
Write-UpdateJson (Join-Path $modStage 'ModPackage.json') @{updateSchema=1;kind='briefcase-mod';platform='windows-x64';modId=$modId;version='1.1.0';repository=$modRepository;files=$modRows}
$null=Read-ModUpdatePackage $modStage $modId '1.1.0' $modRepository 'windows-x64'
$modArchive=Join-Path $fixture 'Test.Early-windows-x64-1.1.0.zip'
[IO.Compression.ZipFile]::CreateFromDirectory($modStage,$modArchive)
$modRelease=[pscustomobject]@{tag_name='v1.1.0';draft=$false;prerelease=$false;assets=@([pscustomobject]@{
    name='Test.Early-windows-x64-1.1.0.zip';state='uploaded';size=(Get-Item $modArchive).Length;
    digest='sha256:'+((Get-FileHash $modArchive).Hash.ToLowerInvariant());browser_download_url='https://github.com/Example/Test.Early/releases/download/v1.1.0/Test.Early-windows-x64-1.1.0.zip'})}
Write-UpdateJson $configPath @{schemaVersion=1;enabled=$true;updateMods=$true;repository='';timeoutSeconds=20}
$script:modRequests=0
function Get-UpdateDownload([string]$Url,[string]$Destination,[int]$TimeoutSeconds,[long]$MaxBytes){
    $script:modRequests++
    if($Url.EndsWith('/latest')){Write-UpdateJson $Destination $modRelease}else{Copy-Item -LiteralPath $modArchive -Destination $Destination}
}
$modResult=Invoke-ModUpdates $win64
Check ($modResult.checked -eq 1 -and $modResult.updated -eq 1 -and $script:modRequests -eq 2) 'Startup mod was not updated.'
Check ((Read "$modPrefix/Test.Early.dll") -ceq 'MZnew') 'Updated mod binary missing.'
Check ((Read "$modPrefix/Data/config.json") -ceq '{"local":true}') 'Mod configuration was overwritten.'
Check ((Read "$modPrefix/briefcase.mod.json"|ConvertFrom-Json).version -ceq '1.1.0') 'Updated mod manifest missing.'
Check ((Read "$modPrefix/.briefcase-update.json"|ConvertFrom-Json).version -ceq '1.1.0') 'Mod update metadata missing.'
$modResult=Invoke-ModUpdates $win64
Check ($modResult.current -eq 1 -and $script:modRequests -eq 3) 'Current mod downloaded again.'
Write-UpdateJson $configPath @{schemaVersion=1;enabled=$true;updateMods=$false;repository='';timeoutSeconds=20}
$before=$script:modRequests;$modResult=Invoke-ModUpdates $win64
Check ($modResult.state -ceq 'disabled' -and $script:modRequests -eq $before) 'Mod update opt-out ignored.'
Write-Output "PASS updater contracts: $checks checks (release selection, ZIP boundaries, integrity, preservation, recovery, offline startup)."
