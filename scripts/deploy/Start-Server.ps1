[CmdletBinding()]
param([string]$ServerWin64 = '', [string[]]$ServerArguments = @(), [switch]$ValidateOnly)
. (Join-Path $PSScriptRoot 'Common.ps1')
$paths = Get-DeploymentPaths -ServerWin64 $ServerWin64
& (Join-Path $paths.Win64 'StartBriefcaseNativeServer.ps1') -ServerArguments $ServerArguments -ValidateOnly:$ValidateOnly
