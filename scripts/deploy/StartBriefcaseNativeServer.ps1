[CmdletBinding()]
param([string[]]$ServerArguments = @(), [switch]$ValidateOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This is the deployed launcher. Never infer the working directory from the caller.
$win64 = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$config = Get-Content -LiteralPath (Join-Path $win64 'Briefcase\launch.json') -Raw | ConvertFrom-Json
$allowed = [IO.Path]::GetFullPath($config.serverWin64).TrimEnd('\')
if ($win64 -ine $allowed -or $win64 -notmatch '\\DeceiveInc\\Binaries\\Win64$') {
    throw "Launcher must be installed in the authorized Win64 directory: $allowed"
}
for ($cursor = Get-Item -LiteralPath $win64; $null -ne $cursor; $cursor = $cursor.Parent) {
    if ($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point forbidden: $($cursor.FullName)" }
}
$exe = Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Missing dedicated server: $exe" }
if ((Get-Item -LiteralPath $exe).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Executable cannot be a reparse point.' }
$launch = [ordered]@{ executable = $exe; workingDirectory = $win64 }
$selectionPath = Join-Path $win64 'Briefcase\settings.json'
if (Test-Path -LiteralPath $selectionPath) {
    $file = Get-Item -LiteralPath $selectionPath
    if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $file.Length -gt 65536) { throw 'Invalid mod selection file.' }
    $selection = Get-Content -LiteralPath $selectionPath -Raw | ConvertFrom-Json
    if ($selection.enabledMods -isnot [Array]) { throw 'enabledMods must be an array.' }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($id in $selection.enabledMods) {
        if ($id -isnot [string] -or $id -cnotmatch '^[a-z0-9]+([.-][a-z0-9]+)*$' -or -not $seen.Add($id)) {
            throw 'Invalid or duplicate enabled mod id.'
        }
    }
    $launch.enabledMods = @($selection.enabledMods)
} else {
    $launch.modSelection = 'All installed server mods (settings.json absent)'
}
if ($ValidateOnly) { return [pscustomobject]$launch }
$active = @(Get-CimInstance Win32_Process -Filter "Name = 'DeceiveIncServer-Win64-Shipping.exe'" | Where-Object { $_.ExecutablePath -ieq $exe })
if ($active.Count) { throw "This server is already running (PID $($active.ProcessId -join ', '))." }

# Serialize the entire update + launch, including concurrent administration restarts.
$updateRoot = Join-Path $win64 'Briefcase\Updates'
$updater = Join-Path $win64 'Briefcase\Updater\Updater.ps1'
$launchLock = $null
if (Test-Path -LiteralPath $updater) {
    for ($cursor = Get-Item -LiteralPath (Split-Path $updater); $null -ne $cursor; $cursor = $cursor.Parent) {
        if ($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Updater path cannot be redirected.' }
    }
    if ((Get-Item -LiteralPath $updater).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Updater cannot be redirected.' }
    . $updater
    Assert-UpdatePlainPath $updateRoot
    New-Item -ItemType Directory -Path $updateRoot -Force | Out-Null
    $lockPath = Join-Path $updateRoot 'launch.lock'
    Assert-UpdatePlainPath $lockPath
    $launchLock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
}
try {
if ($launchLock) {
    $active = @(Get-CimInstance Win32_Process -Filter "Name = 'DeceiveIncServer-Win64-Shipping.exe'" | Where-Object { $_.ExecutablePath -ieq $exe })
    if ($active.Count) { throw 'This server is already running.' }
    $updateConfig=Join-Path $win64 'Briefcase/updater.json'
    $updateExample=Join-Path $win64 'Briefcase/Updater/updater.example.json'
    if(-not(Test-Path -LiteralPath $updateConfig) -and (Test-Path -LiteralPath $updateExample)){
        Write-UpdateJson $updateConfig (Get-Content -LiteralPath $updateExample -Raw|ConvertFrom-Json)
    }
    $launch.update = Invoke-ServerUpdate $win64
    # A framework update may have replaced this module. Reload it before checking
    # mods so the new framework and its mod package contract take effect together.
    if ($launch.update -like 'installed *') { . $updater }
    $launch.modUpdates = Invoke-ModUpdates $win64
    Write-UpdateJson (Join-Path $updateRoot 'last-result.json') @{state=$launch.update;mods=$launch.modUpdates;checkedAt=(Get-Date).ToString('o')}
}

$gamePort = 50000
$queryPort = 50001
$ini = Join-Path $win64 '..\..\Saved\Config\WindowsServer\TripwireServer.ini'
if (Test-Path -LiteralPath $ini) {
    foreach ($line in Get-Content -LiteralPath $ini) {
        if ($line -match '^GamePort=(\d+)$') { $gamePort = [int]$Matches[1] }
        if ($line -match '^QueryPort=(\d+)$') { $queryPort = [int]$Matches[1] }
    }
}
if ($gamePort -lt 1 -or $gamePort -gt 65535 -or $queryPort -lt 1 -or $queryPort -gt 65535) { throw 'Invalid configured port.' }
$arguments = @('-unattended', '-NoSplash', '-NOCONSOLE', '-nullrhi', '-nosound', "-Port=$gamePort", "-QueryPort=$queryPort") + $ServerArguments
if ($arguments | Where-Object { $_ -match '[\r\n"]|\s' }) { throw 'Use individual arguments without spaces or quotes for this prototype.' }
$logs = Join-Path $win64 'Briefcase\Logs'
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$nativeLauncher=Join-Path $win64 'Briefcase.ServerLauncher.exe'
if(-not(Test-Path -LiteralPath $nativeLauncher -PathType Leaf)){throw 'Missing native server launcher.'}
$pidFile=Join-Path $logs "launcher-$stamp.pid"
$errorFile=Join-Path $logs "launcher-$stamp.stderr.log"
$helper=Start-Process -FilePath $nativeLauncher -WorkingDirectory $win64 -ArgumentList (@('--launch-child')+$arguments) -WindowStyle Hidden -PassThru -RedirectStandardOutput $pidFile -RedirectStandardError $errorFile
# Keep the native process handle alive: Windows PowerShell otherwise loses ExitCode.
$null=$helper.Handle
$helper.WaitForExit()
if($helper.ExitCode -ne 0){throw "Native launch failed: $(Get-Content -LiteralPath $errorFile -Raw)"}
$childId=(Get-Content -LiteralPath $pidFile -Raw).Trim()
if($childId -notmatch '^[1-9][0-9]*$'){throw 'Invalid native launcher response.'}
$process=Get-Process -Id ([int]$childId) -ErrorAction Stop
$launch.pid = $process.Id
$launch.arguments = $arguments
$launch.startedAt = (Get-Date).ToString('o')
$launch | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logs "launch-$stamp.json") -Encoding utf8
Start-Sleep -Seconds 3
$process.Refresh()
if ($process.HasExited) { throw "Server exited during startup, code $($process.ExitCode). Check the game logs." }
[pscustomobject]$launch

} finally { if ($launchLock) { $launchLock.Dispose() } }
