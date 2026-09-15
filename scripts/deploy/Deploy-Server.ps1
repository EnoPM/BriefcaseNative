[CmdletBinding()]
param([string]$ServerWin64 = '', [switch]$PlanOnly,
      [ValidatePattern('^[a-z0-9]+([.-][a-z0-9]+)*$')][string]$ModId,
      [switch]$IncludeRuntime, [switch]$ActivateOnly, [switch]$RuntimeOnly, [string]$PackagePath='')
. (Join-Path $PSScriptRoot 'Common.ps1')
$paths = Get-DeploymentPaths -ServerWin64 $ServerWin64
$package = if($PackagePath){[IO.Path]::GetFullPath($PackagePath)}else{Join-Path $paths.Project 'dist\Win64'}
if($RuntimeOnly -and ($ModId -or $ActivateOnly)){throw 'RuntimeOnly cannot select or activate mods.'}
$required = @()
if (-not $ModId -or $IncludeRuntime) { $required += @('Briefcase.ServerLauncher.exe', 'Briefcase\Runtime\Briefcase.ServerBootstrap.dll', 'Briefcase\Runtime\Briefcase.NativeHost.dll', 'StartBriefcaseNativeServer.ps1') }
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $relative) -PathType Leaf)) { throw "Missing package file: $relative. Build first." }
}
if ($ActivateOnly -and -not $ModId) { throw 'ActivateOnly requires a single ModId.' }
if ($ModId) {
    $modManifest = Join-Path $package "Briefcase\Mods\$ModId\briefcase.mod.json"
    if (-not (Test-Path -LiteralPath $modManifest -PathType Leaf)) { throw "Missing mod package: $ModId" }
    Assert-NoReparsePoint -Path $modManifest
    $manifest = Get-Content -LiteralPath $modManifest -Raw | ConvertFrom-Json
    if ($manifest.id -cne $ModId -or $manifest.entry -notmatch '^[A-Za-z0-9_.-]+\.dll$' -or $manifest.entry.Contains('..')) { throw 'Invalid packaged manifest.' }
    if (-not (Test-Path -LiteralPath (Join-Path (Split-Path $modManifest) $manifest.entry) -PathType Leaf)) { throw 'Missing packaged mod DLL.' }
}
$files = @(Get-ChildItem -LiteralPath $package -File -Recurse)
$inventory = foreach ($file in $files) {
    Assert-NoReparsePoint -Path $file.FullName
    $relative = [IO.Path]::GetRelativePath($package, $file.FullName)
    $setupTool=$relative -cin @('Briefcase.ServerLauncher.exe','Briefcase\Tools\Briefcase.AdminSetup.exe','Briefcase\Tools\Briefcase.ServerRestart.exe')
    if($RuntimeOnly -and $relative.StartsWith('Briefcase\Mods\',[StringComparison]::OrdinalIgnoreCase)){continue}
    if (($file.Extension -notin @('.dll', '.json', '.txt', '.ps1', '.md') -and -not $setupTool) -or ($file.Extension -eq '.ps1' -and $relative -notin @('StartBriefcaseNativeServer.ps1','Briefcase\Updater\Updater.ps1','Briefcase\Updater\Restart-Server.ps1','Briefcase\Updater\Launch-Server.ps1'))) { throw "Forbidden runtime file: $relative" }
    if ($relative -ne 'Briefcase.ServerLauncher.exe' -and $relative -ne 'Package.json' -and $relative -ne 'version.dll' -and $relative -ne 'StartBriefcaseNativeServer.ps1' -and -not $relative.StartsWith('Briefcase\')) { throw "Unexpected package path: $relative" }
    if ($ModId) {
        $selected = $relative.StartsWith("Briefcase\Mods\$ModId\", [StringComparison]::OrdinalIgnoreCase)
        $support = -not $relative.StartsWith('Briefcase\Mods\', [StringComparison]::OrdinalIgnoreCase)
        if (-not $selected -and -not ($IncludeRuntime -and $support)) { continue }
    }
    $destination = [IO.Path]::GetFullPath((Join-Path $paths.Win64 $relative))
    if (-not $destination.StartsWith($paths.Win64 + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe destination: $destination" }
    # Reject junctions/symlinks anywhere in an existing destination subtree.
    $probe = $destination
    while ($probe.Length -ge $paths.Win64.Length) {
        if (Test-Path -LiteralPath $probe) {
            if ((Get-Item -LiteralPath $probe).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point forbidden: $probe" }
        }
        $probe = [IO.Path]::GetDirectoryName($probe)
    }
    if ($relative -match '^Briefcase\\Mods\\[^\\]+\\Data\\' -and (Test-Path -LiteralPath $destination)) {
        Write-Host "Preserved local mod data: $relative"
        continue
    }
    [pscustomobject]@{ relative = $relative; source = $file.FullName; destination = $destination; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash; replaces = (Test-Path -LiteralPath $destination) }
}
Assert-NoReparsePoint -Path (Join-Path $paths.Win64 'Briefcase\launch.json')
Assert-NoReparsePoint -Path (Join-Path $paths.Win64 'Briefcase\Backups')
$legacyProxy = Join-Path $paths.Win64 'version.dll'
Assert-NoReparsePoint -Path $legacyProxy
$removeProxy = (-not $ModId -or $IncludeRuntime) -and (Test-Path -LiteralPath $legacyProxy)
$settingsFile = Join-Path $paths.Win64 'Briefcase\settings.json'
Assert-NoReparsePoint -Path $settingsFile
$inventory | Format-Table relative, replaces, sha256
if ($ActivateOnly) { Write-Host "Enabled mod selection after deployment: $ModId only" }
if ($PlanOnly) { return }
$active = @(Get-CimInstance Win32_Process -Filter "Name = 'DeceiveIncServer-Win64-Shipping.exe'" | Where-Object { $_.ExecutablePath -ieq $paths.Executable })
if ($active.Count) { throw 'Stop this development server before deploying.' }
$backup = Join-Path $paths.Win64 ('Briefcase\Backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$inventory | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backup 'inventory.json') -Encoding utf8
foreach ($item in $inventory) {
    if ($item.replaces) {
        $saved = Join-Path $backup $item.relative
        New-Item -ItemType Directory -Path (Split-Path $saved) -Force | Out-Null
        Copy-Item -LiteralPath $item.destination -Destination $saved
    }
}
if ($removeProxy) {
    $savedProxy = Join-Path $backup 'version.dll'
    Copy-Item -LiteralPath $legacyProxy -Destination $savedProxy
    if ((Get-FileHash -LiteralPath $savedProxy).Hash -ne (Get-FileHash -LiteralPath $legacyProxy).Hash) { throw 'Proxy backup verification failed.' }
}
$launchConfig = Join-Path $paths.Win64 'Briefcase\launch.json'
if (Test-Path -LiteralPath $launchConfig) { Copy-Item -LiteralPath $launchConfig -Destination (Join-Path $backup 'previous-launch.json') }
foreach ($item in $inventory) {
    New-Item -ItemType Directory -Path (Split-Path $item.destination) -Force | Out-Null
    Copy-Item -LiteralPath $item.source -Destination $item.destination -Force
    if ((Get-FileHash -LiteralPath $item.destination -Algorithm SHA256).Hash -ne $item.sha256) { throw "Copy verification failed: $($item.relative)" }
}
if ($removeProxy) { Remove-Item -LiteralPath $legacyProxy }
if ($ActivateOnly) {
    if (Test-Path -LiteralPath $settingsFile) { Copy-Item -LiteralPath $settingsFile -Destination (Join-Path $backup 'previous-settings.json') }
    @{ enabledMods = @($ModId) } | ConvertTo-Json | Set-Content -LiteralPath $settingsFile -Encoding utf8
}
@{ serverWin64 = $paths.Win64 } | ConvertTo-Json | Set-Content -LiteralPath $launchConfig -Encoding utf8
Write-Host "Deployed to $($paths.Win64). Backup and inventory: $backup"
