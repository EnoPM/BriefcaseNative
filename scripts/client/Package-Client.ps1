[CmdletBinding()]param()
. (Join-Path $PSScriptRoot 'Common.ps1')
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
& (Join-Path $PSScriptRoot 'Test-Client.ps1')
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$dist=[IO.Path]::GetFullPath((Join-Path $project 'dist'))
foreach($kind in @('Client','Server')) {
    $destination=[IO.Path]::GetFullPath((Join-Path $dist $kind))
    if($destination -ine (Join-Path $project "dist\$kind")){throw 'Package cleanup target mismatch.'}
    Assert-PlainClientPath $destination
    if(Test-Path -LiteralPath $destination){Remove-Item -LiteralPath $destination -Recurse -Force}
    $components=if($kind -eq 'Client'){@('Client')}else{@('Briefcase')}
    foreach($component in $components) {
        & $cmake --install (Join-Path $project 'build') --config Release --component $component --prefix $destination
        if($LASTEXITCODE){throw "Packaging $kind/$component failed."}
    }
    $licenses=Join-Path $destination 'Briefcase\Licenses'
    New-Item -ItemType Directory -Path $licenses -Force|Out-Null
    Copy-Item -Path (Join-Path $project 'dist\Win64\Briefcase\Licenses\*') -Destination $licenses -Recurse -Force
    if($kind -eq 'Client'){
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'StartBriefcaseNativeClient.ps1') -Destination $destination
        '{"schemaVersion":1,"enabledMods":["briefcase.native-overlay-sample"]}'|
            Set-Content -LiteralPath (Join-Path $destination 'Briefcase\loader.json') -Encoding utf8
    }
    $frameworkVersion=& (Join-Path $project 'scripts/release/Read-Version.ps1') -ProjectRoot $project
    $records=@(Get-ChildItem -LiteralPath $destination -Recurse -File|ForEach-Object {
        [ordered]@{path=$_.FullName.Substring($destination.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant();bytes=$_.Length}
    })
    [ordered]@{updateSchema=1;platform='windows-x64';gameSha256=$(if($kind -eq 'Server'){'78afe1dbeecb09027c274def4f0ac855b447dc52ffe3cd9482c1be4341b0dae6'}else{''});frameworkVersion=$frameworkVersion;environment=$kind.ToLowerInvariant();configuration='Release x64';files=$records}|
        ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $destination 'Package.json') -Encoding utf8
}
& (Join-Path $PSScriptRoot 'Assert-Package.ps1')

& (Join-Path $project 'scripts\update\Package-Release.ps1')

& (Join-Path $project 'scripts/release/Package-SDK.ps1')
