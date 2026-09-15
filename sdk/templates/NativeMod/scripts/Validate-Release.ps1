[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][ValidatePattern('^[a-f0-9]{40}$')][string]$Commit
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if($Version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'){
    throw 'Release version must be MAJOR.MINOR.PATCH without a v prefix or prerelease suffix.'
}
$cmake=Get-Content -LiteralPath (Join-Path $project 'CMakeLists.txt') -Raw
if($cmake -notmatch 'project\([A-Za-z0-9_.-]+ VERSION (\d+\.\d+\.\d+)'){throw 'Missing CMake project version.'}
if($Matches[1] -cne $Version){throw "Version $Version does not match CMake version $($Matches[1])."}
$head=git -C $project rev-parse HEAD
if($LASTEXITCODE -or $head -cne $Commit){throw 'Checkout does not match the requested commit.'}
$tagCommit=git -C $project rev-parse --verify --quiet "refs/tags/v${Version}^{commit}"
$code=$LASTEXITCODE
if($code -notin @(0,1)){throw 'Cannot inspect release tag.'}
if($code -eq 0 -and $tagCommit -cne $Commit){throw "Tag v$Version already points to another commit."}
$global:LASTEXITCODE=0
Write-Output "Release v$Version from $Commit validated."

$manifest=Get-Content -LiteralPath (Join-Path $project 'briefcase.mod.json') -Raw|ConvertFrom-Json
if($manifest.version -cne $Version){throw 'Manifest version differs from release version'}
