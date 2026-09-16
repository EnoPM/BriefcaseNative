[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'Updater.ps1')
$package = Join-Path $project 'dist/Server'
$inventory = Get-Content -LiteralPath (Join-Path $package 'Package.json') -Raw | ConvertFrom-Json
$version = ConvertTo-ReleaseVersion $inventory.frameworkVersion
$null = Read-UpdatePackage $package "$version" $inventory.gameSha256
$output = Join-Path $project 'dist/Releases'
Assert-UpdatePlainPath $output
New-Item -ItemType Directory -Path $output -Force | Out-Null
$archive = Join-Path $output "BriefcaseNative-Server-windows-x64-$version.zip"
Assert-UpdatePlainPath $archive
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($package, $archive, [IO.Compression.CompressionLevel]::Optimal, $false)
Write-Output "Release asset prepared locally: $archive"
