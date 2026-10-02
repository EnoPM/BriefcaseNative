[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_.-]+$')][string]$RepositoryName,
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$')][string]$Repository,
    [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{40}$')][string]$Commit,
    [Parameter(Mandatory)][string]$Archive,
    [switch]$Draft
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$source = [IO.Path]::GetFullPath((Join-Path $root "repositories\$RepositoryName"))
$archivePath = [IO.Path]::GetFullPath($Archive)
if (-not $source.StartsWith($root + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or
    -not $archivePath.StartsWith((Join-Path $source 'dist') + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or
    $Repository.Split('/')[1] -cne $RepositoryName) { throw 'Release source, archive or destination is invalid.' }
if (-not $env:GH_TOKEN) { throw 'GH_TOKEN is required to publish a release.' }
$version = (Get-Content -LiteralPath (Join-Path $source 'VERSION') -Raw).Trim()
if ($version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' -or
    [IO.Path]::GetFileName($archivePath) -cne "$RepositoryName-windows-x64-$version.zip") {
    throw 'Archive and VERSION do not match.'
}
$actual = (& git -c "safe.directory=$($source.Replace('\','/'))" -C $source rev-parse HEAD).Trim()
if ($LASTEXITCODE -or $actual -cne $Commit) { throw 'Release commit does not match the checked-out source.' }
$tag = "v$version"
& git -c "safe.directory=$($source.Replace('\','/'))" -C $source rev-parse --verify --quiet "refs/tags/$tag^{commit}" *> $null
if ($LASTEXITCODE -eq 0) { throw 'This mod version has already been tagged.' }
$identity = (& gh api "repos/$Repository" | ConvertFrom-Json).full_name
if ($LASTEXITCODE -or $identity -cne $Repository) { throw 'GitHub repository identity mismatch.' }
$hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
$size = (Get-Item -LiteralPath $archivePath).Length
$notes = Join-Path $source ('build\release-notes-' + [guid]::NewGuid().ToString('N') + '.md')
try {
    @("$RepositoryName $version for Windows x64 dedicated servers.", '',
      'Install the matching BriefcaseNative Windows server release first, then follow the README included in this archive.',
      'Stop the server before replacing mod files. Keep your existing Data/config.json when upgrading.') |
        Set-Content -LiteralPath $notes -Encoding utf8
    & gh release create $tag $archivePath --repo $Repository --target $Commit --title "$RepositoryName $version" --notes-file $notes --draft
    if ($LASTEXITCODE) { throw 'Failed to create the draft release.' }
    $apiUrl = & gh release view $tag --repo $Repository --json apiUrl --jq .apiUrl
    if ($LASTEXITCODE -or $apiUrl -notmatch ('^https://api\.github\.com/repos/' +
            [regex]::Escape($Repository) + '/releases/[0-9]+$')) {
        throw 'Cannot verify the new draft release.'
    }
    $release = & gh api $apiUrl | ConvertFrom-Json
    if ($LASTEXITCODE -or -not $release.draft -or $release.tag_name -cne $tag) {
        throw 'Unexpected draft release state.'
    }
    $assets = @($release.assets | Where-Object { $_.name -ceq [IO.Path]::GetFileName($archivePath) })
    if ($assets.Count -ne 1 -or $assets[0].state -cne 'uploaded' -or
        $assets[0].digest -cne "sha256:$hash" -or $assets[0].size -ne $size) {
        throw 'Uploaded mod archive failed verification; the release remains a draft.'
    }
    if (-not $Draft) {
        & gh release edit $tag --repo $Repository --draft=false --latest
        if ($LASTEXITCODE) { throw 'Release verification passed but publication failed.' }
    }
    Write-Output "Release ready: $($release.html_url)"
} finally {
    Remove-Item -LiteralPath $notes -Force -ErrorAction SilentlyContinue
}
