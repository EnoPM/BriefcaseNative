[CmdletBinding()]
param([Parameter(Mandatory=$true)][int]$LauncherProcessId,
      [Parameter(ValueFromRemainingArguments=$true)][string[]]$ServerArguments=@())
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$win64=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
try {
    $parent=Get-Process -Id $LauncherProcessId -ErrorAction SilentlyContinue
    if($parent -and -not $parent.WaitForExit(15000)){throw 'Launcher did not exit before update.'}
    & (Join-Path $win64 'StartBriefcaseNativeServer.ps1') -ServerArguments $ServerArguments | Out-Null
} catch {
    $logs=Join-Path $win64 'Briefcase/Logs'
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    $_.Exception.Message | Add-Content -LiteralPath (Join-Path $logs 'launcher-error.log')
    exit 1
}
