Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
function Assert-PlainClientPath([string]$Path) {
    $probe=[IO.Path]::GetFullPath($Path)
    while($probe) {
        if(Test-Path -LiteralPath $probe) {
            if((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Reparse point forbidden: $probe"}
        }
        $probe=[IO.Path]::GetDirectoryName($probe)
    }
}
function Resolve-ClientLayout([string]$AllowedRoot,[string]$Win64) {
    $root=[IO.Path]::GetFullPath($AllowedRoot).TrimEnd('\')
    $target=[IO.Path]::GetFullPath($Win64).TrimEnd('\')
    if([IO.Path]::GetFileName($root) -ine 'DeceiveIncNativeClient'){throw 'The authorized root must be the dedicated DeceiveIncNativeClient test copy.'}
    if($target -ine (Join-Path $root 'DeceiveInc\Binaries\Win64')){throw 'Client target must be exactly the authorized Win64 directory.'}
    Assert-PlainClientPath $target
    [pscustomobject]@{Root=$root;Win64=$target;Executable=(Join-Path $target 'DeceiveInc-Win64-Shipping.exe')}
}
function Get-ClientPaths([string]$ClientWin64='',[string]$SettingsPath='') {
    $project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
    if(-not $SettingsPath){$SettingsPath=Join-Path $project 'local.settings.json'}
    $settings=Get-Content -LiteralPath $SettingsPath -Raw|ConvertFrom-Json
    if(-not $ClientWin64){$ClientWin64=$settings.clientWin64}
    $paths=Resolve-ClientLayout $settings.allowedClientRoot $ClientWin64
    if(-not(Test-Path -LiteralPath $paths.Executable -PathType Leaf)){throw "Client executable missing: $($paths.Executable)"}
    Assert-PlainClientPath $paths.Executable
    $paths|Add-Member NoteProperty Project $project -PassThru
}
function Assert-ClientDescendant([string]$Path,[string]$Root) {
    $full=[IO.Path]::GetFullPath($Path)
    $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\'
    if(-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw "Path outside client root: $full"}
    Assert-PlainClientPath $full
    $full
}
function Get-ClientProcesses($Paths) {
    @(Get-CimInstance Win32_Process -Filter "Name = 'DeceiveInc-Win64-Shipping.exe'"|
        Where-Object {$_.ExecutablePath -ieq $Paths.Executable})
}
function Remove-OldClientDeploymentBackups {
    param([Parameter(Mandatory)][string]$ClientRoot,[ValidateRange(1,20)][int]$Keep=5)
    $root=[IO.Path]::GetFullPath($ClientRoot).TrimEnd('\')
    $backups=Assert-ClientDescendant (Join-Path $root 'BriefcaseDeploymentBackups') $root
    if(-not(Test-Path -LiteralPath $backups)){return}
    $candidates=@(Get-ChildItem -LiteralPath $backups -Directory -Force|Where-Object {
        $_.Name -cmatch '^[0-9]{8}-[0-9]{6}-[0-9]{3}$'
    }|Sort-Object LastWriteTimeUtc,Name -Descending)
    foreach($directory in @($candidates|Select-Object -Skip $Keep)){
        $resolved=[IO.Path]::GetFullPath($directory.FullName)
        if([IO.Path]::GetDirectoryName($resolved) -ine $backups){throw "Backup escaped retention root: $resolved"}
        Assert-PlainClientPath $resolved
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
