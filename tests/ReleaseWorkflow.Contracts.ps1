# A real local release archive; git/gh are substitutes, no network or publication.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot
$fixture=Join-Path $project ('artifacts/release-workflow-'+[guid]::NewGuid().ToString('N'))
foreach($dir in @('scripts/release','scripts/update','dist/Releases')){
    New-Item -ItemType Directory -Path (Join-Path $fixture $dir) -Force|Out-Null
}
Copy-Item -Path (Join-Path $project 'scripts/release/*.ps1') -Destination (Join-Path $fixture 'scripts/release')
Copy-Item -LiteralPath (Join-Path $project 'scripts/update/Updater.ps1') -Destination (Join-Path $fixture 'scripts/update')
Copy-Item -LiteralPath (Join-Path $project 'VERSION') -Destination $fixture
$global:bcReleaseTestVersion=& (Join-Path $fixture 'scripts/release/Read-Version.ps1') -ProjectRoot $fixture
$global:bcReleaseTestArchive=Join-Path $fixture "dist/Releases/BriefcaseNative-Server-windows-x64-$global:bcReleaseTestVersion.zip"
Copy-Item -LiteralPath (Join-Path $project "dist/Releases/BriefcaseNative-Server-windows-x64-$global:bcReleaseTestVersion.zip") -Destination $global:bcReleaseTestArchive
$global:bcReleaseTestSdk=Join-Path $fixture "dist/Releases/BriefcaseNative-SDK-$global:bcReleaseTestVersion.zip"
Copy-Item -LiteralPath (Join-Path $project "dist/Releases/BriefcaseNative-SDK-$global:bcReleaseTestVersion.zip") -Destination $global:bcReleaseTestSdk
$global:bcReleaseTestCommit='a'*40
$global:bcReleaseTestTag=$null
$global:bcReleaseTestMode='ok'
$global:bcReleaseTestCommands=[Collections.Generic.List[string]]::new()
$checks=0
function Check([bool]$Value,[string]$Message){if(-not $Value){throw $Message};$script:checks++}
function Reject([scriptblock]$Action){$caught=$false;try{& $Action|Out-Null}catch{$caught=$true};Check $caught 'Unsafe release accepted.'}
function git {
    $global:LASTEXITCODE=0
    if($args -contains '--verify'){
        if($global:bcReleaseTestTag){return $global:bcReleaseTestTag}
        $global:LASTEXITCODE=1;return
    }
    return $global:bcReleaseTestCommit
}
function gh {
    $command=$args -join ' '
    $global:bcReleaseTestCommands.Add($command)
    $global:LASTEXITCODE=0
    if($command.StartsWith('release create ')){
        if($global:bcReleaseTestMode -eq 'exists'){$global:LASTEXITCODE=1}
        return
    }
    if($command.StartsWith('release view ')){return 'https://api.github.com/repos/fixture/repo/releases/123'}
    if($command -eq 'api https://api.github.com/repos/fixture/repo/releases/123'){
        $assets=@(foreach($path in @($global:bcReleaseTestArchive,$global:bcReleaseTestSdk)){
            $hash=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()
            if($global:bcReleaseTestMode -eq 'corrupt'){$hash='0'*64}
            @{name=[IO.Path]::GetFileName($path);state='uploaded';size=(Get-Item -LiteralPath $path).Length;digest="sha256:$hash"}
        })
        return @{draft=$true;tag_name="v$global:bcReleaseTestVersion";html_url='https://github.com/fixture/repo/releases/tag/test';assets=$assets}|ConvertTo-Json -Depth 5
    }
    if($command.StartsWith('release edit ')){return}
    throw "Unexpected command: $command"
}
$validate=Join-Path $fixture 'scripts/release/Test-ReleaseVersion.ps1'
$publish=Join-Path $fixture 'scripts/release/Publish-Release.ps1'
$oldToken=$env:GH_TOKEN
$oldSummary=$env:GITHUB_STEP_SUMMARY
try {
    $env:GH_TOKEN='fixture-never-sent'
    $env:GITHUB_STEP_SUMMARY=''
    & $validate -Version $global:bcReleaseTestVersion -Commit $global:bcReleaseTestCommit|Out-Null
    Check ($LASTEXITCODE -eq 0) 'Absent tag reported failure.'
    foreach($version in @('v0.3.0','0.3.0-beta','01.3.0','99.99.99',('0.3.0'+[Environment]::NewLine+'injected'))){
        Reject {& $validate -Version $version -Commit $global:bcReleaseTestCommit}
    }
    foreach($invalid in @('v1.2.3','01.2.3','1.2.3-beta',"1.2.3`ninjected",'')) {
        [IO.File]::WriteAllText((Join-Path $fixture 'VERSION'),$invalid)
        Reject {& $validate -Version $global:bcReleaseTestVersion -Commit $global:bcReleaseTestCommit}
    }
    [IO.File]::WriteAllText((Join-Path $fixture 'VERSION'),$global:bcReleaseTestVersion+"`r`n")
    Reject {& $validate -Version $global:bcReleaseTestVersion -Commit ('b'*40)}
    $global:bcReleaseTestTag='b'*40
    Reject {& $validate -Version $global:bcReleaseTestVersion -Commit $global:bcReleaseTestCommit}
    $global:bcReleaseTestTag=$global:bcReleaseTestCommit
    & $validate -Version $global:bcReleaseTestVersion -Commit $global:bcReleaseTestCommit|Out-Null
    Check ($LASTEXITCODE -eq 0) 'Matching tag rejected.'
    $global:bcReleaseTestTag=$null
    & $publish -Version $global:bcReleaseTestVersion -Commit $global:bcReleaseTestCommit -Repository fixture/repo -Draft|Out-Null
    Check (-not ($global:bcReleaseTestCommands -match '^release edit ')) 'Draft published.'
    Check ($global:bcReleaseTestCommands[0] -match '--draft$') 'Release visible before verification.'
    Check ($global:bcReleaseTestCommands[0] -match "--target $global:bcReleaseTestCommit") 'Release targets wrong commit.'
    $global:bcReleaseTestCommands.Clear()
    & $publish -Version $global:bcReleaseTestVersion -Commit $global:bcReleaseTestCommit -Repository fixture/repo|Out-Null
    Check ($global:bcReleaseTestCommands[-1] -match '--draft=false --latest$') 'Verified release not published.'
    $global:bcReleaseTestCommands.Clear();$global:bcReleaseTestMode='corrupt'
    Reject {& $publish -Version $global:bcReleaseTestVersion -Commit $global:bcReleaseTestCommit -Repository fixture/repo}
    Check (-not ($global:bcReleaseTestCommands -match '^release edit ')) 'Corrupt assets published.'
    $global:bcReleaseTestCommands.Clear();$global:bcReleaseTestMode='exists'
    Reject {& $publish -Version $global:bcReleaseTestVersion -Commit $global:bcReleaseTestCommit -Repository fixture/repo}
    Check ($global:bcReleaseTestCommands.Count -eq 1) 'Existing release modified.'
    $global:LASTEXITCODE=0
    Write-Output "PASS $checks release workflow contracts; no remote calls."
} finally {
    $env:GH_TOKEN=$oldToken
    $env:GITHUB_STEP_SUMMARY=$oldSummary
}
