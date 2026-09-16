[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateCount(1,128)][string[]]$ModId,
    [string]$ServerWin64 = '',
    [switch]$PlanOnly
)
. (Join-Path $PSScriptRoot 'Common.ps1')
$paths = Get-DeploymentPaths -ServerWin64 $ServerWin64
$unique = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($id in $ModId) {
    if ($id -cnotmatch '^[a-z0-9]+([.-][a-z0-9]+)*$' -or -not $unique.Add($id)) {
        throw "Invalid or duplicate mod id: $id"
    }
    $directory = Join-Path $paths.Win64 "Briefcase\Mods\$id"
    $manifestPath = Join-Path $directory 'briefcase.mod.json'
    Assert-NoReparsePoint -Path $manifestPath
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "Mod is not deployed: $id" }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.id -cne $id -or $manifest.entry -cnotmatch '^[A-Za-z0-9_.-]+\.dll$' -or $manifest.entry.Contains('..')) {
        throw "Invalid deployed manifest: $id"
    }
    $dll = Join-Path $directory $manifest.entry
    Assert-NoReparsePoint -Path $dll
    if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) { throw "Missing mod DLL: $id" }
}
$settingsPath = Join-Path $paths.Win64 'Briefcase\settings.json'
Assert-NoReparsePoint -Path $settingsPath
$selection = [ordered]@{ enabledMods = @($ModId) }
Write-Host ("Selected mods: " + ($ModId -join ', '))
if ($PlanOnly) { return [pscustomobject]$selection }
$active = @(Get-CimInstance Win32_Process -Filter "Name = 'DeceiveIncServer-Win64-Shipping.exe'" |
    Where-Object { $_.ExecutablePath -ieq $paths.Executable })
if ($active.Count) { throw 'Stop this development server before changing its startup mod selection.' }
$backup = Join-Path $paths.Win64 ('Briefcase\Backups\selection-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [guid]::NewGuid().ToString('N'))
Assert-NoReparsePoint -Path $backup
New-Item -ItemType Directory -Path $backup -Force | Out-Null
if (Test-Path -LiteralPath $settingsPath) {
    Copy-Item -LiteralPath $settingsPath -Destination (Join-Path $backup 'previous-settings.json')
}
$temporary = Join-Path (Split-Path $settingsPath) ('settings-' + [guid]::NewGuid().ToString('N') + '.tmp')
Assert-NoReparsePoint -Path $temporary
$selection | ConvertTo-Json | Set-Content -LiteralPath $temporary -Encoding utf8
Move-Item -LiteralPath $temporary -Destination $settingsPath -Force
$written = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
if (($written.enabledMods -join ',') -cne ($ModId -join ',')) { throw 'Selection readback mismatch.' }
Remove-OldDeploymentBackups -ServerWin64 $paths.Win64 -Keep 5
Write-Host "Selection saved: $settingsPath. Mod Data files preserved."
[pscustomobject]$selection
