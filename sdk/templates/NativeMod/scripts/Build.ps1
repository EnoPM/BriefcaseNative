[CmdletBinding()]param([string]$SdkPath='')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sdk=& (Join-Path $PSScriptRoot 'Fetch-Sdk.ps1') -SdkPath $SdkPath
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'Visual Studio C++ tools required'}
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'|Out-Null
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctest=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
$ninja=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
& $cmake -S $project -B (Join-Path $project 'build') -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_PREFIX_PATH=$sdk"
if($LASTEXITCODE){throw 'Configure failed'}
& $cmake --build (Join-Path $project 'build') --parallel 4
if($LASTEXITCODE){throw 'Build failed'}
& $ctest --test-dir (Join-Path $project 'build') --output-on-failure
if($LASTEXITCODE){throw 'Mod contracts failed'}
$stage=Join-Path $project ('build/package-'+[guid]::NewGuid().ToString('N'))
& $cmake --install (Join-Path $project 'build') --prefix $stage
if($LASTEXITCODE){throw 'Install failed'}
& (Join-Path $PSScriptRoot 'Package.ps1') -Stage $stage -SdkPath $sdk
