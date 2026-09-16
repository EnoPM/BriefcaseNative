[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'Package-Tools.ps1')
$version=& (Join-Path $PSScriptRoot 'Read-Version.ps1') -ProjectRoot $project
$output=Join-Path $project 'dist/Releases'
New-Item -ItemType Directory -Path $output -Force|Out-Null

function New-BriefcaseSdk([ValidateSet('server','client')][string]$Environment) {
 $stage=Join-Path $project ("artifacts/sdk-$Environment-"+[guid]::NewGuid().ToString('N'))
 foreach($dir in @('include','third_party/include','Licenses','cmake')){
  New-Item -ItemType Directory -Path (Join-Path $stage $dir) -Force|Out-Null
 }
 Copy-Item -Path (Join-Path $project 'sdk/Briefcase.ModApi/include/*') -Destination (Join-Path $stage 'include') -Recurse -Force
 if($Environment -eq 'client'){
  Copy-Item -Path (Join-Path $project 'sdk/Briefcase.ClientModApi/include/*') -Destination (Join-Path $stage 'include') -Recurse -Force
 }
 # The typed game API is shared and calls the runtime exclusively through the public C ABI.
 $typedSource=Join-Path $stage 'src/DeceiveInc'
 New-Item -ItemType Directory -Path $typedSource -Force|Out-Null
 Copy-Item -Path (Join-Path $project 'sdk/Briefcase.DeceiveInc/include/*') -Destination (Join-Path $stage 'include') -Recurse -Force
 foreach($name in @('Spy.cpp','SpyContracts.hpp')){
  Copy-Item -LiteralPath (Join-Path $project "sdk/Briefcase.DeceiveInc/src/$name") -Destination $typedSource
 }
 Copy-Item -LiteralPath (Join-Path $project 'build/_deps/json-src/include/nlohmann') -Destination (Join-Path $stage 'third_party/include') -Recurse
 Copy-Item -LiteralPath (Join-Path $project 'build/_deps/json-src/LICENSE.MIT') -Destination (Join-Path $stage 'Licenses/nlohmann-json.txt')
 $config=(Get-Content -LiteralPath (Join-Path $project 'sdk/BriefcaseNativeSDKConfig.cmake.in') -Raw).Replace('@VERSION@',$version).Replace('@ENVIRONMENT@',$Environment)
 [IO.File]::WriteAllText((Join-Path $stage 'cmake/BriefcaseNativeSDKConfig.cmake'),$config)
 $versionConfig=@'
set(PACKAGE_VERSION "@VERSION@")
if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)
 set(PACKAGE_VERSION_EXACT TRUE)
 set(PACKAGE_VERSION_COMPATIBLE TRUE)
else()
 set(PACKAGE_VERSION_COMPATIBLE FALSE)
endif()
'@
 [IO.File]::WriteAllText((Join-Path $stage 'cmake/BriefcaseNativeSDKConfigVersion.cmake'),$versionConfig.Replace('@VERSION@',$version))
 foreach($file in Get-ChildItem -LiteralPath $stage -Recurse -File){
  $text=[IO.File]::ReadAllText($file.FullName).Replace(([string][char]13+[char]10),[string][char]10)
  [IO.File]::WriteAllText($file.FullName,$text,[Text.UTF8Encoding]::new($false))
 }
 $files=@(Get-ChildItem -LiteralPath $stage -File -Recurse|Sort-Object FullName|ForEach-Object{
  [ordered]@{path=$_.FullName.Substring($stage.Length+1).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant();bytes=$_.Length}
 })
 Write-PackageJson (Join-Path $stage 'SDK.json') ([ordered]@{schemaVersion=1;version=$version;abiVersion=1;environment=$Environment;files=$files})
 $label=[cultureinfo]::InvariantCulture.TextInfo.ToTitleCase($Environment)
 $archive=Join-Path $output "BriefcaseNative-SDK-$label-$version.zip"
 Assert-PackagePlainPath $archive
 if(Test-Path -LiteralPath $archive){Remove-Item -LiteralPath $archive -Force}
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 $zip=[IO.Compression.ZipFile]::Open($archive,[IO.Compression.ZipArchiveMode]::Create)
 try {
  foreach($file in Get-ChildItem -LiteralPath $stage -File -Recurse|Sort-Object FullName){
   $entry=$zip.CreateEntry($file.FullName.Substring($stage.Length+1).Replace('\','/'))
   $entry.LastWriteTime=[DateTimeOffset]::new(2020,1,1,0,0,0,[TimeSpan]::Zero)
   $inputStream=[IO.File]::OpenRead($file.FullName);$outputStream=$entry.Open()
   try{$inputStream.CopyTo($outputStream)}finally{$inputStream.Dispose();$outputStream.Dispose()}
  }
 } finally {$zip.Dispose()}
 & (Join-Path $PSScriptRoot 'Assert-SDK.ps1') -Archive $archive -Version $version -Environment $Environment
 Write-Output "$label SDK prepared: $archive"
}

$legacy=Join-Path $output "BriefcaseNative-SDK-$version.zip"
if(Test-Path -LiteralPath $legacy){Remove-Item -LiteralPath $legacy -Force}
New-BriefcaseSdk server
New-BriefcaseSdk client
