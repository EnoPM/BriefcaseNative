[CmdletBinding()]param()
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
& (Join-Path $project 'scripts\test\Test.ps1')
& (Join-Path $project 'tests\ClientDeployment.Contracts.ps1')
