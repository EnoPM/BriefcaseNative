[CmdletBinding()]param([Parameter(Mandatory=$true)][string]$Version,[Parameter(Mandatory=$true)][string]$Commit,[Parameter(Mandatory=$true)][string]$Repository,[switch]$Draft)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
& (Join-Path $PSScriptRoot 'Validate-Release.ps1') -Version $Version -Commit $Commit
$config=Get-Content -LiteralPath (Join-Path $project 'mod-build.json') -Raw|ConvertFrom-Json
if($Repository -cnotmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$'){throw 'Invalid repository'}
$manifest=Get-Content -LiteralPath (Join-Path $project 'briefcase.mod.json') -Raw|ConvertFrom-Json
$archive=Join-Path $project "dist/$($config.repository)-windows-x64-$Version.zip"
$checksum=$archive+'.sha256'
$hash=(Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant()
if((Get-Content -LiteralPath $checksum -Raw).Trim() -cne "$hash  $([IO.Path]::GetFileName($archive))"){throw 'Checksum mismatch'}
$prefix="Briefcase/Mods/$($manifest.id)/"
$expected=@(($prefix+$manifest.entry),($prefix+'briefcase.mod.json'),($prefix+'Data/config.json'),($prefix+'Licenses/nlohmann-json.txt'))
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead($archive)
try{
 $names=@($zip.Entries|ForEach-Object {$_.FullName.Replace('\','/')})
 if(@(Compare-Object $expected $names).Count){throw 'Invalid mod archive inventory'}
}finally{$zip.Dispose()}
$tag="v$Version"
& gh release create $tag $archive $checksum --repo $Repository --target $Commit --title "$($config.repository) $Version" --generate-notes --draft
if($LASTEXITCODE){throw 'Release creation failed; existing releases are never overwritten'}
$url=& gh release view $tag --repo $Repository --json apiUrl --jq .apiUrl
if($LASTEXITCODE -or $url -notmatch ('^https://api\.github\.com/repos/'+[regex]::Escape($Repository)+'/releases/[0-9]+$')){throw 'Cannot resolve draft'}
$json=& gh api $url
if($LASTEXITCODE){throw 'Cannot inspect uploaded assets'}
$release=$json|ConvertFrom-Json
if(-not $release.draft -or $release.tag_name -cne $tag){throw 'Unexpected release'}
foreach($file in @($archive,$checksum)){
 $asset=@($release.assets|Where-Object {$_.name -ceq [IO.Path]::GetFileName($file)})
 if($asset.Count -ne 1 -or $asset[0].state -ne 'uploaded' -or $asset[0].digest -cne ('sha256:'+(Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant())){throw 'Uploaded assets differ; draft retained'}
}
if(-not $Draft){& gh release edit $tag --repo $Repository --draft=false --latest;if($LASTEXITCODE){throw 'Publication failed'}}
Write-Output $release.html_url
