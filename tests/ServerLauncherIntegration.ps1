param([Parameter(Mandatory=$true)][string]$Build)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot
$win64=Join-Path $project ('artifacts/launcher-integration-'+[guid]::NewGuid().ToString('N')+'/DeceiveInc/Binaries/Win64')
$runtime=Join-Path $win64 'Briefcase/Core'
$update=Join-Path $win64 'Briefcase/Core/Updater'
$tools=Join-Path $win64 'Briefcase/Core/Tools'
$admin=Join-Path $win64 'Briefcase/Admin'
New-Item -ItemType Directory -Path $runtime,$update,$tools,$admin -Force|Out-Null
Copy-Item -LiteralPath (Join-Path $Build 'Briefcase.ServerLauncher.exe') -Destination $win64
Copy-Item -LiteralPath (Join-Path $Build 'Briefcase.ServerBootstrap.dll') -Destination $runtime
Copy-Item -LiteralPath (Join-Path $Build 'Briefcase.LauncherMockHost.dll') -Destination (Join-Path $runtime 'Briefcase.NativeHost.dll')
Copy-Item -LiteralPath (Join-Path $Build 'Briefcase.ServerLauncherContracts.exe') -Destination (Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe')
Copy-Item -LiteralPath (Join-Path $Build 'Briefcase.ServerUpdater.exe') -Destination $tools
@{schemaVersion=1;enabled=$false;repository='EnoPM/BriefcaseNative';timeoutSeconds=20}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $win64 'Briefcase/updater.json')
$previous=$env:BC_TEST_SERVER_LIFETIME
$env:BC_TEST_SERVER_LIFETIME='60000'
$run=$null
$restartRun=$null
try {
    $initial=Start-Process -FilePath (Join-Path $win64 'Briefcase.ServerLauncher.exe') -WorkingDirectory $project -WindowStyle Hidden -PassThru
    if(-not $initial.WaitForExit(15000)){throw 'Initial launcher did not release itself for updates.'}
    $record=$null
    for($i=0;$i -lt 300;$i++){
        $record=Get-ChildItem -LiteralPath (Join-Path $win64 'Briefcase/Logs') -Filter 'launch-*.json' -ErrorAction SilentlyContinue|Select-Object -First 1
        if($record){break}
        Start-Sleep -Milliseconds 100
    }
    if(-not $record){throw 'Coordinator did not return while server was running.'}
    $run=Get-Content -LiteralPath $record.FullName -Raw|ConvertFrom-Json
    if($run.update -ne 'disabled' -or $run.workingDirectory -ne $win64){throw 'Update/cwd contract failed.'}
    $launch=Get-Content -LiteralPath (Join-Path $win64 'Briefcase/launch.json') -Raw|ConvertFrom-Json
    if($launch.serverWin64 -ine $win64){throw 'Fresh installation did not initialize launch.json.'}
    $child=Get-Process -Id $run.pid
    if($child.Path -ine (Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe')){throw 'Unexpected child process.'}
    if(-not(Test-Path -LiteralPath (Join-Path $win64 'entry-verified.txt'))){throw 'Game entered without preparation.'}
    # The launch record is written before the three-second startup health check.
    # Leave the fixture alive until that check and coordinator shutdown complete.
    Start-Sleep -Seconds 4
    $errorLog=Join-Path $win64 'Briefcase/Logs/launcher-error.log'
    if((Test-Path -LiteralPath $errorLog) -and (Get-Item -LiteralPath $errorLog).Length){throw 'Coordinator reported a startup failure.'}
    $coordinator=@(Get-CimInstance Win32_Process -Filter "Name LIKE 'worker-%.exe'" -ErrorAction SilentlyContinue|Where-Object {$_.ExecutablePath -like (Join-Path $win64 'Briefcase\Updates\worker-*.exe')})
    if($coordinator.Count){throw 'Native update coordinator still waits for the running server.'}
    $oldChild=Get-Process -Id $run.pid
    Stop-Process -InputObject $oldChild -Force
    $oldChild.WaitForExit(10000)|Out-Null
    $restartId='a'*32
    @{requestId=$restartId;previousPid=$run.pid;workingDirectory=$win64;state='starting'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $win64 'Briefcase/Admin/restart-result.json')
    $restart=Start-Process -FilePath (Join-Path $win64 'Briefcase.ServerLauncher.exe') -WorkingDirectory $win64 -ArgumentList @('--restart',$restartId,'--wait-parent','0') -WindowStyle Hidden -PassThru
    if(-not $restart.WaitForExit(15000)){throw 'Restart launcher did not release itself.'}
    for($i=0;$i -lt 300;$i++){
        $restartRun=Get-Content -LiteralPath (Join-Path $win64 'Briefcase/Admin/restart-result.json') -Raw|ConvertFrom-Json
        if($restartRun.state -eq 'started'){break}
        Start-Sleep -Milliseconds 100
    }
    if($restartRun.state -ne 'started' -or $restartRun.requestId -cne $restartId -or $restartRun.previousPid -ne $run.pid){throw 'Restart result lost its request or previous process identity.'}
    if((Get-Process -Id $restartRun.pid).Path -ine (Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe')){throw 'Restart returned an unexpected child process.'}
    Write-Output 'PASS complete launcher -> native update coordinator -> injection -> restart metadata -> server PID; no PowerShell runtime dependency.'
} finally {
    $env:BC_TEST_SERVER_LIFETIME=$previous
    if($restartRun -and $restartRun.pid){Stop-Process -Id $restartRun.pid -Force -ErrorAction SilentlyContinue}
    if($run -and $run.pid){Stop-Process -Id $run.pid -Force -ErrorAction SilentlyContinue}
}
