param([Parameter(Mandatory=$true)][string]$Build)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot
$win64=Join-Path $project ('artifacts/proxy-integration-'+[guid]::NewGuid().ToString('N')+'/DeceiveInc/Binaries/Win64')
$runtime=Join-Path $win64 'Briefcase/Core'
$update=Join-Path $win64 'Briefcase/Core/Updater'
$tools=Join-Path $win64 'Briefcase/Core/Tools'
$appNative=Join-Path $project ('artifacts/serverapp-injection-fixture-'+[guid]::NewGuid().ToString('N')+'/Native')
$ue4ss=Join-Path $win64 'ue4ss'
New-Item -ItemType Directory -Path $runtime,$update,$tools,$appNative,(Join-Path $ue4ss 'Mods') -Force|Out-Null
$engine=Join-Path (Split-Path (Split-Path $win64)) 'Saved/Config/WindowsServer/Engine.ini'
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($engine)) -Force|Out-Null
"[ConsoleVariables]`nsb.DisableEAC=1`nFixtureSetting=7`n"|Set-Content -LiteralPath $engine -NoNewline
Copy-Item -LiteralPath (Join-Path $Build 'version.dll') -Destination $win64
Copy-Item -LiteralPath (Join-Path $Build 'Briefcase.LauncherMockHost.dll') -Destination (Join-Path $runtime 'Briefcase.NativeHost.dll')
Copy-Item -LiteralPath (Join-Path $Build 'Briefcase.LauncherMockHost.dll') -Destination (Join-Path $ue4ss 'UE4SS.dll')
Copy-Item -LiteralPath (Join-Path $Build 'Briefcase.ServerLauncherContracts.exe') -Destination (Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe')
Copy-Item -LiteralPath (Join-Path $Build 'Briefcase.ServerUpdater.exe') -Destination $tools
Copy-Item -LiteralPath (Join-Path $Build 'Briefcase.ServerInjector.exe') -Destination $appNative
Copy-Item -LiteralPath (Join-Path $Build 'Briefcase.ServerBootstrap.dll') -Destination $appNative
''|Set-Content -LiteralPath (Join-Path $ue4ss 'Mods/mods.txt') -NoNewline
'[General]'|Set-Content -LiteralPath (Join-Path $ue4ss 'UE4SS-settings.ini')
@{schemaVersion=1;enabled=$false;updateMods=$false;repository='EnoPM/BriefcaseNative';timeoutSeconds=20}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $win64 'Briefcase/updater.json')
$previous=$env:BC_TEST_SERVER_LIFETIME
$env:BC_TEST_SERVER_LIFETIME='60000'
$run=$null
try {
    $initial=Start-Process -FilePath (Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe') -WorkingDirectory $win64 -WindowStyle Hidden -PassThru
    $entered=$false
    for($i=0;$i -lt 300;$i++){
        if(Test-Path -LiteralPath (Join-Path $win64 'entry-verified.txt')){$entered=$true;break}
        Start-Sleep -Milliseconds 100
    }
    if(-not $entered){throw 'Proxy did not prepare Shipping before its entry point.'}
    if(Test-Path -LiteralPath (Join-Path $win64 'Briefcase/Updates/last-result.json')){throw 'Proxy invoked native updates.'}
    $engineText=Get-Content -LiteralPath $engine -Raw
    if($engineText -notmatch '(?m)^sb\.DisableEAC=1$' -or $engineText -notmatch '(?m)^FixtureSetting=7$' -or
       ([regex]::Matches($engineText,'(?im)^\s*sb\.DisableEAC\s*=')).Count -ne 1){
        throw 'Proxy changed prepared EAC configuration.'
    }
    if($initial.HasExited){throw 'Proxy relaunched the server without an update.'}
    if((Get-Process -Id $initial.Id).Path -ine (Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe')){throw 'Unexpected process.'}
    if(Get-ChildItem -LiteralPath (Join-Path $win64 'Briefcase/Logs') -Filter 'launch-*.json' -ErrorAction SilentlyContinue){
        throw 'Coordinator created an unnecessary relaunch record.'
    }
    Start-Sleep -Seconds 4
    if((Get-Process -Id $initial.Id -ErrorAction SilentlyContinue) -eq $null){throw 'Original server did not remain running.'}
    Write-Output 'PASS direct Shipping -> version proxy -> same process with no native update check.'
} finally {
    $env:BC_TEST_SERVER_LIFETIME=$previous
    if($initial){Stop-Process -Id $initial.Id -Force -ErrorAction SilentlyContinue}
}
Remove-Item -LiteralPath (Join-Path $win64 'entry-verified.txt') -ErrorAction SilentlyContinue
$injectedPid = & (Join-Path $appNative 'Briefcase.ServerInjector.exe') '--server' (Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe') '-Port=51000' '-QueryPort=51001'
if($LASTEXITCODE -ne 0 -or $injectedPid -notmatch '^[0-9]+$'){throw 'App injector failed to start Shipping.'}
try {
    for($i=0;$i -lt 100 -and -not(Test-Path -LiteralPath (Join-Path $win64 'entry-verified.txt'));$i++){
        Start-Sleep -Milliseconds 100
    }
    if(-not(Test-Path -LiteralPath (Join-Path $win64 'entry-verified.txt'))){throw 'Injected bootstrap did not prepare Shipping.'}
    if(Test-Path -LiteralPath (Join-Path $win64 'Briefcase/Updates/last-result.json')){throw 'Injected launch invoked native updates.'}
    Write-Output 'PASS ServerApp injector starts Shipping with proxy present and prepares it once before CRT.'
} finally {
    Stop-Process -Id ([int]$injectedPid) -Force -ErrorAction SilentlyContinue
}
Move-Item -LiteralPath (Join-Path $win64 'version.dll') -Destination (Join-Path $win64 'version.dll.disabled')
Remove-Item -LiteralPath (Join-Path $win64 'entry-verified.txt') -ErrorAction SilentlyContinue
$injectedPid = & (Join-Path $appNative 'Briefcase.ServerInjector.exe') '--server' (Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe') '-Port=51000' '-QueryPort=51001'
if($LASTEXITCODE -ne 0 -or $injectedPid -notmatch '^[0-9]+$'){throw 'App injector failed without the proxy.'}
try {
    for($i=0;$i -lt 100 -and -not(Test-Path -LiteralPath (Join-Path $win64 'entry-verified.txt'));$i++){
        Start-Sleep -Milliseconds 100
    }
    if(-not(Test-Path -LiteralPath (Join-Path $win64 'entry-verified.txt'))){throw 'Injected bootstrap did not prepare proxy-free Shipping.'}
    Write-Output 'PASS ServerApp injector starts modded Shipping while the proxy is disabled.'
} finally {
    Stop-Process -Id ([int]$injectedPid) -Force -ErrorAction SilentlyContinue
}
Remove-Item -LiteralPath (Join-Path $win64 'entry-verified.txt') -ErrorAction SilentlyContinue
$env:BC_TEST_SERVER_LIFETIME='60000'
$previousInjector=$env:BRIEFCASE_SERVERAPP_INJECTOR
$env:BRIEFCASE_SERVERAPP_INJECTOR=Join-Path $appNative 'Briefcase.ServerInjector.exe'
$coordinator=Start-Process -FilePath (Join-Path $tools 'Briefcase.ServerUpdater.exe') -ArgumentList @('--root',('"'+$win64+'"'),'--parent','99999999','--') -WorkingDirectory $win64 -WindowStyle Hidden -PassThru
if(-not $coordinator.WaitForExit(15000)){
    Stop-Process -Id $coordinator.Id -Force -ErrorAction SilentlyContinue
    throw 'Restart coordinator timed out.'
}
$env:BC_TEST_SERVER_LIFETIME=$previous
$env:BRIEFCASE_SERVERAPP_INJECTOR=$previousInjector
if($coordinator.ExitCode -ne 0){throw 'Restart coordinator could not inject Shipping.'}
$record=Get-ChildItem -LiteralPath (Join-Path $win64 'Briefcase/Logs') -Filter 'launch-*.json' | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if(-not $record){throw 'Restart coordinator did not record the new server.'}
$restartLaunch=Get-Content -LiteralPath $record.FullName -Raw | ConvertFrom-Json
try {
    if(-not(Test-Path -LiteralPath (Join-Path $win64 'entry-verified.txt'))){throw 'Restart coordinator launched unmodded Shipping.'}
    if(Test-Path -LiteralPath (Join-Path $win64 'Briefcase/Updates/last-result.json')){throw 'Restart coordinator performed a native update check.'}
    Write-Output 'PASS restart coordinator delegates to the app-private injector with the proxy disabled.'
} finally {
    Stop-Process -Id ([int]$restartLaunch.pid) -Force -ErrorAction SilentlyContinue
}
