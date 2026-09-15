[CmdletBinding()]
param([string]$ServerWin64='', [switch]$PlanOnly)
# Launcher, bootstrap and updater form one deployment unit.
& (Join-Path $PSScriptRoot '../deploy/Deploy-Server.ps1') -ServerWin64 $ServerWin64 -RuntimeOnly -PlanOnly:$PlanOnly -PackagePath ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../dist/Server')))
