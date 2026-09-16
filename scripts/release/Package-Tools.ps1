# Build and publication helpers only. No installed runtime sources this file.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

function Assert-PackagePlainPath([string]$Path) {
    $cursor=[IO.Path]::GetFullPath($Path)
    while($cursor){
        if(Test-Path -LiteralPath $cursor){
            if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Reparse point forbidden: $cursor"}
        }
        $cursor=[IO.Path]::GetDirectoryName($cursor)
    }
}
function Resolve-PackagePath([string]$Root,[string]$Relative) {
    if($Relative -cnotmatch '^[A-Za-z0-9_./-]+$'){throw 'Invalid package path.'}
    foreach($part in ($Relative -split '/')){
        if(-not $part -or $part -in @('.','..') -or $part.EndsWith('.') -or $part -match '^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(\.|$)'){
            throw 'Unsafe package path.'
        }
    }
    $base=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $path=[IO.Path]::GetFullPath((Join-Path $base $Relative))
    if(-not $path.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Path escaped package.'}
    Assert-PackagePlainPath $path
    return $path
}
function ConvertTo-PackageVersion([string]$Value) {
    if($Value -cnotmatch '^v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'){throw 'Only stable major.minor.patch versions are supported.'}
    return [version]$Value.TrimStart('v')
}
function Write-PackageJson([string]$Path,$Value) {
    Assert-PackagePlainPath $Path
    New-Item -ItemType Directory -Path (Split-Path $Path) -Force|Out-Null
    $temporary=$Path+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    try {
        [IO.File]::WriteAllText($temporary,($Value|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
        if(Test-Path -LiteralPath $Path){[IO.File]::Replace($temporary,$Path,($temporary+'.previous'));Remove-Item -LiteralPath ($temporary+'.previous') -Force}
        else{[IO.File]::Move($temporary,$Path)}
    } finally {if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary -Force}}
}
function Expand-PackageArchive([string]$Archive,[string]$Stage) {
    Assert-PackagePlainPath $Archive;Assert-PackagePlainPath $Stage
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        if($zip.Entries.Count -gt 4096){throw 'Too many archive entries.'}
        $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase);$total=0L
        foreach($entry in $zip.Entries){
            $name=$entry.FullName.TrimEnd([char[]]@('/','\'))
            $path=Resolve-PackagePath $Stage $name
            if(-not $seen.Add($path)){throw 'Duplicate archive path.'}
            if((($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000){throw 'Archive symlink forbidden.'}
            $total+=$entry.Length
            if($total -gt 1073741824 -or $entry.Length -gt 536870912){throw 'Archive exceeds size limit.'}
        }
        foreach($entry in $zip.Entries){
            $path=Resolve-PackagePath $Stage $entry.FullName.TrimEnd([char[]]@('/','\'))
            if($entry.FullName.EndsWith('/') -or $entry.FullName.EndsWith('\')){New-Item -ItemType Directory -Path $path -Force|Out-Null;continue}
            New-Item -ItemType Directory -Path (Split-Path $path) -Force|Out-Null
            $source=$entry.Open();$destination=[IO.File]::Open($path,[IO.FileMode]::CreateNew)
            try{$source.CopyTo($destination)}finally{$destination.Dispose();$source.Dispose()}
            if((Get-Item -LiteralPath $path).Length -ne $entry.Length){throw 'Truncated archive entry.'}
        }
    } finally {$zip.Dispose()}
}
function Test-FrameworkPackagePath([string]$Relative) {
    return ($Relative -cin @('Briefcase.ServerLauncher.exe','Package.json') -or
        $Relative -cmatch '^Briefcase/Core/(Briefcase\.(NativeHost|ServerBootstrap)\.dll|Tools/Briefcase\.(AdminSetup|ServerRestart|ServerUpdater)\.exe)$' -or
        $Relative -cmatch '^Briefcase/Core/Updater/(updater\.example\.json|build\.json)$' -or
        $Relative -cmatch '^Briefcase/Core/(Docs|Licenses|Localization)/[A-Za-z0-9_./-]+\.(json|txt|md)$')
}
function Read-FrameworkPackage([string]$Stage,[string]$Version,[string]$GameHash) {
    $expected="$(ConvertTo-PackageVersion $Version)"
    $manifestPath=Resolve-PackagePath $Stage 'Package.json'
    if((Get-Item -LiteralPath $manifestPath).Length -gt 2097152){throw 'Package manifest too large.'}
    $manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
    if($manifest.updateSchema -ne 1 -or $manifest.environment -cne 'server' -or $manifest.platform -cne 'windows-x64' -or
       $manifest.frameworkVersion -cne $expected -or $manifest.gameSha256 -ine $GameHash){throw 'Incompatible package identity.'}
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($file in $manifest.files){
        $path=Resolve-PackagePath $Stage $file.path
        if($file.path -ieq 'Package.json' -or -not(Test-FrameworkPackagePath $file.path) -or -not $seen.Add($path) -or
           $file.mode -notin @(420,493) -or $file.sha256 -cnotmatch '^[a-f0-9]{64}$' -or
           (Get-Item -LiteralPath $path).Length -ne $file.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $file.sha256){
            throw 'Invalid package inventory.'
        }
    }
    foreach($file in Get-ChildItem -LiteralPath $Stage -Recurse -File){if($file.FullName -ine $manifestPath -and -not $seen.Contains($file.FullName)){throw 'Unlisted package file.'}}
    foreach($required in @('Briefcase.ServerLauncher.exe','Briefcase/Core/Briefcase.ServerBootstrap.dll','Briefcase/Core/Briefcase.NativeHost.dll',
        'Briefcase/Core/Tools/Briefcase.AdminSetup.exe','Briefcase/Core/Tools/Briefcase.ServerRestart.exe','Briefcase/Core/Tools/Briefcase.ServerUpdater.exe','Briefcase/Core/Updater/build.json')){
        if(-not $seen.Contains((Resolve-PackagePath $Stage $required))){throw "Incomplete package: $required"}
    }
    if((Get-Content -LiteralPath (Resolve-PackagePath $Stage 'Briefcase/Core/Updater/build.json') -Raw|ConvertFrom-Json).frameworkVersion -cne $expected){throw 'Version marker mismatch.'}
    return $manifest
}
function Test-ClientPackagePath([string]$Relative) {
    return ($Relative -cin @('Briefcase.ClientLauncher.exe','version.dll','Briefcase/loader.json',
            'Briefcase/Core/Briefcase.NativeHost.dll','Briefcase/Core/Client/Briefcase.Client.Rendering.dll',
            'Briefcase/Mods/briefcase.native-overlay-sample/Briefcase.NativeOverlaySample.dll',
            'Briefcase/Mods/briefcase.native-overlay-sample/briefcase.mod.json') -or
        $Relative -cmatch '^Briefcase/Core/(Docs|Licenses|Localization)/[A-Za-z0-9_./-]+\.(json|txt|md)$')
}
function Read-ClientPackage([string]$Stage,[string]$Version,[string]$GameHash) {
    $expected="$(ConvertTo-PackageVersion $Version)"
    $manifestPath=Resolve-PackagePath $Stage 'Package.json'
    if((Get-Item -LiteralPath $manifestPath).Length -gt 2097152){throw 'Client package manifest too large.'}
    $manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
    if($manifest.updateSchema -ne 1 -or $manifest.environment -cne 'client' -or $manifest.platform -cne 'windows-x64' -or
       $manifest.frameworkVersion -cne $expected -or $manifest.gameSha256 -ine $GameHash){throw 'Incompatible client package identity.'}
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($file in $manifest.files){
        $path=Resolve-PackagePath $Stage $file.path
        if($file.path -ieq 'Package.json' -or -not(Test-ClientPackagePath $file.path) -or -not $seen.Add($path) -or
           $file.mode -notin @(420,493) -or $file.sha256 -cnotmatch '^[a-f0-9]{64}$' -or
           (Get-Item -LiteralPath $path).Length -ne $file.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $file.sha256){
            throw 'Invalid client package inventory.'
        }
    }
    foreach($file in Get-ChildItem -LiteralPath $Stage -Recurse -File){if($file.FullName -ine $manifestPath -and -not $seen.Contains($file.FullName)){throw 'Unlisted client package file.'}}
    foreach($required in @('Briefcase.ClientLauncher.exe','version.dll','Briefcase/loader.json','Briefcase/Core/Briefcase.NativeHost.dll',
        'Briefcase/Core/Client/Briefcase.Client.Rendering.dll','Briefcase/Mods/briefcase.native-overlay-sample/Briefcase.NativeOverlaySample.dll',
        'Briefcase/Mods/briefcase.native-overlay-sample/briefcase.mod.json')){
        if(-not $seen.Contains((Resolve-PackagePath $Stage $required))){throw "Incomplete client package: $required"}
    }
    return $manifest
}
