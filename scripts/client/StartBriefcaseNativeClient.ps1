[CmdletBinding()]param([string[]]$ClientArguments=@(),[switch]$ValidateOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$win64=[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$config=Get-Content -LiteralPath (Join-Path $win64 'Briefcase\launch.json') -Raw|ConvertFrom-Json
$root=[IO.Path]::GetFullPath($config.allowedClientRoot).TrimEnd('\')
if([IO.Path]::GetFileName($root) -ine 'DeceiveIncNativeClient' -or
    $win64 -ine (Join-Path $root 'DeceiveInc\Binaries\Win64') -or $win64 -ine $config.clientWin64){
    throw 'Launcher must reside in the authorized client Win64 directory.'
}
$exe=Join-Path $win64 'DeceiveInc-Win64-Shipping.exe'
$probe=$exe
while($probe){
    if((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Reparse point forbidden: $probe"}
    $probe=[IO.Path]::GetDirectoryName($probe)
}
if((Get-FileHash -LiteralPath $exe).Hash -ine $config.executableSha256){throw 'Client executable differs from the deployed build.'}
$gameLog=Join-Path $win64 'Briefcase\Logs\DeceiveInc-client.log'
$arguments=@('-ABSLOG="'+$gameLog+'"')+$ClientArguments
$launch=[ordered]@{executable=$exe;workingDirectory=$win64;arguments=$arguments}
if($ValidateOnly){return [pscustomobject]$launch}
$active=@(Get-CimInstance Win32_Process -Filter "Name = 'DeceiveInc-Win64-Shipping.exe'"|Where-Object {$_.ExecutablePath -ieq $exe})
if($active.Count){throw "This client copy is already running: $($active.ProcessId -join ', ')"} 
if($ClientArguments|Where-Object {$_ -match '[\r\n"]|\s'}){throw 'Use separate arguments without embedded spaces or quotes.'}
$parameters=@{FilePath=$exe;WorkingDirectory=$win64;WindowStyle='Normal';PassThru=$true}
$parameters.ArgumentList=$arguments
$process=Start-Process @parameters
$launch.pid=$process.Id;$launch.startedAt=$process.StartTime.ToString('o')
$launch|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $win64 'Briefcase\Logs\last-launch.json') -Encoding utf8
Start-Sleep -Seconds 3
$process.Refresh()
if($process.HasExited){throw "Client exited during bootstrap: $($process.ExitCode)"}
[pscustomobject]$launch
