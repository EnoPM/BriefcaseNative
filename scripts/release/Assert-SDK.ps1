[CmdletBinding()]param([Parameter(Mandatory=$true)][string]$Archive,[Parameter(Mandatory=$true)][string]$Version)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $project 'scripts/update/Updater.ps1')
$stage=Join-Path $project ('artifacts/check-sdk-'+[guid]::NewGuid().ToString('N'))
Expand-UpdateArchive $Archive $stage
$manifest=Get-Content -LiteralPath (Join-Path $stage 'SDK.json') -Raw|ConvertFrom-Json
if($manifest.schemaVersion -ne 1 -or $manifest.version -cne $Version -or $manifest.abiVersion -ne 1){throw 'Invalid SDK identity'}
$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach($record in $manifest.files){
 $path=Resolve-UpdatePath $stage $record.path
 if($record.path -notmatch '^(include/Briefcase/.+\.(h|hpp)|third_party/include/nlohmann/.+\.hpp|src/DeceiveInc/(Spy\.cpp|SpyContracts\.hpp)|Licenses/nlohmann-json\.txt|cmake/BriefcaseNativeSDKConfig(Version)?\.cmake)$' -or -not $seen.Add($path)){throw 'Unexpected SDK file'}
 if((Get-FileHash -LiteralPath $path).Hash -ine $record.sha256 -or (Get-Item -LiteralPath $path).Length -ne $record.bytes){throw 'SDK integrity failure'}
}
foreach($file in Get-ChildItem -LiteralPath $stage -Recurse -File){
 if($file.FullName -ine (Join-Path $stage 'SDK.json') -and -not $seen.Contains($file.FullName)){throw 'Unlisted SDK file'}
}
foreach($required in @('include/Briefcase/DeceiveInc/Spy.hpp','src/DeceiveInc/Spy.cpp','src/DeceiveInc/SpyContracts.hpp','include/Briefcase/ModApi.h','include/Briefcase/Unreal.hpp','third_party/include/nlohmann/json.hpp','cmake/BriefcaseNativeSDKConfig.cmake')){
 if(-not $seen.Contains((Resolve-UpdatePath $stage $required))){throw 'Incomplete SDK'}
}
Write-Output "SDK $Version validated."
