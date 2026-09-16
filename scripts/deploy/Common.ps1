Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
function Assert-NoReparsePoint {
    param([Parameter(Mandatory)][string]$Path)
    $probe=[IO.Path]::GetFullPath($Path)
    while($probe){
        if(Test-Path -LiteralPath $probe){
            if((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Reparse point forbidden: $probe"}
        }
        $probe=[IO.Path]::GetDirectoryName($probe)
    }
}
function Get-DeploymentPaths {
    param([string]$ServerWin64='',[string]$SettingsPath='')
    $project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
    if(-not $SettingsPath){$SettingsPath=Join-Path $project 'local.settings.json'}
    $settings=Get-Content -LiteralPath $SettingsPath -Raw|ConvertFrom-Json
    if(-not $ServerWin64){$ServerWin64=$env:BRIEFCASE_SERVER_WIN64}
    if(-not $ServerWin64){$ServerWin64=$settings.serverWin64}
    if(-not $settings.allowedServerRoot -or -not $ServerWin64){throw 'Configure allowedServerRoot and serverWin64 in local.settings.json.'}
    $root=[IO.Path]::GetFullPath($settings.allowedServerRoot).TrimEnd('\')
    $target=[IO.Path]::GetFullPath($ServerWin64).TrimEnd('\')
    $expected=Join-Path $root 'DeceiveInc\Binaries\Win64'
    if($target -ine $expected){throw "Destination must be exactly $expected"}
    $exe=Join-Path $target 'DeceiveIncServer-Win64-Shipping.exe'
    if(-not(Test-Path -LiteralPath $exe -PathType Leaf)){throw "Missing server executable: $exe"}
    Assert-NoReparsePoint -Path $exe
    [pscustomobject]@{Project=$project;Win64=$target;Executable=$exe}
}
function Remove-OldDeploymentBackups {
    param([Parameter(Mandatory)][string]$ServerWin64,[ValidateRange(1,20)][int]$Keep=5)
    $win64=[IO.Path]::GetFullPath($ServerWin64).TrimEnd('\')
    $root=[IO.Path]::GetFullPath((Join-Path $win64 'Briefcase\Backups')).TrimEnd('\')
    if($root -ine (Join-Path $win64 'Briefcase\Backups') -or -not(Test-Path -LiteralPath $root)){return}
    Assert-NoReparsePoint -Path $root
    $candidates=@(Get-ChildItem -LiteralPath $root -Directory -Force|Where-Object {
        $_.Name -cmatch '^((selection-)?[0-9]{8}-[0-9]{6}-[0-9]{3}(-[a-f0-9]{32})?|updater-[a-f0-9]{32})$'
    }|Sort-Object LastWriteTimeUtc,Name -Descending)
    foreach($directory in @($candidates|Select-Object -Skip $Keep)){
        $resolved=[IO.Path]::GetFullPath($directory.FullName)
        if([IO.Path]::GetDirectoryName($resolved) -ine $root){throw "Backup escaped retention root: $resolved"}
        Assert-NoReparsePoint -Path $resolved
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
