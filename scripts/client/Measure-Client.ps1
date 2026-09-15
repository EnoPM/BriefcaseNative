[CmdletBinding()]param([string]$ClientWin64='',[string]$SettingsPath='',[ValidateRange(1,30)][int]$Seconds=10)
. (Join-Path $PSScriptRoot 'Common.ps1')
$paths=Get-ClientPaths $ClientWin64 $SettingsPath
$active=@(Get-ClientProcesses $paths)
if($active.Count -ne 1){throw 'Exactly one instance of this client copy is required.'}
$p=Get-Process -Id $active[0].ProcessId
$cpu=$p.TotalProcessorTime.TotalSeconds;$timer=[Diagnostics.Stopwatch]::StartNew()
Start-Sleep -Seconds $Seconds
$p.Refresh();$elapsed=$timer.Elapsed.TotalSeconds
$path=Join-Path $paths.Win64 'Briefcase\Logs\client-metrics.json'
$previous=if(Test-Path -LiteralPath $path){(Get-Item -LiteralPath $path).LastWriteTimeUtc}else{[DateTime]::MinValue}
$event=[Threading.EventWaitHandle]::OpenExisting("Local\BriefcaseNative.Metrics.$($p.Id)")
$null=$event.Set();$event.Dispose()
for($i=0;$i -lt 40;$i++){
    if((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).LastWriteTimeUtc -gt $previous){break}
    Start-Sleep -Milliseconds 100
}
if(-not(Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).LastWriteTimeUtc -le $previous){throw 'Runtime metrics snapshot timed out.'}
[ordered]@{
    pid=$p.Id;executable=$p.Path;sampleSeconds=$elapsed;cpuPercentOneCore=100*($p.TotalProcessorTime.TotalSeconds-$cpu)/$elapsed;
    workingSetMiB=$p.WorkingSet64/1MB;modules=@($p.Modules|Where-Object {$_.ModuleName -match 'Briefcase|d3d11|dxgi|version.dll|UE4SS'}|ForEach-Object {$_.FileName});
    rendering=(Get-Content -LiteralPath $path -Raw|ConvertFrom-Json)
}|ConvertTo-Json -Depth 5
