[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_.-]+$')][string]$RepositoryName,
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_.-]+$')][string]$InstallName,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\..\build-ue4ss-ninja'),
    [switch]$PreEntry
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$repository = [IO.Path]::GetFullPath((Join-Path $root "repositories\$RepositoryName"))
$build = [IO.Path]::GetFullPath($BuildDirectory)
if (-not $repository.StartsWith($root + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or
    -not $build.StartsWith($root + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe mod source or build path.' }
if (-not (Test-Path -LiteralPath (Join-Path $repository 'ue4ss\CMakeLists.txt'))) {
    throw 'UE4SS mod source is missing.'
}
$version = (Get-Content -LiteralPath (Join-Path $repository 'VERSION') -Raw).Trim()
if ($version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
    throw 'Invalid mod VERSION.'
}
$binary = Join-Path $build "$RepositoryName\main.dll"
$early = Join-Path $build "$RepositoryName\BriefcasePreEntry.dll"
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw 'Mod DLL was not built.' }
if ($PreEntry -and -not (Test-Path -LiteralPath $early -PathType Leaf)) {
    throw 'Required pre-entry DLL was not built.'
}
$dist = Join-Path $repository 'dist'
$stage = Join-Path $repository ('build\package-ue4ss-' + [guid]::NewGuid().ToString('N'))
$archive = Join-Path $dist "$RepositoryName-windows-x64-$version.zip"
foreach ($path in @($dist, $stage, $archive)) {
    $full = [IO.Path]::GetFullPath($path)
    if (-not $full.StartsWith($repository + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe mod package path: $path" }
}

try {
    $mod = Join-Path $stage "ue4ss\Mods\$InstallName"
    $dlls = Join-Path $mod 'dlls'
    New-Item -ItemType Directory -Path $dlls -Force | Out-Null
    New-Item -ItemType Directory -Path $dist -Force | Out-Null
    Copy-Item -LiteralPath $binary -Destination (Join-Path $dlls 'main.dll')
    if ($PreEntry) { Copy-Item -LiteralPath $early -Destination $mod }
    foreach ($relative in @('Data\config.json', 'README.md')) {
        $source = Join-Path $repository $relative
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            $target = Join-Path $mod $relative
            New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
            Copy-Item -LiteralPath $source -Destination $target
        }
    }
    $jsonLicense = Join-Path $build '_deps\nlohmann_json-src\LICENSE.MIT'
    if (Test-Path -LiteralPath $jsonLicense -PathType Leaf) {
        $licenses = Join-Path $mod 'Licenses'
        New-Item -ItemType Directory -Path $licenses -Force | Out-Null
        Copy-Item -LiteralPath $jsonLicense -Destination (Join-Path $licenses 'nlohmann-json.txt')
    }
    if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($stage, $archive)
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $entries = @($zip.Entries | Where-Object { -not [string]::IsNullOrEmpty($_.Name) } |
            ForEach-Object FullName)
        $required = @("ue4ss/Mods/$InstallName/dlls/main.dll")
        if ($PreEntry) { $required += "ue4ss/Mods/$InstallName/BriefcasePreEntry.dll" }
        foreach ($name in $required) {
            if ($name -cnotin $entries) { throw "Mod archive is missing $name" }
        }
        if (@($entries | Where-Object { $_ -match '(?i)\.(pdb|lib|obj|exe)$' }).Count) {
            throw 'Mod archive contains development binaries.'
        }
        if (@($entries | Where-Object { $_ -notlike "ue4ss/Mods/$InstallName/*" }).Count) {
            throw 'Mod archive contains files outside its own UE4SS directory.'
        }
    } finally { $zip.Dispose() }
    Write-Output $archive
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
