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
foreach($index in 1..8){
    $name=('20260101-0000{0:D2}-000' -f $index)
    $directory=Join-Path $root "BriefcaseDeploymentBackups\$name"
    New-Item -ItemType Directory -Path $directory -Force|Out-Null
    'backup'|Set-Content -LiteralPath (Join-Path $directory 'marker.txt')
    (Get-Item -LiteralPath $directory).LastWriteTimeUtc=[datetime]::UtcNow.AddMinutes($index)
}
$manual=Join-Path $root 'BriefcaseDeploymentBackups\manual-keep'
New-Item -ItemType Directory -Path $manual -Force|Out-Null
'keep'|Set-Content -LiteralPath (Join-Path $manual 'marker.txt')
Remove-OldClientDeploymentBackups -ClientRoot $root -Keep 5
$retained=@(Get-ChildItem -LiteralPath (Join-Path $root 'BriefcaseDeploymentBackups') -Directory|Where-Object {$_.Name -cmatch '^[0-9]{8}-[0-9]{6}-[0-9]{3}$'})
Check ($retained.Count -eq 5) 'Client backup retention failed'
Check (Test-Path -LiteralPath (Join-Path $manual 'marker.txt')) 'Manual client backup removed'
$junction=Join-Path $root 'link'
New-Item -ItemType Junction -Path $junction -Target $fixture|Out-Null
Reject {Assert-ClientDescendant (Join-Path $junction 'file') $root} 'Junction escape rejected'
# Remove only the verified link itself; never recursively traverse it.
if((Get-Item -LiteralPath $junction).Attributes -band [IO.FileAttributes]::ReparsePoint){Remove-Item -LiteralPath $junction -Force}
Write-Output "PASS $script:checks client deployment path and retention contracts; fixture files only."
