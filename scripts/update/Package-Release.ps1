[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $project 'scripts/release/Package-Tools.ps1')
$package = Join-Path $project 'dist/Server'
$inventory = Get-Content -LiteralPath (Join-Path $package 'Package.json') -Raw | ConvertFrom-Json
$version = ConvertTo-PackageVersion $inventory.frameworkVersion
$null = Read-FrameworkPackage $package "$version" $inventory.gameSha256
$output = Join-Path $project 'dist/Releases'
Assert-PackagePlainPath $output
New-Item -ItemType Directory -Path $output -Force | Out-Null
$archive = Join-Path $output "BriefcaseNative-Server-windows-x64-$version.zip"
Assert-PackagePlainPath $archive
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($package, $archive, [IO.Compression.CompressionLevel]::Optimal, $false)
Write-Output "Release asset prepared locally: $archive"
$client = Join-Path $project 'dist/Client'
$clientInventory = Get-Content -LiteralPath (Join-Path $client 'Package.json') -Raw | ConvertFrom-Json
$clientHash = (Get-Content -LiteralPath (Join-Path $project 'runtime/Briefcase.NativeHost/ClientBuild.json') -Raw | ConvertFrom-Json).sha256
$null = Read-ClientPackage $client "$version" $clientHash
$clientArchive = Join-Path $output "BriefcaseNative-Client-windows-x64-$version.zip"
Assert-PackagePlainPath $clientArchive
if (Test-Path -LiteralPath $clientArchive) { Remove-Item -LiteralPath $clientArchive -Force }
[IO.Compression.ZipFile]::CreateFromDirectory($client, $clientArchive, [IO.Compression.CompressionLevel]::Optimal, $false)
Write-Output "Client release asset prepared locally: $clientArchive"
