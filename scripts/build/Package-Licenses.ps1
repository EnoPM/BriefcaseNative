$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$destination=Join-Path $project 'dist\Win64\Briefcase\Licenses'
New-Item -ItemType Directory -Path $destination -Force|Out-Null
$additional=@{
    'AsmJit.txt'='build\_deps\polyhook2-src\asmjit\LICENSE.md'
    'AsmTK.txt'='build\_deps\polyhook2-src\asmtk\LICENSE.md'
    'Zycore.txt'='build\_deps\zydis-src\dependencies\zycore\LICENSE'
}
foreach($name in $additional.Keys){Copy-Item -LiteralPath (Join-Path $project $additional[$name]) -Destination (Join-Path $destination $name) -Force}
$metadata=& cargo metadata --locked --format-version 1 --manifest-path (Join-Path $project 'third_party\RE-UE4SS\deps\first\patternsleuth_bind\Cargo.toml')|ConvertFrom-Json
if($LASTEXITCODE){throw 'Cargo license inventory failed.'}
$records=foreach($package in $metadata.packages){
    [pscustomobject]@{name=$package.name;version=$package.version;license=$package.license;source=$package.source;repository=$package.repository}
    $folder=Split-Path $package.manifest_path
    $licenses=@(Get-ChildItem -LiteralPath $folder -File|Where-Object{$_.Name -match '^(LICENSE|LICENCE|COPYING|NOTICE)'})
    if($licenses.Count){
        $licenseDirectory=Join-Path $destination ('Rust\'+$package.name+'-'+$package.version)
        New-Item -ItemType Directory -Path $licenseDirectory -Force|Out-Null
        foreach($file in $licenses){Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $licenseDirectory ($file.Name+'.txt')) -Force}
    }
}
$records|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $destination 'RustDependencies.json') -Encoding utf8
@'
UE4SS and UEPseudo: MIT, https://github.com/UE4SS-RE/RE-UE4SS
patternsleuth: MIT OR Apache-2.0, https://github.com/trumank/patternsleuth
Pinned patternsleuth commit: 33e731e99f2a6bb7f65a8e95e89fd1c06ce9d1d2
The pinned patternsleuth source declares its license in Cargo.toml; no standalone license file is supplied there.
patternsleuth_bind is part of RE-UE4SS (MIT).
Zydis, Zycore, PolyHook2, nlohmann/json, AsmJit and AsmTK license texts are included.
Rust dependencies and their declared licenses are listed in RustDependencies.json.
License texts supplied by those crates are included under Rust/.
'@|Set-Content -LiteralPath (Join-Path $destination 'ThirdPartyNotices.txt') -Encoding utf8
