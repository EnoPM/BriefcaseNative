$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$target=Join-Path $project 'third_party\RE-UE4SS'
$commit='d935b5b23bac03b65c14ae38382b02007204cc2e'
if(-not(Test-Path -LiteralPath $target)){
    git clone --branch v3.0.1 --depth 1 https://github.com/UE4SS-RE/RE-UE4SS.git $target
    if($LASTEXITCODE){throw 'UE4SS clone failed.'}
}
$actual=git -C $target rev-parse HEAD
if($LASTEXITCODE -or $actual -ne $commit){throw 'Unexpected UE4SS revision. Existing checkout left intact.'}
git -c 'url.https://github.com/.insteadOf=git@github.com:' -C $target submodule update --init --recursive --depth 1
if($LASTEXITCODE){throw 'Submodule checkout failed.'}
& (Join-Path $PSScriptRoot 'Patch-UE4SS.ps1')
