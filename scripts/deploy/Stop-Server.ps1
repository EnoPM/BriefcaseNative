[CmdletBinding()]
param([string]$ServerWin64='')
. (Join-Path $PSScriptRoot 'Common.ps1')
$paths=Get-DeploymentPaths -ServerWin64 $ServerWin64
$processes=@(Get-CimInstance Win32_Process -Filter "Name = 'DeceiveIncServer-Win64-Shipping.exe'"|Where-Object{$_.ExecutablePath -ieq $paths.Executable})
foreach($entry in $processes){
    $process=Get-Process -Id $entry.ProcessId -ErrorAction Stop
    if($process.Path -ine $paths.Executable){throw 'Process identity changed; refusing to stop.'}
    $started=$process.StartTime
    $request=$null
    $ack=$null
    try {
        $request=[Threading.EventWaitHandle]::OpenExisting("Local\BriefcaseNative.Stop.$($entry.ProcessId)")
        $ack=[Threading.EventWaitHandle]::OpenExisting("Local\BriefcaseNative.Stopped.$($entry.ProcessId)")
        [void]$request.Set()
        if($ack.WaitOne(5000)){Write-Host 'Mod hooks and callbacks cleaned up.'}
        else{Write-Warning 'Mod cleanup did not acknowledge within 5 seconds; stopping the authorized process.'}
    } catch [Threading.WaitHandleCannotBeOpenedException] {
        # A prior runtime version, or a process still preparing, has no control events.
    } finally {
        if($null -ne $request){$request.Dispose()}
        if($null -ne $ack){$ack.Dispose()}
    }
    $process.Refresh()
    if($process.HasExited){continue}
    $current=Get-Process -Id $entry.ProcessId -ErrorAction Stop
    if($current.Path -ine $paths.Executable -or $current.StartTime -ne $started){throw 'Process identity changed during shutdown.'}
    Stop-Process -InputObject $current -Force
    $process.WaitForExit(10000)|Out-Null
    Write-Host "Stopped authorized server PID $($entry.ProcessId)"
}
