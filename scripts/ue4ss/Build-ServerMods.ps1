[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\..\build-ue4ss-ninja'),
    [switch]$Offline,
    [switch]$RuntimeOnly,
    [string[]]$ModTargets = @()
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$build = [IO.Path]::GetFullPath($BuildDirectory)
if (-not $build.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
    throw "BuildDirectory must remain under $root"
}

$ue4ssSource = Join-Path $root 'third_party\RE-UE4SS'
if (-not (Test-Path -LiteralPath $ue4ssSource)) {
    & (Join-Path $root 'scripts\build\Fetch-Dependencies.ps1')
} else {
    & (Join-Path $root 'scripts\build\Patch-UE4SS.ps1')
}
# CMake asks Git for the pinned UE4SS revision. CI and sandbox accounts may not
# own the checkout, so scope the trust exception to this child process only.
$gitConfigCount = if ($env:GIT_CONFIG_COUNT -match '^[0-9]+$') { [int]$env:GIT_CONFIG_COUNT } else { 0 }
Set-Item -LiteralPath "Env:GIT_CONFIG_KEY_$gitConfigCount" -Value 'safe.directory'
Set-Item -LiteralPath "Env:GIT_CONFIG_VALUE_$gitConfigCount" -Value $ue4ssSource.Replace('\','/')
$env:GIT_CONFIG_COUNT = [string]($gitConfigCount + 1)

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer (vswhere.exe) was not found.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with the C++ toolchain was not found.' }
$devShell = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'

# Import the compiler environment into this PowerShell process.
$environment = & $env:ComSpec /d /s /c "`"$devShell`" -arch=x64 -host_arch=x64 >nul && set"
foreach ($line in $environment) {
    $parts = $line -split '=', 2
    if ($parts.Count -eq 2) { Set-Item -LiteralPath "Env:$($parts[0])" -Value $parts[1] }
}
# Ninja consumes MSVC's English /showIncludes prefix as dependency metadata.
# Force that stable prefix so localized compiler output is not printed as
# hundreds of thousands of log lines on French development machines.
$env:VSLANG = '1033'

$modSwitch = if ($RuntimeOnly) { 'OFF' } else { 'ON' }
$configure = @(
    '-S', (Join-Path $root 'ue4ss'),
    '-B', $build,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Game__Shipping__Win64',
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
    '-DRust_CARGO_TARGET_LINK_NATIVE_LIBS=kernel32;ntdll;userenv;ws2_32;dbghelp;msvcrt',
    "-DBRIEFCASE_BUILD_LOCAL_MODS=$modSwitch"
)
if ($Offline) { $configure += '-DFETCHCONTENT_FULLY_DISCONNECTED=ON' }
& cmake @configure
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$targets=@('UE4SS','Briefcase.DeceiveInc.Contracts','Briefcase.NativeStamina.Sites','Briefcase.ServerBridge.UE4SS')
if ($RuntimeOnly -and $ModTargets.Count) { throw 'RuntimeOnly cannot build a mod target.' }
if (-not $RuntimeOnly) {
    if ($ModTargets.Count) {
        foreach ($target in $ModTargets) {
            if ($target -cnotmatch '^[A-Za-z0-9_.-]+$') { throw "Invalid mod target: $target" }
        }
        $targets += $ModTargets
    } else {
        $targets = @('all')
    }
}
$buildLog = Join-Path $build 'runtime-build.log'
& cmake --build $build --target @targets --parallel *> $buildLog
$buildExit = $LASTEXITCODE
Get-Content -LiteralPath $buildLog | Where-Object { $_ -notmatch 'inclusion du fichier|including file' } | Write-Host
exit $buildExit
