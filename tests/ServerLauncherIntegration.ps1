param([Parameter(Mandatory=$true)][string]$Build)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot
$win64=Join-Path $project ('artifacts/proxy-integration-'+[guid]::NewGuid().ToString('N')+'/DeceiveInc/Binaries/Win64')
$runtime=Join-Path $win64 'Briefcase/Core'
$update=Join-Path $win64 'Briefcase/Core/Updater'
$tools=Join-Path $win64 'Briefcase/Core/Tools'
$ue4ss=Join-Path $win64 'ue4ss'
New-Item -ItemType Directory -Path $runtime,$update,$tools,(Join-Path $ue4ss 'Mods') -Force|Out-Null
$engine=Join-Path (Split-Path (Split-Path $win64)) 'Saved/Config/WindowsServer/Engine.ini'
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($engine)) -Force|Out-Null
"[ConsoleVariables]`nsb.DisableEAC=0`nFixtureSetting=7`n"|Set-Content -LiteralPath $engine -NoNewline
Copy-Item -LiteralPath (Join-Path $Build 'version.dll') -Destination $win64
Copy-Item -LiteralPath (Join-Path $Build 'Briefcase.LauncherMockHost.dll') -Destination (Join-Path $runtime 'Briefcase.NativeHost.dll')
Copy-Item -LiteralPath (Join-Path $Build 'Briefcase.LauncherMockHost.dll') -Destination (Join-Path $ue4ss 'UE4SS.dll')
Copy-Item -LiteralPath (Join-Path $Build 'Briefcase.ServerLauncherContracts.exe') -Destination (Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe')
Copy-Item -LiteralPath (Join-Path $Build 'Briefcase.ServerUpdater.exe') -Destination $tools
''|Set-Content -LiteralPath (Join-Path $ue4ss 'Mods/mods.txt') -NoNewline
'[General]'|Set-Content -LiteralPath (Join-Path $ue4ss 'UE4SS-settings.ini')
@{schemaVersion=1;enabled=$false;updateMods=$false;repository='EnoPM/BriefcaseNative';timeoutSeconds=20}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $win64 'Briefcase/updater.json')
$previous=$env:BC_TEST_SERVER_LIFETIME
$env:BC_TEST_SERVER_LIFETIME='60000'
$run=$null
try {
    $initial=Start-Process -FilePath (Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe') -WorkingDirectory $win64 -WindowStyle Hidden -PassThru
    $result=$null
    for($i=0;$i -lt 300;$i++){
        $result=Get-Item -LiteralPath (Join-Path $win64 'Briefcase/Updates/last-result.json') -ErrorAction SilentlyContinue
        if($result){break}
        Start-Sleep -Milliseconds 100
    }
    if(-not $result){throw 'Proxy update probe did not complete.'}
    $probe=Get-Content -LiteralPath $result.FullName -Raw|ConvertFrom-Json
    if($probe.framework -ne 'disabled'){throw 'Update probe contract failed.'}
    $launch=Get-Content -LiteralPath (Join-Path $win64 'Briefcase/launch.json') -Raw|ConvertFrom-Json
    if($launch.serverWin64 -ine $win64){throw 'Fresh installation did not initialize launch.json.'}
    $engineText=Get-Content -LiteralPath $engine -Raw
    if($engineText -notmatch '(?m)^sb\.DisableEAC=1$' -or $engineText -notmatch '(?m)^FixtureSetting=7$' -or
       ([regex]::Matches($engineText,'(?im)^\s*sb\.DisableEAC\s*=')).Count -ne 1){
        throw 'Coordinator did not configure EAC-free server mode conservatively.'
    }
    if($initial.HasExited){throw 'Proxy relaunched the server without an update.'}
    if((Get-Process -Id $initial.Id).Path -ine (Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe')){throw 'Unexpected process.'}
    if(Get-ChildItem -LiteralPath (Join-Path $win64 'Briefcase/Logs') -Filter 'launch-*.json' -ErrorAction SilentlyContinue){
        throw 'Coordinator created an unnecessary relaunch record.'
    }
    if(-not(Test-Path -LiteralPath (Join-Path $win64 'entry-verified.txt'))){throw 'Game entered without proxy preparation.'}
    Start-Sleep -Seconds 4
    if((Get-Process -Id $initial.Id -ErrorAction SilentlyContinue) -eq $null){throw 'Original server did not remain running.'}
    Write-Output 'PASS direct Shipping -> version proxy -> update probe -> same Shipping process; preparation occurred before CRT with Win64 cwd.'
} finally {
    $env:BC_TEST_SERVER_LIFETIME=$previous
    if($initial){Stop-Process -Id $initial.Id -Force -ErrorAction SilentlyContinue}
}
