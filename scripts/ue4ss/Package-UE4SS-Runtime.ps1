[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\..\build-ue4ss-ninja'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\..\dist\UE4SS-Runtime')
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$build = [IO.Path]::GetFullPath($BuildDirectory)
$output = [IO.Path]::GetFullPath($OutputDirectory)
$allowed = [IO.Path]::GetFullPath((Join-Path $root 'dist\UE4SS-Runtime'))
if (-not $build.StartsWith($root + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe UE4SS build path.' }
if ($output -ine $allowed) { throw 'Unsafe UE4SS package path.' }

$runtime = Join-Path $build 'Output\Game__Shipping__Win64\UE4SS\bin\UE4SS.dll'
if (-not (Test-Path -LiteralPath $runtime -PathType Leaf)) { throw 'UE4SS.dll was not built.' }
$sources = @{
    'UE4SS.txt' = Join-Path $root 'third_party\RE-UE4SS\LICENSE'
    'DearImGui.txt' = Join-Path $build '_deps\imgui-src\LICENSE.txt'
    'ImGuiColorTextEdit.txt' = Join-Path $build '_deps\imguitextedit-src\LICENSE'
    'PolyHook2.txt' = Join-Path $build '_deps\polyhook2-src\LICENSE'
    'nlohmann-json.txt' = Join-Path $build '_deps\nlohmann_json-src\LICENSE.MIT'
    'Zydis.txt' = Join-Path $build '_deps\zydis-src\LICENSE'
    'Tracy.txt' = Join-Path $build '_deps\tracy-src\LICENSE'
    'glaze.txt' = Join-Path $build '_deps\glaze-src\LICENSE'
}
foreach ($source in $sources.Values) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing UE4SS license: $source" }
}

if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -Recurse -Force }
$ue4ss = Join-Path $output 'ue4ss'
$mods = Join-Path $ue4ss 'Mods'
$licenses = Join-Path $ue4ss 'Licenses'
foreach ($directory in @($mods, $licenses)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
Copy-Item -LiteralPath $runtime -Destination (Join-Path $ue4ss 'UE4SS.dll')
Copy-Item -LiteralPath (Join-Path $root 'resources\UE4SS\Server\UE4SS-settings.ini') -Destination $ue4ss
[IO.File]::WriteAllText((Join-Path $mods 'mods.txt'), '', [Text.UTF8Encoding]::new($false))
foreach ($entry in $sources.GetEnumerator()) {
    Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $licenses $entry.Key)
}
if (@(Get-ChildItem -LiteralPath $output -Recurse -File | Where-Object Extension -EQ '.pdb').Count) {
    throw 'UE4SS runtime package contains debug symbols.'
}
if (@(Get-ChildItem -LiteralPath $mods -Directory).Count) {
    throw 'Framework package must not contain mods.'
}
Write-Output "UE4SS runtime prepared: $output"
