[CmdletBinding()]param([string]$ClientWin64='',[string]$SettingsPath='',[string[]]$ClientArguments=@(),[switch]$ValidateOnly)
. (Join-Path $PSScriptRoot 'Common.ps1')
$paths=Get-ClientPaths $ClientWin64 $SettingsPath
& (Join-Path $paths.Win64 'StartBriefcaseNativeClient.ps1') -ClientArguments $ClientArguments -ValidateOnly:$ValidateOnly
