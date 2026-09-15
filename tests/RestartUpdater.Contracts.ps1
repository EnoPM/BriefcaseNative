# Real native helper + PowerShell handoff against a renamed ping process, never the game.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot
$fixture=Join-Path $project ('artifacts/restart-updater-'+[guid]::NewGuid().ToString('N'))
$win64=Join-Path $fixture 'DeceiveInc/Binaries/Win64'
$root=Join-Path $win64 'Briefcase'
foreach($dir in @('Tools','Updater','Admin')) { New-Item -ItemType Directory -Path (Join-Path $root $dir) -Force|Out-Null }
Copy-Item -LiteralPath (Join-Path $project 'build/Briefcase.ServerRestart.exe') -Destination (Join-Path $root 'Tools')
foreach($name in @('Updater.ps1','Restart-Server.ps1')) { Copy-Item -LiteralPath (Join-Path $project "scripts/update/$name") -Destination (Join-Path $root 'Updater') }
$exe=Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe'
Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/ping.exe') -Destination $exe
$ini=Join-Path $fixture 'DeceiveInc/Saved/Config/WindowsServer/TripwireServer.ini'
New-Item -ItemType Directory -Path (Split-Path $ini) -Force|Out-Null
[IO.File]::WriteAllText($ini,"[/Script/DeceiveInc.TripwireServerSettings]`nGamePort=50100`nQueryPort=50101`n")
# This substitute launcher verifies the helper binary is no longer locked, then returns a fake PID.
$launcher=@'
param([string[]]$ServerArguments)
$file=Join-Path $PSScriptRoot 'Briefcase/Tools/Briefcase.ServerRestart.exe'
$handle=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$handle.Dispose()
if (($ServerArguments -join ',') -ne '-TestFlag') { throw 'Restart arguments changed.' }
[pscustomobject]@{pid=12345}
'@
[IO.File]::WriteAllText((Join-Path $win64 'StartBriefcaseNativeServer.ps1'),$launcher)
$id=[guid]::NewGuid().ToString('N')
$ready=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,"Local\BriefcaseNative.Restart.$id.Ready")
$commit=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,"Local\BriefcaseNative.Restart.$id.Commit")
$fake=$null;$helper=$null
try {
    $fake=Start-Process -FilePath $exe -ArgumentList @('-t','127.0.0.1') -WorkingDirectory $win64 -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $fixture 'ping.log')
    $ticket=@{requestId=$id;pid=$fake.Id;created=$fake.StartTime.ToFileTimeUtc();arguments=@('-unattended','-NoSplash','-Port=50000','-QueryPort=50001','-TestFlag')}
    $ticket|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root 'Admin/restart-request.json') -Encoding ascii
    $helper=Start-Process -FilePath (Join-Path $root 'Tools/Briefcase.ServerRestart.exe') -ArgumentList $id -WorkingDirectory $win64 -WindowStyle Hidden -PassThru
    if(-not $ready.WaitOne(10000)){throw 'Native restart helper never became ready.'}
    $fake.Refresh()
    if($fake.HasExited){throw 'Helper stopped process before acknowledgement.'}
    $null=$commit.Set()
    if(-not $helper.WaitForExit(15000) -or $helper.ExitCode -ne 0){throw 'Native restart helper failed.'}
    if(-not $fake.WaitForExit(5000)){throw 'Old process still running.'}
    $resultPath=Join-Path $root 'Admin/restart-result.json'
    $deadline=[DateTime]::UtcNow.AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 200
        $result=Get-Content -LiteralPath $resultPath -Raw|ConvertFrom-Json
    } while($result.state -eq 'starting' -and [DateTime]::UtcNow -lt $deadline)
    if($result.state -ne 'started' -or $result.pid -ne 12345 -or $result.workingDirectory -ine $win64){throw "Restart handoff failed: $($result|ConvertTo-Json -Compress)"}
    Write-Output 'PASS native restart handoff: acknowledgement, exact process exit, helper unlock, launcher arguments and final result.'
} finally {
    foreach($process in @($helper,$fake)) {
        if($process){$process.Refresh();if(-not $process.HasExited){$process.Kill()};$process.Dispose()}
    }
    $ready.Dispose();$commit.Dispose()
}
