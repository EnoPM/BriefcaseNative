[CmdletBinding()]param([string]$ClientWin64='',[string]$SettingsPath='')
. (Join-Path $PSScriptRoot 'Common.ps1')
$paths=Get-ClientPaths $ClientWin64 $SettingsPath
$package=Join-Path $paths.Project 'dist\Client'
& (Join-Path $PSScriptRoot 'Assert-Package.ps1')
if(@(Get-ClientProcesses $paths).Count){throw 'Close this client copy with Stop-Client.ps1 before deploying.'}
$hash=(Get-FileHash -LiteralPath $paths.Executable).Hash.ToLowerInvariant()
$identity=Get-Content -LiteralPath (Join-Path $paths.Project 'runtime\Briefcase.NativeHost\ClientBuild.json') -Raw|ConvertFrom-Json
if($hash -ne $identity.sha256){throw 'Unsupported client executable; deployment aborted.'}
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$backup=Assert-ClientDescendant (Join-Path $paths.Root "BriefcaseDeploymentBackups\$stamp") $paths.Root
$files=@(Get-ChildItem -LiteralPath $package -File -Recurse|Where-Object {$_.Name -ne 'Package.json'})
# Resolve and check every destination before replacing any file.
foreach($file in $files) {
    $relative=$file.FullName.Substring($package.Length+1)
    $null=Assert-ClientDescendant (Join-Path $paths.Win64 $relative) $paths.Root
}
foreach($file in $files) {
    $relative=$file.FullName.Substring($package.Length+1)
    $target=Assert-ClientDescendant (Join-Path $paths.Win64 $relative) $paths.Root
    if($relative -eq 'Briefcase\loader.json' -and (Test-Path -LiteralPath $target)){continue}
    if((Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $target).Hash -eq (Get-FileHash -LiteralPath $file.FullName).Hash){continue}
    if(Test-Path -LiteralPath $target) {
        $saved=Assert-ClientDescendant (Join-Path $backup $relative) $paths.Root
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($saved)) -Force|Out-Null
        Copy-Item -LiteralPath $target -Destination $saved
    }
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force|Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $target -Force
    if((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash){throw "Deployment verification failed: $relative"}
}
$launch=Assert-ClientDescendant (Join-Path $paths.Win64 'Briefcase\launch.json') $paths.Root
[ordered]@{allowedClientRoot=$paths.Root;clientWin64=$paths.Win64;executableSha256=$hash}|
    ConvertTo-Json|Set-Content -LiteralPath $launch -Encoding utf8
$logs=Assert-ClientDescendant (Join-Path $paths.Win64 'Briefcase\Logs') $paths.Root
New-Item -ItemType Directory -Path $logs -Force|Out-Null
$inventory=Assert-ClientDescendant (Join-Path $paths.Win64 'Briefcase\InstalledPackage.json') $paths.Root
Copy-Item -LiteralPath (Join-Path $package 'Package.json') -Destination $inventory -Force
& (Join-Path $paths.Win64 'StartBriefcaseNativeClient.ps1') -ValidateOnly
