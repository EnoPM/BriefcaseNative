[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ServerRoot,
    [string]$ListenAddress='127.0.0.1',
    [ValidateRange(1,65535)][int]$Port=32189,
    [Parameter(Mandatory)][string]$PublicEndpoint,
    [string]$GeneratePasswordFile,
    [switch]$GeneratePassword
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$root=[IO.Path]::GetFullPath($ServerRoot).TrimEnd('\')
$win64=Join-Path $root 'DeceiveInc\Binaries\Win64'
$briefcase=Join-Path $win64 'Briefcase'
if(-not $briefcase.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Path outside server root.'}
for($path=$briefcase;$path;$path=[IO.Path]::GetDirectoryName($path)){
    if((Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Reparse point forbidden: $path"}
}
$setup=Join-Path $project 'build\Briefcase.AdminSetup.exe'
if(-not(Test-Path -LiteralPath $setup)){throw 'Compile the Release build first.'}
$arguments=@('--root',$briefcase,'--listen',$ListenAddress,'--port',"$Port",'--endpoint',$PublicEndpoint)
if($GeneratePasswordFile){$arguments+=@('--generate-password-file',[IO.Path]::GetFullPath($GeneratePasswordFile))}
if($GeneratePassword){$arguments+='--generate-password'}
& $setup @arguments
if($LASTEXITCODE){throw 'Administration setup failed.'}
