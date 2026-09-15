$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
& (Join-Path $project 'tests\Updater.Contracts.ps1')
& (Join-Path $project 'tests\RestartUpdater.Contracts.ps1')
& (Join-Path $project 'tests\Launcher.Contracts.ps1')
& (Join-Path $project 'tests\Deployment.Contracts.ps1')
$command=Get-Command ctest -ErrorAction SilentlyContinue
if($command){$ctest=$command.Source}else{
    $vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $ctest=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
}
& $ctest --test-dir "$project\build" -C Release --output-on-failure
if($LASTEXITCODE){throw 'Native contract tests failed.'}
