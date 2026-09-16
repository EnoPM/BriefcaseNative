Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$fixture=Join-Path $project ('artifacts\deploy-tests-'+[guid]::NewGuid().ToString('N'))
$targetRoot=Join-Path $fixture 'server'
$target=Join-Path $targetRoot 'DeceiveInc\Binaries\Win64'
$package=Join-Path $fixture 'dist\Win64'
$scripts=Join-Path $fixture 'scripts\deploy'
function Put([string]$Path,[string]$Text){
    New-Item -ItemType Directory -Path (Split-Path $Path) -Force|Out-Null
    [IO.File]::WriteAllText($Path,$Text)
}
Put (Join-Path $target 'DeceiveIncServer-Win64-Shipping.exe') 'Fixture, never executed'
Put (Join-Path $fixture 'local.settings.json') (@{allowedServerRoot=$targetRoot;serverWin64=$target}|ConvertTo-Json)
New-Item -ItemType Directory -Path $scripts -Force|Out-Null
foreach($name in @('Common.ps1','Deploy-Server.ps1','Select-ServerMods.ps1')){
    Copy-Item -LiteralPath (Join-Path $project "scripts\deploy\$name") -Destination (Join-Path $scripts $name)
}
$entry='Briefcase\Mods\test.selected'
Put (Join-Path $package "$entry\briefcase.mod.json") '{"id":"test.selected","entry":"Selected.dll"}'
Put (Join-Path $package "$entry\Selected.dll") 'new selected'
Put (Join-Path $package "$entry\Data\config.json") '{"value":12}'
Put (Join-Path $target "$entry\Data\config.json") '{"value":7}'
Put (Join-Path $target 'Briefcase\settings.json') '{"enabledMods":["test.old"]}'
Put (Join-Path $package 'Briefcase\Mods\test.other\Other.dll') 'new other'
Put (Join-Path $target 'Briefcase\Mods\test.other\Other.dll') 'old other'
Put (Join-Path $package 'Briefcase.ServerLauncher.exe') 'new launcher'
Put (Join-Path $package 'Briefcase\Core\Briefcase.ServerBootstrap.dll') 'bootstrap'
Put (Join-Path $target 'version.dll') 'old proxy'
Put (Join-Path $target 'StartBriefcaseNativeServer.ps1') 'legacy launcher'
Put (Join-Path $target 'Briefcase\Updater\Updater.ps1') 'legacy updater'
Put (Join-Path $package 'Briefcase\Core\Tools\Briefcase.ServerUpdater.exe') 'fixture updater, never executed'
Put (Join-Path $package 'Briefcase\Core\Briefcase.NativeHost.dll') 'new runtime'
function Get-CimInstance {param($ClassName,$Filter) @()}
$checks=0
function Check([bool]$value){if(-not $value){throw "Deployment contract failed: $script:checks"};$script:checks++}
$deploy=Join-Path $scripts 'Deploy-Server.ps1'
& $deploy -ServerWin64 $target -ModId test.selected -ActivateOnly -PlanOnly *> (Join-Path $fixture 'plan.txt')
Check (-not(Test-Path -LiteralPath (Join-Path $target "$entry\Selected.dll")))
Check ((Get-Content -LiteralPath (Join-Path $target 'Briefcase\settings.json') -Raw).Contains('test.old'))
& $deploy -ServerWin64 $target -ModId test.selected -ActivateOnly *> (Join-Path $fixture 'deploy.txt')
Check ((Get-Content -LiteralPath (Join-Path $target "$entry\Selected.dll") -Raw) -eq 'new selected')
Check ((Get-Content -LiteralPath (Join-Path $target "$entry\Data\config.json") -Raw) -eq '{"value":7}')
Check ((Get-Content -LiteralPath (Join-Path $target 'Briefcase\Mods\test.other\Other.dll') -Raw) -eq 'old other')
Check ((Get-Content -LiteralPath (Join-Path $target 'version.dll') -Raw) -eq 'old proxy')
$selection=Get-Content -LiteralPath (Join-Path $target 'Briefcase\settings.json') -Raw|ConvertFrom-Json
Check ($selection.enabledMods.Count -eq 1 -and $selection.enabledMods[0] -eq 'test.selected')
& $deploy -ServerWin64 $target -ModId test.selected -IncludeRuntime *> (Join-Path $fixture 'runtime.txt')
Check (-not(Test-Path -LiteralPath (Join-Path $target 'version.dll')))
Check (-not(Test-Path -LiteralPath (Join-Path $target 'StartBriefcaseNativeServer.ps1')))
Check (-not(Test-Path -LiteralPath (Join-Path $target 'Briefcase\Updater\Updater.ps1')))
Check ((Get-Content -LiteralPath (Join-Path $target 'Briefcase.ServerLauncher.exe') -Raw) -eq 'new launcher')
Check ((Get-Content -LiteralPath (Join-Path $target "$entry\Data\config.json") -Raw) -eq '{"value":7}')
Check ((Get-Content -LiteralPath (Join-Path $target 'Briefcase\Mods\test.other\Other.dll') -Raw) -eq 'old other')
# A combined test must explicitly activate both installed mods without resetting Data.
Put (Join-Path $target 'Briefcase\Mods\test.other\briefcase.mod.json') '{"id":"test.other","entry":"Other.dll"}'
$select=Join-Path $scripts 'Select-ServerMods.ps1'
& $select -ModId @('test.selected','test.other') -PlanOnly *> (Join-Path $fixture 'selection-plan.txt')
Check (((Get-Content -LiteralPath (Join-Path $target 'Briefcase\settings.json') -Raw|ConvertFrom-Json).enabledMods.Count) -eq 1)
& $select -ModId @('test.selected','test.other') *> (Join-Path $fixture 'selection.txt')
$selection=Get-Content -LiteralPath (Join-Path $target 'Briefcase\settings.json') -Raw|ConvertFrom-Json
Check (($selection.enabledMods -join ',') -ceq 'test.selected,test.other')
Check ((Get-Content -LiteralPath (Join-Path $target "$entry\Data\config.json") -Raw) -eq '{"value":7}')
$previous=[IO.File]::ReadAllText((Join-Path $target 'Briefcase\settings.json'))
foreach($invalid in @(@('test.selected','test.selected'),@('test.missing'),@('../outside'))) {
    $failed=$false
    try { & $select -ModId $invalid *> (Join-Path $fixture 'invalid-selection.txt') } catch { $failed=$true }
    Check $failed
    Check ([IO.File]::ReadAllText((Join-Path $target 'Briefcase\settings.json')) -ceq $previous)
}
# Runtime-only update includes the one approved setup executable and preserves all mods/settings.
Put (Join-Path $package 'Briefcase\Core\Tools\Briefcase.AdminSetup.exe') 'fixture setup, never executed'
Put (Join-Path $package 'Briefcase\Core\Tools\Briefcase.ServerRestart.exe') 'fixture restart, never executed'
Put (Join-Path $target "$entry\Selected.dll") 'keep current mod'
& $deploy -ServerWin64 $target -RuntimeOnly *> (Join-Path $fixture 'runtime-only.txt')
Check ((Get-Content -LiteralPath (Join-Path $target "$entry\Selected.dll") -Raw) -eq 'keep current mod')
Check ((Get-Content -LiteralPath (Join-Path $target "$entry\Data\config.json") -Raw) -eq '{"value":7}')
Check ([IO.File]::ReadAllText((Join-Path $target 'Briefcase\settings.json')) -ceq $previous)
Check ((Get-Content -LiteralPath (Join-Path $target 'Briefcase\Core\Tools\Briefcase.AdminSetup.exe') -Raw) -eq 'fixture setup, never executed')
Check ((Get-Content -LiteralPath (Join-Path $target 'Briefcase\Core\Tools\Briefcase.ServerRestart.exe') -Raw) -eq 'fixture restart, never executed')
Check ((Get-Content -LiteralPath (Join-Path $target 'Briefcase\Core\Tools\Briefcase.ServerUpdater.exe') -Raw) -eq 'fixture updater, never executed')
Put (Join-Path $package 'Briefcase\Core\Tools\Unexpected.exe') 'not allowed'
$failed=$false
try { & $deploy -ServerWin64 $target -RuntimeOnly -PlanOnly *> (Join-Path $fixture 'invalid-exe.txt') } catch {$failed=$true}
Check $failed
Remove-Item -LiteralPath (Join-Path $package 'Briefcase\Core\Tools\Unexpected.exe') -Force
# Development backups are bounded while unrelated directories are untouched.
foreach($index in 1..8){
    $name=('20260101-0000{0:D2}-000' -f $index)
    Put (Join-Path $target "Briefcase\Backups\$name\marker.txt") "$index"
    (Get-Item -LiteralPath (Join-Path $target "Briefcase\Backups\$name")).LastWriteTimeUtc=[datetime]::UtcNow.AddMinutes($index)
}
Put (Join-Path $target 'Briefcase\Backups\manual-keep\marker.txt') 'keep'
& $deploy -ServerWin64 $target -RuntimeOnly *> (Join-Path $fixture 'retention.txt')
$retained=@(Get-ChildItem -LiteralPath (Join-Path $target 'Briefcase\Backups') -Directory|Where-Object {$_.Name -cmatch '^(selection-)?[0-9]{8}-[0-9]{6}-[0-9]{3}(-[a-f0-9]{32})?$'})
Check ($retained.Count -eq 5)
Check (Test-Path -LiteralPath (Join-Path $target 'Briefcase\Backups\manual-keep\marker.txt'))
Write-Host "PASS $checks deployment contracts; fixture files only."
