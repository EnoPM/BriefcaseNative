[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidatePattern('^[a-f0-9]{32}$')][string]$RequestId,
      [Parameter(Mandatory=$true)][int]$ParentProcessId)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$win64 = Split-Path $root
. (Join-Path $PSScriptRoot 'Updater.ps1')
$resultPath = Resolve-UpdatePath $win64 'Briefcase/Admin/restart-result.json'
try {
    # The native restart helper has already checked/stopped the exact server. Wait for it to
    # release its own EXE before the updater can replace all framework binaries.
    $parent = Get-Process -Id $ParentProcessId -ErrorAction SilentlyContinue
    if ($parent) {
        if ($parent.Path -ine (Join-Path $root 'Tools/Briefcase.ServerRestart.exe')) { throw 'Unexpected restart helper.' }
        if (-not $parent.WaitForExit(30000)) { throw 'Restart helper did not exit.' }
    }
    $ticket = Get-Content -LiteralPath (Resolve-UpdatePath $win64 'Briefcase/Admin/restart-request.json') -Raw | ConvertFrom-Json
    if ($ticket.requestId -cne $RequestId) { throw 'Restart ticket changed.' }
    $arguments = @($ticket.arguments | Where-Object { $_ -notmatch '^-([Pp]ort|[Qq]uery[Pp]ort)=' -and $_ -notin @('-unattended','-NoSplash') })
    $run = & (Join-Path $win64 'StartBriefcaseNativeServer.ps1') -ServerArguments $arguments
    Write-UpdateJson $resultPath @{requestId=$RequestId;previousPid=$ticket.pid;pid=$run.pid;workingDirectory=$win64;state='started'}
} catch {
    Write-UpdateJson $resultPath @{requestId=$RequestId;state='error';message=$_.Exception.Message}
    exit 1
}
