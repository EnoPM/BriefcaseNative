[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][ValidatePattern('^[a-f0-9]{40}$')][string]$Commit,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$')][string]$Repository,
    [switch]$Draft
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
& (Join-Path $PSScriptRoot 'Test-ReleaseVersion.ps1') -Version $Version -Commit $Commit
. (Join-Path $PSScriptRoot 'Package-Tools.ps1')
$archive=Join-Path $project "dist/Releases/BriefcaseNative-Server-windows-x64-$Version.zip"
$hash=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
$client=Join-Path $project "dist/Releases/BriefcaseNative-Client-windows-x64-$Version.zip"
$clientHash=(Get-FileHash -LiteralPath $client -Algorithm SHA256).Hash.ToLowerInvariant()
# Verify the artifact again after transfer between jobs. No dependency build runs here.
$stage=Join-Path $project ('artifacts/release-verify-'+[guid]::NewGuid().ToString('N'))
Expand-PackageArchive $archive $stage
$null=Read-FrameworkPackage $stage $Version '78afe1dbeecb09027c274def4f0ac855b447dc52ffe3cd9482c1be4341b0dae6'
$clientStage=Join-Path $project ('artifacts/client-release-verify-'+[guid]::NewGuid().ToString('N'))
Expand-PackageArchive $client $clientStage
$supportedClient=(Get-Content -LiteralPath (Join-Path $project 'runtime/Briefcase.NativeHost/ClientBuild.json') -Raw|ConvertFrom-Json).sha256
$null=Read-ClientPackage $clientStage $Version $supportedClient
$sdk=Join-Path $project "dist/Releases/BriefcaseNative-SDK-$Version.zip"
& (Join-Path $PSScriptRoot 'Assert-SDK.ps1') -Archive $sdk -Version $Version
if(-not $env:GH_TOKEN){throw 'GH_TOKEN is required for publication.'}
$tag="v$Version"
# Never replace an existing release. Assemble as a draft so incomplete assets stay invisible to updaters.
& gh release create $tag $archive $client $sdk --repo $Repository --target $Commit --title "BriefcaseNative $Version" --generate-notes --draft
if($LASTEXITCODE){throw 'Release creation/upload failed. Inspect any draft left on GitHub before retrying.'}
# Drafts may not have a tag yet: resolve their REST URL through the CLI's draft-aware lookup.
$apiUrl=& gh release view $tag --repo $Repository --json apiUrl --jq .apiUrl
if($LASTEXITCODE -or $apiUrl -notmatch ('^https://api\.github\.com/repos/'+[regex]::Escape($Repository)+'/releases/[0-9]+$')){
    throw 'Cannot locate the new draft release.'
}
$json=& gh api $apiUrl
if($LASTEXITCODE){throw 'Cannot verify uploaded release. It remains a draft.'}
$release=$json|ConvertFrom-Json
if(-not $release.draft -or $release.tag_name -cne $tag){throw 'Unexpected release state.'}
$uploaded=@($release.assets|Where-Object {$_.name -ceq [IO.Path]::GetFileName($archive)})
if($uploaded.Count -ne 1 -or $uploaded[0].state -ne 'uploaded' -or $uploaded[0].digest -cne "sha256:$hash" -or
    $uploaded[0].size -ne (Get-Item -LiteralPath $archive).Length){
    throw 'GitHub asset verification failed. Release remains a draft.'
}
foreach($file in @($client,$sdk)){
 $asset=@($release.assets|Where-Object {$_.name -ceq [IO.Path]::GetFileName($file)})
 $digest='sha256:'+(Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant()
 if($asset.Count -ne 1 -or $asset[0].state -ne 'uploaded' -or $asset[0].digest -cne $digest -or $asset[0].size -ne (Get-Item -LiteralPath $file).Length){throw 'SDK upload verification failed; release remains a draft'}
}
if(-not $Draft){
    & gh release edit $tag --repo $Repository --draft=false --latest
    if($LASTEXITCODE){throw 'Publication failed. Inspect the release state on GitHub.'}
}
$url=$release.html_url
Write-Output "Release ready: $url (draft=$([bool]$Draft))"
if($env:GITHUB_STEP_SUMMARY){
    @("## BriefcaseNative $Version","", "Release: $url", "", "Commit: $Commit", "", "Draft: $([bool]$Draft)", "", "Server SHA256: $hash", "", "Client SHA256: $clientHash") |
        Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Encoding utf8
}
