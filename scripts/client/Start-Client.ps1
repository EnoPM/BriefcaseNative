[CmdletBinding()]param([string]$ClientWin64='',[string]$SettingsPath='',[string[]]$ClientArguments=@(),[switch]$ValidateOnly)
. (Join-Path $PSScriptRoot 'Common.ps1')
$paths=Get-ClientPaths $ClientWin64 $SettingsPath
$launcher=Join-Path $paths.Win64 'Briefcase.ClientLauncher.exe'
if(-not(Test-Path -LiteralPath $launcher -PathType Leaf)){throw "Missing native client launcher: $launcher"}
$arguments=@()
if($ValidateOnly){$arguments+='--validate-only'}
$arguments+=$ClientArguments
& $launcher @arguments
if($LASTEXITCODE){throw "Native client launcher failed with exit code $LASTEXITCODE"}
