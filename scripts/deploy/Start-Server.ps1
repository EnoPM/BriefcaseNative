[CmdletBinding()]
param([string]$ServerWin64 = '', [string[]]$ServerArguments = @(), [switch]$ValidateOnly)
. (Join-Path $PSScriptRoot 'Common.ps1')
$paths = Get-DeploymentPaths -ServerWin64 $ServerWin64
$launcher=Join-Path $paths.Win64 'Briefcase.ServerLauncher.exe'
if(-not(Test-Path -LiteralPath $launcher -PathType Leaf)){throw "Missing native launcher: $launcher"}
Assert-NoReparsePoint -Path $launcher
if($ValidateOnly){[pscustomobject]@{executable=$launcher;workingDirectory=$paths.Win64;arguments=$ServerArguments};return}
Start-Process -FilePath $launcher -WorkingDirectory $paths.Win64 -ArgumentList $ServerArguments -WindowStyle Hidden
