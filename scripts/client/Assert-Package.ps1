[CmdletBinding()]param([string]$ClientPackage='',[string]$ServerPackage='')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if(-not $ClientPackage){$ClientPackage=Join-Path $project 'dist\Client'}
if(-not $ServerPackage){$ServerPackage=Join-Path $project 'dist\Server'}
$expectedClient=@('version.dll','Briefcase\Core\Briefcase.NativeHost.dll','Briefcase\Core\Client\Briefcase.Client.Rendering.dll',
    'Briefcase\Mods\briefcase.native-overlay-sample\Briefcase.NativeOverlaySample.dll')
$actualClient=@(Get-ChildItem -LiteralPath $ClientPackage -Filter *.dll -File -Recurse|
    ForEach-Object {$_.FullName.Substring($ClientPackage.TrimEnd('\').Length+1)})
if(@(Compare-Object $expectedClient $actualClient).Count){throw 'Unexpected client DLL inventory.'}
foreach($package in @($ClientPackage,$ServerPackage)){
    if(-not(Test-Path -LiteralPath $package)){throw "Missing package: $package"}
    $bad=@(Get-ChildItem -LiteralPath $package -Recurse -File|Where-Object {$_.Extension -match '^\.(pdb|lib|obj|exe)$'})
    $allowedSetup=Join-Path $ServerPackage 'Briefcase\Core\Tools\Briefcase.AdminSetup.exe'
    $allowedRestart=Join-Path $ServerPackage 'Briefcase\Core\Tools\Briefcase.ServerRestart.exe'
    $allowedUpdater=Join-Path $ServerPackage 'Briefcase\Core\Tools\Briefcase.ServerUpdater.exe'
    $allowedClientLauncher=Join-Path $ClientPackage 'Briefcase.ClientLauncher.exe'
    $bad=@($bad | Where-Object {$_.FullName -ine (Join-Path $ServerPackage 'Briefcase.ServerLauncher.exe') -and $_.FullName -ine $allowedClientLauncher -and $_.FullName -ine $allowedSetup -and $_.FullName -ine $allowedRestart -and $_.FullName -ine $allowedUpdater})
    if($bad.Count){throw "Development artifacts in package: $($bad.Name -join ', ')"}
    foreach($file in Get-ChildItem -LiteralPath $package -Filter briefcase.mod.json -Recurse -File) {
        $manifest=Get-Content -LiteralPath $file.FullName -Raw|ConvertFrom-Json
        if($package -eq $ClientPackage -and $manifest.environment -eq 'server'){throw 'Server-only mod in client package.'}
        if($package -eq $ServerPackage -and $manifest.environment -eq 'client'){throw 'Client-only mod in server package.'}
    }
    if(Test-Path -LiteralPath (Join-Path $package 'Package.json')) {
        $inventory=Get-Content -LiteralPath (Join-Path $package 'Package.json') -Raw|ConvertFrom-Json
        foreach($record in $inventory.files) {
            $file=Join-Path $package $record.path
            if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ine $record.sha256){throw "Package hash mismatch: $file"}
        }
    }
}
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$dumpbin=(Get-ChildItem -Path (Join-Path $vs 'VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe')|Sort-Object FullName -Descending|Select-Object -First 1).FullName
foreach($file in Get-ChildItem -LiteralPath $ServerPackage -File -Recurse | Where-Object {$_.Extension -in '.dll','.exe'}) {
    $imports=& $dumpbin /nologo /dependents $file.FullName
    if($LASTEXITCODE -or ($imports -match '(?i)d3d|dxgi|imgui|Client.Rendering')){throw "Graphics dependency or unreadable server binary: $($file.Name)"}
}
if(@(Get-ChildItem -LiteralPath $ServerPackage -Recurse|Where-Object {$_.Name -match '(?i)imgui|Client\.|OverlaySample|d3d|dxgi'}).Count){throw 'Client graphics component in server package.'}
foreach($required in @('Briefcase.ClientLauncher.exe','Briefcase\Core\Localization\fr.json','Briefcase\Core\Localization\en.json','Briefcase\Core\Licenses\MbedTLS.txt','Briefcase\Core\Licenses\DearImGui.txt','Briefcase\loader.json')){
    if(-not(Test-Path -LiteralPath (Join-Path $ClientPackage $required))){throw "Missing client package file: $required"}
}
$commonClient=(Get-FileHash -LiteralPath (Join-Path $ClientPackage 'Briefcase\Core\Briefcase.NativeHost.dll')).Hash
$commonServer=(Get-FileHash -LiteralPath (Join-Path $ServerPackage 'Briefcase\Core\Briefcase.NativeHost.dll')).Hash
if($commonClient -ne $commonServer){throw 'Client and server do not share the same runtime build.'}
if(-not(Test-Path -LiteralPath (Join-Path $ServerPackage 'Briefcase\Core\Tools\Briefcase.AdminSetup.exe'))){throw 'Missing server administration setup tool.'}
foreach($package in @($ClientPackage,$ServerPackage)) {
    if(@(Get-ChildItem -LiteralPath $package -Recurse -File | Where-Object {$_.Name -in @('server.json','clients.json','pairing.json','passwords.json','restart-request.json','ui-settings.json')}).Count){throw 'Local administration identity in user package.'}
}
Write-Output 'PASS client/server package inventories, common runtime, DLL dependencies, manifests, licenses and absence of PDBs.'

foreach($required in @('Briefcase\Core\Tools\Briefcase.ServerUpdater.exe','Briefcase\Core\Updater\build.json','Briefcase\Core\Updater\updater.example.json')) {
    if(-not(Test-Path -LiteralPath (Join-Path $ServerPackage $required))){throw "Missing server updater file: $required"}
}
if(Test-Path -LiteralPath (Join-Path $ServerPackage 'Briefcase\updater.json')){throw 'Local update repository configuration in user package.'}

if(Test-Path -LiteralPath (Join-Path $ServerPackage 'Briefcase/Mods')){throw 'Framework server package must not contain independent mods.'}

if(Test-Path -LiteralPath (Join-Path $ServerPackage 'version.dll')){throw 'Server package must use the launcher, not the client proxy.'}
foreach($required in @('Briefcase.ServerLauncher.exe','Briefcase/Core/Briefcase.ServerBootstrap.dll','Briefcase/Core/Licenses/Detours.txt','Briefcase/Core/Licenses/miniz.txt')){
 if(-not(Test-Path -LiteralPath (Join-Path $ServerPackage $required))){throw "Missing launcher file: $required"}
}
