[CmdletBinding()]
param([ValidateRange(15,3600)][int]$DurationSeconds=180,[ValidateRange(5,60)][int]$SampleSeconds=15)
. (Join-Path $PSScriptRoot '..\deploy\Common.ps1')
$paths=Get-DeploymentPaths
$active=@(Get-CimInstance Win32_Process -Filter "Name = 'DeceiveIncServer-Win64-Shipping.exe'"|Where-Object{$_.ExecutablePath -ieq $paths.Executable})
if($active.Count -ne 1){throw 'Expected exactly one authorized server process.'}
$process=Get-Process -Id $active[0].ProcessId
$report=Join-Path $paths.Project ('artifacts\stability-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.json')
$points=[Collections.Generic.List[object]]::new()
$timer=[Diagnostics.Stopwatch]::StartNew()
while($true){
    $process.Refresh()
    if($process.HasExited){throw "Server exited during observation: $($process.ExitCode)"}
    $points.Add([pscustomobject]@{elapsedSeconds=[math]::Round($timer.Elapsed.TotalSeconds,2);pid=$process.Id;cpuSeconds=$process.TotalProcessorTime.TotalSeconds;privateBytes=$process.PrivateMemorySize64;workingSetBytes=$process.WorkingSet64;handles=$process.HandleCount;threads=$process.Threads.Count;mainWindow=$process.MainWindowHandle.ToInt64()})
    $points|ConvertTo-Json -Depth 3|Set-Content -LiteralPath $report -Encoding utf8
    if($timer.Elapsed.TotalSeconds -ge $DurationSeconds){break}
    Start-Sleep -Milliseconds ([math]::Max(1,[math]::Min($SampleSeconds*1000,($DurationSeconds-$timer.Elapsed.TotalSeconds)*1000)))
}
Write-Host "Observation complete: $report"
$points|Select-Object -First 1
$points|Select-Object -Last 1
