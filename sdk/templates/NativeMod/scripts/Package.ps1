[CmdletBinding()]param([Parameter(Mandatory=$true)][string]$Stage,[Parameter(Mandatory=$true)][string]$SdkPath)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$manifest=Get-Content -LiteralPath (Join-Path $project 'briefcase.mod.json') -Raw|ConvertFrom-Json
$config=Get-Content -LiteralPath (Join-Path $project 'mod-build.json') -Raw|ConvertFrom-Json
if($manifest.id -cnotmatch '^[a-z0-9.-]+$' -or $manifest.version -cnotmatch '^\d+\.\d+\.\d+$' -or $manifest.entry -cnotmatch '^[A-Za-z0-9_.-]+\.dll$'){throw 'Invalid mod manifest'}
if($manifest.environment -ne 'server' -or $manifest.minimumApi -ne 1){throw 'Unexpected environment or ABI'}
$mod=Join-Path $Stage "Briefcase/Mods/$($manifest.id)"
$licenses=Join-Path $mod 'Licenses'
New-Item -ItemType Directory -Path $licenses -Force|Out-Null
Copy-Item -LiteralPath (Join-Path $SdkPath 'Licenses/nlohmann-json.txt') -Destination $licenses
$expected=@("Briefcase/Mods/$($manifest.id)/$($manifest.entry)","Briefcase/Mods/$($manifest.id)/briefcase.mod.json",
 "Briefcase/Mods/$($manifest.id)/Data/config.json","Briefcase/Mods/$($manifest.id)/Licenses/nlohmann-json.txt")
$actual=@(Get-ChildItem -LiteralPath $Stage -Recurse -File|ForEach-Object {$_.FullName.Substring($Stage.Length+1).Replace('\','/')})
if(@(Compare-Object $expected $actual).Count){throw 'Unexpected files in mod package'}
$dist=Join-Path $project 'dist'
New-Item -ItemType Directory -Path $dist -Force|Out-Null
$name="$($config.repository)-windows-x64-$($manifest.version).zip"
$archive=Join-Path $dist $name
if(Test-Path -LiteralPath $archive){Remove-Item -LiteralPath $archive -Force}
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($Stage,$archive)
$hash=(Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($archive+'.sha256'),"$hash  $name"+[Environment]::NewLine)
Write-Output "Mod package verified: $archive"
