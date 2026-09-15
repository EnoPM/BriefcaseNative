[CmdletBinding()]param([string]$ClientWin64='',[string]$SettingsPath='',[switch]$Force)
. (Join-Path $PSScriptRoot 'Common.ps1')
$paths=Get-ClientPaths $ClientWin64 $SettingsPath
foreach($record in @(Get-ClientProcesses $paths)) {
    $process=[Diagnostics.Process]::GetProcessById($record.ProcessId)
    # Retain a process handle before exit; otherwise ExitCode can be unavailable after Get-Process.
    $null=$process.Handle
    if($process.Path -ine $paths.Executable){throw 'Process identity changed.'}
    try {
        $event=[Threading.EventWaitHandle]::OpenExisting("Local\BriefcaseNative.Stop.$($process.Id)")
        $null=$event.Set();$event.Dispose()
        $ack=[Threading.EventWaitHandle]::OpenExisting("Local\BriefcaseNative.Stopped.$($process.Id)")
        $null=$ack.WaitOne(5000);$ack.Dispose()
    }catch [Threading.WaitHandleCannotBeOpenedException] {}
    $forced=$false
    $process.Refresh()
    if(-not $process.HasExited){$null=$process.CloseMainWindow()}
    if(-not $process.WaitForExit(10000)) {
        if(-not $Force){throw "Client PID $($process.Id) did not close."}
        $fresh=Get-Process -Id $process.Id
        if($fresh.Path -ine $paths.Executable -or $fresh.StartTime -ne $process.StartTime){throw 'Process identity changed before stop.'}
        Stop-Process -Id $process.Id -Force
        $forced=$true
    }
    $process.WaitForExit()
    if(-not $forced -and $process.ExitCode -ne 0){throw "Client exited with code $($process.ExitCode); inspect the crash/game log."}
    [pscustomobject]@{pid=$process.Id;exitCode=$process.ExitCode;forced=$forced}
}
