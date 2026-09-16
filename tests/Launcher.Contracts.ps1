Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $project 'scripts\deploy\Common.ps1')
$fixture=Join-Path $project ('artifacts\launcher-tests-'+[guid]::NewGuid().ToString('N'))
$win64=Join-Path $fixture 'DeceiveInc\Binaries\Win64'
New-Item -ItemType Directory -Path (Join-Path $win64 'Briefcase') -Force | Out-Null
$exe=Join-Path $win64 'DeceiveIncServer-Win64-Shipping.exe'
Set-Content -LiteralPath (Join-Path $win64 'Briefcase.ServerLauncher.exe') -Value 'Fixture'
Set-Content -LiteralPath $exe -Value 'Test fixture, never executed.'
@{serverWin64=$win64}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $win64 'Briefcase\launch.json')
$settingsPath=Join-Path $fixture 'settings.json'
@{allowedServerRoot=$fixture;serverWin64=$win64}|ConvertTo-Json|Set-Content -LiteralPath $settingsPath
$launcher=Join-Path $win64 'StartBriefcaseNativeServer.ps1'
Copy-Item -LiteralPath (Join-Path $project 'scripts\deploy\StartBriefcaseNativeServer.ps1') -Destination $launcher
$global:bcLauncherTestchecks=0
function Assert-True([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message};$global:bcLauncherTestchecks++}
function Assert-Throws([scriptblock]$Action,[string]$Message){$failed=$false;try{& $Action|Out-Null}catch{$failed=$true};Assert-True $failed $Message}
$global:bcLauncherTestcaptured=$null
$global:bcLauncherTestactive=@()
function Get-CimInstance {param($ClassName,$Filter) $global:bcLauncherTestactive}
function Start-Sleep {param($Seconds)}
function Start-Process {
    param($FilePath,$WorkingDirectory,$ArgumentList,$WindowStyle,[switch]$PassThru,[switch]$Wait,$RedirectStandardOutput,$RedirectStandardError)
    $global:bcLauncherTestcaptured=$PSBoundParameters
    $fake=[pscustomobject]@{Id=123456;HasExited=$false;ExitCode=0;Handle=1}
    '123456' | Set-Content -LiteralPath $RedirectStandardOutput
    $fake|Add-Member -MemberType ScriptMethod -Name WaitForExit -Value {}
    $fake|Add-Member -MemberType ScriptMethod -Name Refresh -Value {}
    $fake
}
function Get-Process {param($Id) $fake=[pscustomobject]@{Id=$Id;HasExited=$false};$fake|Add-Member -MemberType ScriptMethod -Name Refresh -Value {};$fake}
Push-Location $project
try {
    $plan=& $launcher -ValidateOnly
    Assert-True ($plan.workingDirectory -eq $win64) 'Preflight must use Win64.'
    Assert-True ($null -eq $global:bcLauncherTestcaptured) 'Preflight cannot launch.'
    $run=& $launcher
    Assert-True ($global:bcLauncherTestcaptured.FilePath -eq (Join-Path $win64 'Briefcase.ServerLauncher.exe')) 'Wrong executable.'
    Assert-True ($global:bcLauncherTestcaptured.WorkingDirectory -eq $win64) 'WorkingDirectory must be Win64.'
    Assert-True ($global:bcLauncherTestcaptured.WindowStyle -eq 'Hidden') 'Startup must be hidden.'
    Assert-True ($global:bcLauncherTestcaptured.ArgumentList -contains '-unattended') 'Unattended required.'
    Assert-True ($global:bcLauncherTestcaptured.ArgumentList -contains '-NOCONSOLE') 'No console required.'
    Assert-True ($global:bcLauncherTestcaptured.ArgumentList -contains '-nullrhi') 'No graphics required.'
    Assert-True ($run.pid -eq 123456) 'Record must contain PID.'
    $paths=Get-DeploymentPaths -SettingsPath $settingsPath -ServerWin64 $win64
    Assert-True ($paths.Win64 -eq $win64) 'Valid target rejected.'
    Assert-Throws {Get-DeploymentPaths -SettingsPath $settingsPath -ServerWin64 $fixture} 'Install root accepted.'
    Assert-Throws {Get-DeploymentPaths -SettingsPath $settingsPath -ServerWin64 ($fixture+'-other\DeceiveInc\Binaries\Win64')} 'Sibling accepted.'
    Assert-Throws {Get-DeploymentPaths -SettingsPath $settingsPath -ServerWin64 $project} 'Source directory accepted.'
    $global:bcLauncherTestactive=@([pscustomobject]@{ExecutablePath=$exe;ProcessId=1})
    Assert-Throws {& $launcher} 'Duplicate process accepted.'
    $global:bcLauncherTestactive=@([pscustomobject]@{ExecutablePath='X:\Unrelated\DeceiveIncServer-Win64-Shipping.exe';ProcessId=2})
    & $launcher|Out-Null
    Assert-True ($global:bcLauncherTestcaptured.WorkingDirectory -eq $win64) 'Unrelated process changed the target.'
    Assert-Throws {& $launcher -ServerArguments @('arg with spaces')} 'Unsupported quoting accepted.'
    $modSelection=Join-Path $win64 'Briefcase\settings.json'
    '{"enabledMods":["test.cap","test.timer"]}'|Set-Content -LiteralPath $modSelection
    $plan=& $launcher -ValidateOnly
    Assert-True (($plan.enabledMods -join ',') -ceq 'test.cap,test.timer') 'Preflight must expose the actual selection.'
    $run=& $launcher
    Assert-True (($run.enabledMods -join ',') -ceq 'test.cap,test.timer') 'Launch result lost selected mods.'
    $record=Get-ChildItem -LiteralPath (Join-Path $win64 'Briefcase\Logs') -Filter 'launch-*.json'|Sort-Object Name|Select-Object -Last 1
    $trace=Get-Content -LiteralPath $record.FullName -Raw|ConvertFrom-Json
    Assert-True (($trace.enabledMods -join ',') -ceq 'test.cap,test.timer') 'Launch trace lost selected mods.'
    '{"enabledMods":["test.diagnostic"]}'|Set-Content -LiteralPath $modSelection
    $plan=& $launcher -ValidateOnly
    Assert-True (($plan.enabledMods -join ',') -ceq 'test.diagnostic') 'Selection must not be cached across launches.'
    foreach($bad in @('{"enabledMods":"test.cap"}','{"enabledMods":["test.cap","test.cap"]}','{"enabledMods":["../outside"]}')) {
        $bad|Set-Content -LiteralPath $modSelection
        Assert-Throws {& $launcher -ValidateOnly} 'Malformed selection accepted.'
    }
    '{"enabledMods":[]}'|Set-Content -LiteralPath $modSelection
    $updaterDir=Join-Path $win64 'Briefcase\Updater'
    New-Item -ItemType Directory -Path $updaterDir -Force|Out-Null
    Copy-Item -LiteralPath (Join-Path $project 'scripts\update\Updater.ps1') -Destination $updaterDir
    $run=& $launcher
    Assert-True ($run.update -eq 'failed-kept-installed') 'Missing fixture build metadata should retain the installed server.'
    $defaults=Get-Content -LiteralPath (Join-Path $win64 'Briefcase/updater.json') -Raw|ConvertFrom-Json
    Assert-True ($defaults.enabled -and $defaults.repository -ceq 'EnoPM/BriefcaseNative') 'First launch did not configure the official updater.'
    Assert-True ($global:bcLauncherTestcaptured.WorkingDirectory -eq $win64) 'Updater changed working directory.'
    $lock=[IO.File]::Open((Join-Path $win64 'Briefcase\Updates\launch.lock'),[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try { Assert-Throws {& $launcher} 'Concurrent launch lock ignored.' } finally {$lock.Dispose()}
    $run=& $launcher
    Assert-True ($run.pid -eq 123456) 'Lock not released after startup.'
    Copy-Item -LiteralPath (Join-Path $project 'scripts/update/updater.example.json') -Destination $updaterDir
    $example=Get-Content -LiteralPath (Join-Path $updaterDir 'updater.example.json') -Raw|ConvertFrom-Json
    $example.enabled=$false
    $example|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $updaterDir 'updater.example.json')
    $run=& $launcher
    Assert-True ($run.update -eq 'failed-kept-installed') 'A changed example must not overwrite existing user settings.'
    Remove-Item -LiteralPath (Join-Path $win64 'Briefcase/updater.json')
    $run=& $launcher
    Assert-True ($run.update -eq 'disabled') 'First launch must initialize update configuration from the package.'
    Assert-True (Test-Path -LiteralPath (Join-Path $win64 'Briefcase/updater.json')) 'Update configuration not persisted.'
    @{serverWin64=$project}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $win64 'Briefcase\launch.json')
    Assert-Throws {& $launcher -ValidateOnly} 'Relocated launcher accepted.'
} finally {Pop-Location}
Write-Host "PASS $global:bcLauncherTestchecks launcher contracts; no process started."
