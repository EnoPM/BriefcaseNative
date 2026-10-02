[CmdletBinding()]
param([string]$ServerWin64 = '', [string[]]$ServerArguments = @(), [switch]$ValidateOnly)
. (Join-Path $PSScriptRoot 'Common.ps1')
$paths = Get-DeploymentPaths -ServerWin64 $ServerWin64
$proxy=Join-Path $paths.Win64 'version.dll'
if(-not(Test-Path -LiteralPath $proxy -PathType Leaf)){throw "Missing Briefcase version.dll proxy: $proxy"}
Assert-NoReparsePoint -Path $proxy
if($ValidateOnly){[pscustomobject]@{executable=$paths.Executable;workingDirectory=$paths.Win64;arguments=$ServerArguments};return}
Start-Process -FilePath $paths.Executable -WorkingDirectory $paths.Win64 -ArgumentList $ServerArguments -WindowStyle Hidden
