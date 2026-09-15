[CmdletBinding()]param([string]$SdkPath='')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$config=Get-Content -LiteralPath (Join-Path $project 'mod-build.json') -Raw|ConvertFrom-Json
if(-not $SdkPath){
 if($config.sdkRepository -cnotmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $config.sdkVersion -cnotmatch '^\d+\.\d+\.\d+$'){throw 'Invalid SDK release reference'}
 $cache=Join-Path $project '.sdk'
 New-Item -ItemType Directory -Path $cache -Force|Out-Null
 $work=Join-Path $cache ([guid]::NewGuid().ToString('N'))
 New-Item -ItemType Directory -Path $work|Out-Null
 $name="BriefcaseNative-SDK-$($config.sdkVersion).zip"
 $base="https://github.com/$($config.sdkRepository)/releases/download/v$($config.sdkVersion)"
 $archive=Join-Path $work $name
 Invoke-WebRequest -Uri "$base/$name" -OutFile $archive
 $actual=(Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant()
 if($config.sdkSha256){
  if($config.sdkSha256 -cnotmatch '^[a-f0-9]{64}$' -or $actual -cne $config.sdkSha256){throw 'Pinned SDK checksum mismatch'}
 } else {
  # Bootstrap first SDK release: use the checksum published next to this fixed version.
  $checksum=Invoke-WebRequest -Uri "$base/$name.sha256"
  if($checksum.Content.Trim() -cne "$actual  $name"){throw 'SDK release checksum mismatch'}
 }
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 $zip=[IO.Compression.ZipFile]::OpenRead($archive)
 try {
  foreach($entry in $zip.Entries){
   if($entry.FullName -match '(^[/\\]|:|(^|[/\\])\.\.([/\\]|$))'){throw 'Invalid SDK archive path'}
  }
 }finally{$zip.Dispose()}
 $SdkPath=Join-Path $work 'sdk'
 Expand-Archive -LiteralPath $archive -DestinationPath $SdkPath
}
$SdkPath=[IO.Path]::GetFullPath($SdkPath)
$manifest=Get-Content -LiteralPath (Join-Path $SdkPath 'SDK.json') -Raw|ConvertFrom-Json
if($manifest.schemaVersion -ne 1 -or $manifest.version -cne $config.sdkVersion -or $manifest.abiVersion -ne 1){throw 'Unsupported SDK version'}
foreach($record in $manifest.files){
 $path=[IO.Path]::GetFullPath((Join-Path $SdkPath $record.path))
 if(-not $path.StartsWith($SdkPath.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid SDK inventory path'}
 if((Get-FileHash -LiteralPath $path).Hash -ine $record.sha256){throw 'SDK inventory mismatch'}
}
return $SdkPath
