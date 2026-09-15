$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $project 'scripts\client\Common.ps1')
$script:checks=0
function Check([bool]$Value,[string]$Name){$script:checks++;if(-not $Value){throw $Name}}
function Reject([scriptblock]$Action,[string]$Name){$rejected=$false;try{& $Action|Out-Null}catch{$rejected=$true};Check $rejected $Name}
$fixture=Join-Path $project ('artifacts\client-paths-'+[guid]::NewGuid().ToString('N'))
$root=Join-Path $fixture 'DeceiveIncNativeClient'
$win64=Join-Path $root 'DeceiveInc\Binaries\Win64'
New-Item -ItemType Directory -Path (Join-Path $win64 'Briefcase\Logs') -Force|Out-Null
$exe=Join-Path $win64 'DeceiveInc-Win64-Shipping.exe'
'fixture, never executed'|Set-Content -LiteralPath $exe
$layout=Resolve-ClientLayout $root $win64
Check ($layout.Win64 -eq $win64) 'Valid exact Win64'
Reject {Resolve-ClientLayout $root $root} 'Root cwd rejected'
Reject {Resolve-ClientLayout $root (Join-Path $root 'DeceiveInc\Binaries\Win64\..\Other')} 'Traversal rejected'
Reject {Resolve-ClientLayout (Join-Path $fixture 'DeceiveIncNativeServer') $win64} 'Server root rejected'
Reject {Assert-ClientDescendant (Join-Path $fixture 'outside') $root} 'Outside write rejected'
Reject {Assert-ClientDescendant ($root+'-suffix\file') $root} 'Prefix collision rejected'
$launcher=Join-Path $win64 'StartBriefcaseNativeClient.ps1'
Copy-Item -LiteralPath (Join-Path $project 'scripts\client\StartBriefcaseNativeClient.ps1') -Destination $launcher
[ordered]@{allowedClientRoot=$root;clientWin64=$win64;executableSha256=(Get-FileHash -LiteralPath $exe).Hash}|
    ConvertTo-Json|Set-Content -LiteralPath (Join-Path $win64 'Briefcase\launch.json')
Push-Location $project
try {$launch=& $launcher -ValidateOnly}finally{Pop-Location}
Check ($launch.workingDirectory -eq $win64 -and $launch.executable -eq $exe) 'Launch invariant independent of caller cwd'
'changed'|Set-Content -LiteralPath $exe
Reject {& $launcher -ValidateOnly} 'Build hash mismatch rejected'
$junction=Join-Path $root 'link'
New-Item -ItemType Junction -Path $junction -Target $fixture|Out-Null
Reject {Assert-ClientDescendant (Join-Path $junction 'file') $root} 'Junction escape rejected'
# Remove only the verified link itself; never recursively traverse it.
if((Get-Item -LiteralPath $junction).Attributes -band [IO.FileAttributes]::ReparsePoint){Remove-Item -LiteralPath $junction -Force}
Write-Output "PASS $script:checks client deployment/launcher contracts; fixture files only."
