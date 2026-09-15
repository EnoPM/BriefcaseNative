[CmdletBinding()]
param([switch]$ConfigureOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'Visual Studio C++ tools are required.'}
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$cmake=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$cargoBin=Join-Path $env:USERPROFILE '.cargo\bin'
if(Test-Path -LiteralPath $cargoBin){$env:PATH=$cargoBin+';'+$env:PATH}
& $cmake -S $project -B "$project\build" -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'
if($LASTEXITCODE){throw 'CMake configuration failed.'}
if($ConfigureOnly){return}
& $cmake --build "$project\build" --config Release --parallel 4 -- -k 10
if($LASTEXITCODE){throw 'Release x64 build failed.'}
# Recreate generated framework staging; never retain old bundled mods.
$staging=[IO.Path]::GetFullPath((Join-Path $project 'dist/Win64'))
if($staging -ine [IO.Path]::GetFullPath("$project/dist/Win64")){throw 'Unexpected staging directory'}
. (Join-Path $project 'scripts/deploy/Common.ps1')
Assert-NoReparsePoint $staging
if(Test-Path -LiteralPath $staging){
 if(@(Get-ChildItem -LiteralPath $staging -Recurse -Force|Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count){throw 'Reparse point in staging'}
 Remove-Item -LiteralPath $staging -Recurse -Force
}
& $cmake --install "$project\build" --config Release --component Briefcase --prefix "$project\dist\Win64"
if($LASTEXITCODE){throw 'Packaging failed.'}
& (Join-Path $PSScriptRoot 'Package-Licenses.ps1')
