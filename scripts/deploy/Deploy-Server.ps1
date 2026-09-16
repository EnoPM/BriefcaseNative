[CmdletBinding()]
param([string]$ServerWin64 = '', [switch]$PlanOnly,
      [ValidatePattern('^[a-z0-9]+([.-][a-z0-9]+)*$')][string]$ModId,
      [switch]$IncludeRuntime, [switch]$ActivateOnly, [switch]$RuntimeOnly, [string]$PackagePath='')
. (Join-Path $PSScriptRoot 'Common.ps1')
$paths = Get-DeploymentPaths -ServerWin64 $ServerWin64
$package = if($PackagePath){[IO.Path]::GetFullPath($PackagePath)}else{Join-Path $paths.Project 'dist\Win64'}
if($RuntimeOnly -and ($ModId -or $ActivateOnly)){throw 'RuntimeOnly cannot select or activate mods.'}
$required = @()
if (-not $ModId -or $IncludeRuntime) { $required += @('Briefcase.ServerLauncher.exe', 'Briefcase\Core\Briefcase.ServerBootstrap.dll', 'Briefcase\Core\Briefcase.NativeHost.dll', 'Briefcase\Core\Tools\Briefcase.ServerUpdater.exe') }
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
    $setupTool=$relative -cin @('Briefcase.ServerLauncher.exe','Briefcase\Core\Tools\Briefcase.AdminSetup.exe','Briefcase\Core\Tools\Briefcase.ServerRestart.exe','Briefcase\Core\Tools\Briefcase.ServerUpdater.exe')
    if($RuntimeOnly -and $relative.StartsWith('Briefcase\Mods\',[StringComparison]::OrdinalIgnoreCase)){continue}
    if (($file.Extension -notin @('.dll', '.json', '.txt', '.md') -and -not $setupTool)) { throw "Forbidden runtime file: $relative" }
    if ($relative -ne 'Briefcase.ServerLauncher.exe' -and $relative -ne 'Package.json' -and $relative -ne 'version.dll' -and -not $relative.StartsWith('Briefcase\')) { throw "Unexpected package path: $relative" }
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
$retired=@()
$retiredDirectories=@()
$legacyLocalization=Join-Path $paths.Win64 'Briefcase\Localization'
if(-not $ModId -or $IncludeRuntime){
    foreach($name in @('StartBriefcaseNativeServer.ps1')){
        $candidate=Join-Path $paths.Win64 $name
        Assert-NoReparsePoint -Path $candidate
        if(Test-Path -LiteralPath $candidate -PathType Leaf){$retired+=@([pscustomobject]@{relative=$name;path=$candidate})}
    }
    foreach($name in @('Briefcase\Runtime','Briefcase\Tools','Briefcase\Updater','Briefcase\Docs','Briefcase\Licenses')){
        $candidate=[IO.Path]::GetFullPath((Join-Path $paths.Win64 $name))
        if(-not $candidate.StartsWith($paths.Win64+'\',[StringComparison]::OrdinalIgnoreCase)){throw "Unsafe retired directory: $candidate"}
        Assert-NoReparsePoint -Path $candidate
        if(Test-Path -LiteralPath $candidate -PathType Container){$retiredDirectories+=@([pscustomobject]@{relative=$name;path=$candidate})}
    }
    if(Test-Path -LiteralPath $legacyLocalization -PathType Container){
        Assert-NoReparsePoint -Path $legacyLocalization
        foreach($file in Get-ChildItem -LiteralPath $legacyLocalization -File -Filter '*.json'){
            Assert-NoReparsePoint -Path $file.FullName
            $retired+=@([pscustomobject]@{relative=('Briefcase\Localization\'+$file.Name);path=$file.FullName})
        }
    }
}
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
foreach($item in $retired){
    $saved=Join-Path $backup ('Retired\'+$item.relative)
    New-Item -ItemType Directory -Path (Split-Path $saved) -Force|Out-Null
    Copy-Item -LiteralPath $item.path -Destination $saved
}
foreach($item in $retiredDirectories){
    $saved=Join-Path $backup ('Retired\'+$item.relative)
    New-Item -ItemType Directory -Path (Split-Path $saved) -Force|Out-Null
    Copy-Item -LiteralPath $item.path -Destination $saved -Recurse
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
foreach($item in $retired){Remove-Item -LiteralPath $item.path -Force}
foreach($item in $retiredDirectories){Remove-Item -LiteralPath $item.path -Recurse -Force}
if((Test-Path -LiteralPath $legacyLocalization -PathType Container) -and
   -not @(Get-ChildItem -LiteralPath $legacyLocalization -Force).Count){Remove-Item -LiteralPath $legacyLocalization -Force}
if ($ActivateOnly) {
    if (Test-Path -LiteralPath $settingsFile) { Copy-Item -LiteralPath $settingsFile -Destination (Join-Path $backup 'previous-settings.json') }
    @{ enabledMods = @($ModId) } | ConvertTo-Json | Set-Content -LiteralPath $settingsFile -Encoding utf8
}
@{ serverWin64 = $paths.Win64 } | ConvertTo-Json | Set-Content -LiteralPath $launchConfig -Encoding utf8
Remove-OldDeploymentBackups -ServerWin64 $paths.Win64 -Keep 5
Write-Host "Deployed to $($paths.Win64). Backup and inventory: $backup"
