# Shared release protocol and transactional installer. Windows PowerShell 5.1 compatible.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-UpdatePlainPath([string]$Path) {
    $cursor = [IO.Path]::GetFullPath($Path)
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse point forbidden: $cursor"
            }
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
}

function Resolve-UpdatePath([string]$Root, [string]$Relative) {
    # Reject Windows aliases, alternate streams and both path separators before extraction.
    if ($Relative -cnotmatch '^[A-Za-z0-9_./\\-]+$') { throw 'Invalid package path.' }
    foreach ($part in ($Relative -split '[/\\]')) {
        if (-not $part -or $part -in @('.', '..') -or $part.EndsWith('.') -or
            $part -match '^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(\.|$)') { throw 'Unsafe package path.' }
    }
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $path = [IO.Path]::GetFullPath((Join-Path $base $Relative))
    if (-not $path.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Path escaped installation.' }
    Assert-UpdatePlainPath $path
    return $path
}

function Test-UpdateLegacyModPath([string]$Relative) {
 return $Relative.Replace('\','/') -cmatch '^Briefcase/Mods/[a-z0-9.-]+/([^/]+\.dll|briefcase\.mod\.json|Data/config\.json)$'
}
function Test-UpdateManagedPath([string]$Relative) {
 $p=$Relative.Replace('\','/')
 return ($p -cin @('version.dll','Briefcase.ServerLauncher.exe','Package.json','StartBriefcaseNativeServer.ps1') -or
  $p -cmatch '^Briefcase/(Runtime/[^/]+\.dll|Tools/Briefcase\.(AdminSetup|ServerRestart)\.exe)$' -or
  $p -cmatch '^Briefcase/Updater/(Updater\.ps1|Restart-Server\.ps1|Launch-Server\.ps1|updater\.example\.json|build\.json)$' -or
  $p -cmatch '^Briefcase/(Docs|Licenses|Localization)/[A-Za-z0-9_./-]+\.(json|txt|md)$')
}
function Test-UpdateDefaultPath([string]$Relative) { return $false }

function ConvertTo-ReleaseVersion([string]$Value) {
    if ($Value -cnotmatch '^v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw 'Only stable major.minor.patch versions are supported.'
    }
    return [version]$Value.TrimStart('v')
}

function Select-UpdateAsset($Release, [string]$Repository, [string]$CurrentVersion) {
    if ($Repository -cnotmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { throw 'Expected GitHub owner/repository.' }
    if ($Release.draft -or $Release.prerelease) { return $null }
    $version = ConvertTo-ReleaseVersion $Release.tag_name
    if ($version -le (ConvertTo-ReleaseVersion $CurrentVersion)) { return $null }
    $name = "BriefcaseNative-Server-windows-x64-$version.zip"
    $matches = @($Release.assets | Where-Object { $_.name -ceq $name })
    if ($matches.Count -ne 1) { throw "Release must contain exactly one asset named $name." }
    $asset = $matches[0]
    $expectedUrl = "https://github.com/$Repository/releases/download/$($Release.tag_name)/$name"
    if ($asset.browser_download_url -cne $expectedUrl -or $asset.state -ne 'uploaded' -or
        $asset.size -le 0 -or $asset.size -gt 536870912 -or $asset.digest -cnotmatch '^sha256:[a-f0-9]{64}$') {
        throw 'Invalid release asset URL, size or GitHub SHA256 digest.'
    }
    return [pscustomobject]@{version="$version";url=$expectedUrl;size=[long]$asset.size;sha256=$asset.digest.Substring(7)}
}

function Write-UpdateJson([string]$Path, $Value) {
    Assert-UpdatePlainPath $Path
    New-Item -ItemType Directory -Path (Split-Path $Path) -Force | Out-Null
    $temp = $Path + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    [IO.File]::WriteAllText($temp, ($Value | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
    if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($temp, $Path, ($temp + '.previous')); Remove-Item -LiteralPath ($temp + '.previous') -Force }
    else { [IO.File]::Move($temp, $Path) }
}

function Copy-UpdateFile([string]$Source, [string]$Destination) {
    Assert-UpdatePlainPath $Destination
    New-Item -ItemType Directory -Path (Split-Path $Destination) -Force | Out-Null
    $temp = $Destination + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    try {
        [IO.File]::Copy($Source, $temp)
        if (Test-Path -LiteralPath $Destination) { [IO.File]::Replace($temp, $Destination, ($temp + '.previous')); Remove-Item -LiteralPath ($temp + '.previous') -Force }
        else { [IO.File]::Move($temp, $Destination) }
    } finally {
        if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Force }
    }
}

function Expand-UpdateArchive([string]$Archive, [string]$Stage) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        if ($zip.Entries.Count -gt 4096) { throw 'Too many archive entries.' }
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $total = 0L
        # Validate the entire directory before writing anything.
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName.TrimEnd([char[]]@('/','\'))
            $path = Resolve-UpdatePath $Stage $name
            if (-not $seen.Add($path)) { throw 'Duplicate archive path.' }
            if ((($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) { throw 'Archive symlink forbidden.' }
            $total += $entry.Length
            if ($total -gt 1073741824 -or $entry.Length -gt 536870912) { throw 'Archive exceeds size limit.' }
        }
        foreach ($entry in $zip.Entries) {
            $path = Resolve-UpdatePath $Stage $entry.FullName.TrimEnd([char[]]@('/','\'))
            if (($entry.FullName.EndsWith('/') -or $entry.FullName.EndsWith('\'))) { New-Item -ItemType Directory -Path $path -Force | Out-Null; continue }
            New-Item -ItemType Directory -Path (Split-Path $path) -Force | Out-Null
            $source = $entry.Open()
            $destination = [IO.File]::Open($path, [IO.FileMode]::CreateNew)
            try {
                $buffer = New-Object byte[] 65536
                $written = 0L
                while (($count = $source.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $written += $count
                    if ($written -gt $entry.Length -or $written -gt 536870912) { throw 'Expanded entry exceeds declared size.' }
                    $destination.Write($buffer, 0, $count)
                }
                if ($written -ne $entry.Length) { throw 'Truncated archive entry.' }
            } finally { $destination.Dispose(); $source.Dispose() }
        }
    } finally { $zip.Dispose() }
}

function Read-UpdatePackage([string]$Stage, [string]$Version, [string]$GameHash) {
    $manifestPath = Resolve-UpdatePath $Stage 'Package.json'
    if ((Get-Item -LiteralPath $manifestPath).Length -gt 2097152) { throw 'Package manifest too large.' }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.updateSchema -ne 1 -or $manifest.environment -cne 'server' -or
        $manifest.platform -cne 'windows-x64' -or $manifest.frameworkVersion -cne $Version -or
        $manifest.gameSha256 -ine $GameHash) { throw 'Package version, platform, environment or game build mismatch.' }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $manifest.files) {
        $path = Resolve-UpdatePath $Stage $file.path
        if ($file.path -ieq 'Package.json' -or -not $seen.Add($path) -or
            (-not (Test-UpdateManagedPath $file.path) -and -not (Test-UpdateDefaultPath $file.path))) {
            throw 'Unmanaged or duplicate package file.'
        }
        if ($file.sha256 -cnotmatch '^[a-f0-9]{64}$' -or (Get-Item -LiteralPath $path).Length -ne $file.bytes -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $file.sha256) { throw 'Package file hash mismatch.' }
    }
    foreach ($file in Get-ChildItem -LiteralPath $Stage -Recurse -File) {
        if ($file.FullName -ine $manifestPath -and -not $seen.Contains($file.FullName)) { throw 'Unlisted archive file.' }
    }
    foreach ($required in @('Briefcase.ServerLauncher.exe','Briefcase/Runtime/Briefcase.ServerBootstrap.dll','StartBriefcaseNativeServer.ps1','Briefcase/Runtime/Briefcase.NativeHost.dll',
        'Briefcase/Updater/Updater.ps1','Briefcase/Updater/Restart-Server.ps1','Briefcase/Updater/Launch-Server.ps1','Briefcase/Updater/build.json',
        'Briefcase/Tools/Briefcase.ServerRestart.exe')) {
        if (-not $seen.Contains((Resolve-UpdatePath $Stage $required))) { throw "Incomplete package: $required" }
    }
    if (Test-Path -LiteralPath (Join-Path $Stage 'version.dll')) { throw 'Server updates must not install a proxy DLL.' }
    $build = Get-Content -LiteralPath (Resolve-UpdatePath $Stage 'Briefcase/Updater/build.json') -Raw | ConvertFrom-Json
    if ($build.frameworkVersion -cne $Version) { throw 'Installed version marker mismatch.' }
    return $manifest
}

function Restore-UpdateTransaction([string]$Win64) {
    $journalPath = Resolve-UpdatePath $Win64 'Briefcase/Updates/transaction.json'
    if (-not (Test-Path -LiteralPath $journalPath)) { return }
    $journal = Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json
    if ($journal.state -ne 'installing') { return }
    if ($journal.id -cnotmatch '^[a-f0-9]{32}$') { throw 'Invalid update recovery journal.' }
    # Validate every backup before attempting recovery. Failure blocks launching a mixed installation.
    foreach ($item in $journal.files) {
        if (-not (Test-UpdateManagedPath $item.path) -and -not (Test-UpdateLegacyModPath $item.path)) { throw 'Invalid recovery path.' }
        $null = Resolve-UpdatePath $Win64 $item.path
        if ($item.existed) {
            $saved = Resolve-UpdatePath $Win64 ("Briefcase/Updates/$($journal.id)/backup/" + $item.path)
            if ((Get-FileHash -LiteralPath $saved).Hash -ine $item.previousHash) { throw 'Recovery backup damaged.' }
        }
    }
    foreach ($item in $journal.files) {
        $destination = Resolve-UpdatePath $Win64 $item.path
        if ($item.existed) {
            Copy-UpdateFile (Resolve-UpdatePath $Win64 ("Briefcase/Updates/$($journal.id)/backup/" + $item.path)) $destination
        } elseif (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Force }
    }
    $journal.state = 'rolled-back'
    Write-UpdateJson $journalPath $journal
}

function Install-UpdatePackage([string]$Win64, [string]$Stage, $Manifest, [string]$Id) {
    if ($Id -cnotmatch '^[a-f0-9]{32}$') { throw 'Invalid transaction identifier.' }
    $files = @($Manifest.files | ForEach-Object { $_.path }) + @('Package.json')
    $newPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($relative in $files) { $null = $newPaths.Add($relative.Replace('\','/')) }
    $removed = @()
    $oldPath = Resolve-UpdatePath $Win64 'Package.json'
    if (Test-Path -LiteralPath $oldPath) {
        $old = Get-Content -LiteralPath $oldPath -Raw | ConvertFrom-Json
        $removed = @($old.files | Where-Object {
            (Test-UpdateManagedPath $_.path) -and -not $newPaths.Contains($_.path.Replace('\','/'))
        } | ForEach-Object { $_.path })
    }
    $plan = @()
    foreach ($relative in ($files + $removed)) {
        if (-not (Test-UpdateManagedPath $relative) -and -not (Test-UpdateDefaultPath $relative)) { throw 'Invalid install path.' }
        $destination = Resolve-UpdatePath $Win64 $relative
        $exists = Test-Path -LiteralPath $destination
        if ($exists -and (Test-UpdateDefaultPath $relative)) { continue }
        $hash = ''
        if ($exists) {
            $backup = Resolve-UpdatePath $Win64 ("Briefcase/Updates/$Id/backup/" + $relative)
            Copy-UpdateFile $destination $backup
            $hash = (Get-FileHash -LiteralPath $destination).Hash
            if ((Get-FileHash -LiteralPath $backup).Hash -ne $hash) { throw 'Backup verification failed.' }
        }
        $plan += [pscustomobject]@{path=$relative;existed=$exists;previousHash=$hash;remove=($relative -in $removed)}
    }
    $journalPath = Resolve-UpdatePath $Win64 'Briefcase/Updates/transaction.json'
    $journal = [pscustomobject]@{id=$Id;state='installing';version=$Manifest.frameworkVersion;files=$plan}
    Write-UpdateJson $journalPath $journal
    try {
        foreach ($item in $plan) {
            $destination = Resolve-UpdatePath $Win64 $item.path
            if ($item.remove) {
                if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Force }
            } else {
                $source = Resolve-UpdatePath $Stage $item.path
                Copy-UpdateFile $source $destination
                if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) {
                    throw 'Installed file verification failed.'
                }
            }
        }
        $journal.state = 'committed'
        Write-UpdateJson $journalPath $journal
    } catch {
        Restore-UpdateTransaction $Win64
        throw
    }
}

function Get-UpdateDownload([string]$Url, [string]$Destination, [int]$TimeoutSeconds, [long]$MaxBytes) {
    # Streaming, bounded memory/disk and total timeout, including redirect and response bodies.
    Add-Type -AssemblyName System.Net.Http
    $handler = [Net.Http.HttpClientHandler]::new()
    $handler.AllowAutoRedirect = $false
    $client = [Net.Http.HttpClient]::new($handler)
    $cancel = [Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds($TimeoutSeconds))
    $client.DefaultRequestHeaders.UserAgent.ParseAdd('BriefcaseNative-Updater/1')
    $client.DefaultRequestHeaders.Add('Accept','application/vnd.github+json')
    $client.DefaultRequestHeaders.Add('X-GitHub-Api-Version','2022-11-28')
    $response = $null
    try {
        for ($redirects = 0; $redirects -lt 6; ++$redirects) {
            $uri = [uri]$Url
            if ($uri.Scheme -ne 'https' -or $uri.Port -ne 443 -or $uri.UserInfo -or
                $uri.Host -notin @('api.github.com','github.com','release-assets.githubusercontent.com','objects.githubusercontent.com')) {
                throw 'Untrusted download destination.'
            }
            $response = $client.GetAsync($uri, [Net.Http.HttpCompletionOption]::ResponseHeadersRead, $cancel.Token).GetAwaiter().GetResult()
            if ([int]$response.StatusCode -in @(301,302,303,307,308)) {
                $Url = [uri]::new($uri, $response.Headers.Location).AbsoluteUri
                $response.Dispose(); $response = $null
                continue
            }
            $null = $response.EnsureSuccessStatusCode()
            if ($response.Content.Headers.ContentLength -gt $MaxBytes) { throw 'Download too large.' }
            $inputStream = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
            $outputStream = [IO.File]::Open($Destination, [IO.FileMode]::CreateNew)
            try {
                $buffer = New-Object byte[] 65536
                $total = 0L
                while (($read = $inputStream.ReadAsync($buffer,0,$buffer.Length,$cancel.Token).GetAwaiter().GetResult()) -gt 0) {
                    $total += $read
                    if ($total -gt $MaxBytes) { throw 'Download exceeded size limit.' }
                    $outputStream.Write($buffer,0,$read)
                }
            } finally { $outputStream.Dispose(); $inputStream.Dispose() }
            return
        }
        throw 'Too many download redirects.'
    } finally {
        if ($response) { $response.Dispose() }
        $cancel.Dispose(); $client.Dispose(); $handler.Dispose()
    }
}

function Select-ModUpdateAsset($Release, [string]$Repository, [string]$CurrentVersion, [string]$Platform) {
    if ($Repository -cnotmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $Platform -cnotmatch '^(windows|linux)-x64$') {
        throw 'Invalid mod update identity.'
    }
    if ($Release.draft -or $Release.prerelease) { return $null }
    $version = ConvertTo-ReleaseVersion $Release.tag_name
    if ($version -le (ConvertTo-ReleaseVersion $CurrentVersion)) { return $null }
    $repositoryName = $Repository.Substring($Repository.IndexOf('/') + 1)
    $name = "$repositoryName-$Platform-$version.zip"
    $matches = @($Release.assets | Where-Object { $_.name -ceq $name })
    if ($matches.Count -ne 1) { throw "Release must contain exactly one asset named $name." }
    $asset = $matches[0]
    $expectedUrl = "https://github.com/$Repository/releases/download/$($Release.tag_name)/$name"
    if ($asset.browser_download_url -cne $expectedUrl -or $asset.state -ne 'uploaded' -or
        $asset.size -le 0 -or $asset.size -gt 536870912 -or $asset.digest -cnotmatch '^sha256:[a-f0-9]{64}$') {
        throw 'Invalid mod release asset URL, size or GitHub SHA256 digest.'
    }
    return [pscustomobject]@{version="$version";url=$expectedUrl;size=[long]$asset.size;sha256=$asset.digest.Substring(7)}
}

function Read-ModUpdatePackage([string]$Stage, [string]$ModId, [string]$Version,
                               [string]$Repository, [string]$Platform) {
    if ($ModId -cnotmatch '^[a-z0-9]+([.-][a-z0-9]+)*$' -or
        $Repository -cnotmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $Platform -cnotmatch '^(windows|linux)-x64$') {
        throw 'Invalid mod package identity.'
    }
    $null = ConvertTo-ReleaseVersion $Version
    $packagePath = Resolve-UpdatePath $Stage 'ModPackage.json'
    if ((Get-Item -LiteralPath $packagePath).Length -gt 2097152) { throw 'Mod package manifest too large.' }
    $package = Get-Content -LiteralPath $packagePath -Raw | ConvertFrom-Json
    if ($package.updateSchema -ne 1 -or $package.kind -cne 'briefcase-mod' -or
        $package.platform -cne $Platform -or $package.modId -cne $ModId -or
        $package.version -cne $Version -or $package.repository -cne $Repository) {
        throw 'Incompatible mod package identity.'
    }
    $prefix = "Briefcase/Mods/$ModId/"
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in @($package.files)) {
        $path = Resolve-UpdatePath $Stage $file.path
        $preserve = $false
        if ($file.PSObject.Properties.Name -contains 'preserve') { $preserve = $file.preserve }
        if ($file.path -cnotlike "$prefix*" -or $file.path.Length -le $prefix.Length -or
            -not $seen.Add($path) -or ($preserve -and $file.path -cne ($prefix + 'Data/config.json'))) {
            throw 'Invalid mod package path.'
        }
        if ($file.sha256 -cnotmatch '^[a-f0-9]{64}$' -or (Get-Item -LiteralPath $path).Length -ne $file.bytes -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $file.sha256) {
            throw 'Mod package file hash mismatch.'
        }
    }
    $actual = @(Get-ChildItem -LiteralPath $Stage -Recurse -File)
    if ($actual.Count -ne ($seen.Count + 1)) { throw 'Unlisted mod package content.' }
    foreach ($file in $actual) {
        if ($file.FullName -ine $packagePath -and -not $seen.Contains($file.FullName)) { throw 'Unlisted mod package file.' }
    }
    $modManifestPath = Resolve-UpdatePath $Stage ($prefix + 'briefcase.mod.json')
    $mod = Get-Content -LiteralPath $modManifestPath -Raw | ConvertFrom-Json
    if ($mod.schemaVersion -ne 1 -or $mod.id -cne $ModId -or $mod.version -cne $Version -or
        $mod.environment -notin @('server','both') -or $mod.update.provider -cne 'github-releases' -or
        $mod.update.repository -cne $Repository) { throw 'Packaged mod manifest mismatch.' }
    $expectedExtension = if ($Platform -ceq 'windows-x64') { '.dll' } else { '.so' }
    if ($mod.entry -cnotmatch '^[A-Za-z0-9_.-]+\.(dll|so)$' -or -not $mod.entry.EndsWith($expectedExtension, [StringComparison]::Ordinal)) {
        throw 'Mod entry is incompatible with this platform.'
    }
    $entryPath = Resolve-UpdatePath $Stage ($prefix + $mod.entry)
    if (-not $seen.Contains($entryPath)) { throw 'Mod entry is missing from package.' }
    if ($Platform -ceq 'windows-x64') {
        $stream = [IO.File]::OpenRead($entryPath)
        try { if ($stream.ReadByte() -ne 0x4d -or $stream.ReadByte() -ne 0x5a) { throw 'Expected Windows mod DLL.' } }
        finally { $stream.Dispose() }
    }
    return $package
}

function Test-ModUpdateTargetPath([string]$Relative, [string]$ModId) {
    $prefix = "Briefcase/Mods/$ModId/"
    return $Relative.Replace('\','/').StartsWith($prefix, [StringComparison]::Ordinal)
}

function Restore-ModUpdateTransaction([string]$Win64) {
    $journalPath = Resolve-UpdatePath $Win64 'Briefcase/Updates/mod-transaction.json'
    if (-not (Test-Path -LiteralPath $journalPath)) { return }
    $journal = Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json
    if ($journal.state -ne 'installing') { return }
    if ($journal.id -cnotmatch '^[a-f0-9]{32}$' -or $journal.modId -cnotmatch '^[a-z0-9]+([.-][a-z0-9]+)*$') {
        throw 'Invalid mod recovery journal.'
    }
    foreach ($item in @($journal.files)) {
        if (-not (Test-ModUpdateTargetPath $item.path $journal.modId)) { throw 'Invalid mod recovery path.' }
        $null = Resolve-UpdatePath $Win64 $item.path
        if ($item.existed) {
            $saved = Resolve-UpdatePath $Win64 ("Briefcase/Updates/$($journal.id)/backup/" + $item.path)
            if ((Get-FileHash -LiteralPath $saved).Hash -ine $item.previousHash) { throw 'Mod recovery backup damaged.' }
        }
    }
    foreach ($item in @($journal.files)) {
        $destination = Resolve-UpdatePath $Win64 $item.path
        if ($item.existed) {
            Copy-UpdateFile (Resolve-UpdatePath $Win64 ("Briefcase/Updates/$($journal.id)/backup/" + $item.path)) $destination
        } elseif (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Force }
    }
    $journal.state = 'rolled-back'
    Write-UpdateJson $journalPath $journal
}

function Install-ModUpdatePackage([string]$Win64, [string]$Stage, $Manifest, [string]$Id) {
    if ($Id -cnotmatch '^[a-f0-9]{32}$') { throw 'Invalid mod transaction identifier.' }
    $modId = [string]$Manifest.modId
    $prefix = "Briefcase/Mods/$modId/"
    $metadataRelative = $prefix + '.briefcase-update.json'
    $wanted = @{}
    $managed = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $null = $paths.Add($metadataRelative)
    foreach ($file in @($Manifest.files)) {
        $preserve = $false
        if ($file.PSObject.Properties.Name -contains 'preserve') { $preserve = $file.preserve }
        $destination = Resolve-UpdatePath $Win64 $file.path
        if ($preserve) {
            if (-not (Test-Path -LiteralPath $destination)) { $wanted[$file.path] = $file; $null = $paths.Add($file.path) }
        } else { $wanted[$file.path] = $file; $null = $managed.Add($file.path); $null = $paths.Add($file.path) }
    }
    $metadataPath = Resolve-UpdatePath $Win64 $metadataRelative
    if (Test-Path -LiteralPath $metadataPath) {
        $old = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
        if ($old.schemaVersion -ne 1 -or $old.modId -cne $modId) { throw 'Invalid installed mod update metadata.' }
        foreach ($path in @($old.files)) {
            if (-not (Test-ModUpdateTargetPath $path $modId)) { throw 'Invalid installed mod managed path.' }
            if (-not $managed.Contains($path)) { $null = $paths.Add($path) }
        }
    }
    $metadata = [ordered]@{schemaVersion=1;modId=$modId;repository=$Manifest.repository;version=$Manifest.version;platform=$Manifest.platform;files=@($managed)}
    $plan = @()
    foreach ($relative in $paths) {
        $destination = Resolve-UpdatePath $Win64 $relative
        $exists = Test-Path -LiteralPath $destination
        $hash = ''
        if ($exists) {
            $backup = Resolve-UpdatePath $Win64 ("Briefcase/Updates/$Id/backup/" + $relative)
            Copy-UpdateFile $destination $backup
            $hash = (Get-FileHash -LiteralPath $destination).Hash
            if ((Get-FileHash -LiteralPath $backup).Hash -ne $hash) { throw 'Mod backup verification failed.' }
        }
        $plan += [pscustomobject]@{path=$relative;existed=$exists;previousHash=$hash}
    }
    $journalPath = Resolve-UpdatePath $Win64 'Briefcase/Updates/mod-transaction.json'
    $journal = [ordered]@{id=$Id;state='installing';modId=$modId;files=$plan}
    Write-UpdateJson $journalPath $journal
    try {
        foreach ($item in $plan) {
            if ($item.path -ceq $metadataRelative) { continue }
            $destination = Resolve-UpdatePath $Win64 $item.path
            if ($wanted.ContainsKey($item.path)) {
                $source = Resolve-UpdatePath $Stage $item.path
                Copy-UpdateFile $source $destination
                if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) {
                    throw 'Installed mod file verification failed.'
                }
            } elseif (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Force }
        }
        Write-UpdateJson $metadataPath $metadata
        $journal.state = 'committed'; Write-UpdateJson $journalPath $journal
    } catch { Restore-ModUpdateTransaction $Win64; throw }
}

function Invoke-ModUpdates([string]$Win64) {
    Restore-ModUpdateTransaction $Win64
    $result = [ordered]@{state='completed';checked=0;updated=0;current=0;failed=0;mods=@()}
    try {
        $config = Get-Content -LiteralPath (Resolve-UpdatePath $Win64 'Briefcase/updater.json') -Raw | ConvertFrom-Json
        if ($config.schemaVersion -ne 1 -or $config.enabled -isnot [bool] -or $config.timeoutSeconds -lt 1 -or $config.timeoutSeconds -gt 120) {
            throw 'Invalid updater configuration.'
        }
        $updateMods = $true
        if ($config.PSObject.Properties.Name -contains 'updateMods') {
            if ($config.updateMods -isnot [bool]) { throw 'updateMods must be a Boolean.' }
            $updateMods = $config.updateMods
        }
    } catch {
        $result.state='failed-kept-installed'; $result.message=$_.Exception.Message
        Write-Warning "Mod updates unavailable; installed mods retained. $($_.Exception.Message)"
        return [pscustomobject]$result
    }
    if (-not $config.enabled -or -not $updateMods) { $result.state='disabled'; return [pscustomobject]$result }
    $modsRoot = Resolve-UpdatePath $Win64 'Briefcase/Mods'
    if (-not (Test-Path -LiteralPath $modsRoot)) { return [pscustomobject]$result }
    $directories = @(Get-ChildItem -LiteralPath $modsRoot -Directory -Force)
    if ($directories.Count -gt 256) { throw 'Too many installed mods.' }
    foreach ($directory in $directories) {
        $manifestPath = Join-Path $directory.FullName 'briefcase.mod.json'
        if (-not (Test-Path -LiteralPath $manifestPath)) { continue }
        $modId = $directory.Name
        try {
            Assert-UpdatePlainPath $directory.FullName
            $installed = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
            if ($modId -cnotmatch '^[a-z0-9]+([.-][a-z0-9]+)*$' -or $installed.id -cne $modId -or
                $installed.environment -notin @('server','both')) { throw 'Invalid installed server mod manifest.' }
            if (-not ($installed.PSObject.Properties.Name -contains 'update')) { continue }
            $repository = [string]$installed.update.repository
            if ($installed.update.provider -cne 'github-releases' -or $repository -cnotmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') {
                throw 'Invalid mod update source.'
            }
            $null = ConvertTo-ReleaseVersion $installed.version
            $result.checked++
            $work = Resolve-UpdatePath $Win64 ("Briefcase/Updates/" + [guid]::NewGuid().ToString('N'))
            New-Item -ItemType Directory -Path $work -Force | Out-Null
            $releasePath = Join-Path $work 'release.json'
            Get-UpdateDownload "https://api.github.com/repos/$repository/releases/latest" $releasePath $config.timeoutSeconds 2097152
            $asset = Select-ModUpdateAsset (Get-Content -LiteralPath $releasePath -Raw | ConvertFrom-Json) $repository $installed.version 'windows-x64'
            if (-not $asset) { $result.current++; $result.mods += [pscustomobject]@{id=$modId;state='current';version=$installed.version}; continue }
            $archive = Join-Path $work 'release.zip'
            Get-UpdateDownload $asset.url $archive $config.timeoutSeconds $asset.size
            if ((Get-Item -LiteralPath $archive).Length -ne $asset.size -or
                (Get-FileHash -LiteralPath $archive).Hash -ine $asset.sha256) { throw 'Mod release archive digest mismatch.' }
            $stage = Join-Path $work 'stage'; Expand-UpdateArchive $archive $stage
            $package = Read-ModUpdatePackage $stage $modId $asset.version $repository 'windows-x64'
            Install-ModUpdatePackage $Win64 $stage $package ([guid]::NewGuid().ToString('N'))
            $result.updated++; $result.mods += [pscustomobject]@{id=$modId;state='updated';version=$asset.version}
        } catch {
            Restore-ModUpdateTransaction $Win64
            $result.failed++; $result.mods += [pscustomobject]@{id=$modId;state='failed';message=$_.Exception.Message}
            Write-Warning "Mod update unavailable for $modId; installed version retained. $($_.Exception.Message)"
        }
    }
    return [pscustomobject]$result
}

function Invoke-ServerUpdate([string]$Win64) {
    # Caller holds launch.lock through startup. Recovery is mandatory even if updates have been disabled.
    Restore-UpdateTransaction $Win64
    $configPath = Resolve-UpdatePath $Win64 'Briefcase/updater.json'
    $installStarted = $false
    try {
        # Fresh ZIP installations need no manual configuration. Existing settings,
        # including an explicit opt-out or a custom repository, belong to the user.
        if (-not (Test-Path -LiteralPath $configPath)) {
            Write-UpdateJson $configPath @{schemaVersion=1;enabled=$true;updateMods=$true;repository='EnoPM/BriefcaseNative';timeoutSeconds=20}
        }
        $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
        if ($config.schemaVersion -ne 1 -or $config.enabled -isnot [bool] -or $config.timeoutSeconds -lt 1 -or $config.timeoutSeconds -gt 120) {
            throw 'Invalid updater configuration.'
        }
        if (-not $config.enabled) { return 'disabled' }
        if (-not $config.repository) { return 'not-configured' }
        if ($config.repository -cnotmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { throw 'Expected GitHub owner/repository.' }
        $build = Get-Content -LiteralPath (Resolve-UpdatePath $Win64 'Briefcase/Updater/build.json') -Raw | ConvertFrom-Json
        $id = [guid]::NewGuid().ToString('N')
        $work = Resolve-UpdatePath $Win64 "Briefcase/Updates/$id"
        New-Item -ItemType Directory -Path $work -Force | Out-Null
        $metadata = Join-Path $work 'release.json'
        Get-UpdateDownload "https://api.github.com/repos/$($config.repository)/releases/latest" $metadata $config.timeoutSeconds 2097152
        $release = Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
        $asset = Select-UpdateAsset $release $config.repository $build.frameworkVersion
        if (-not $asset) { return 'current' }
        $archive = Join-Path $work 'release.zip'
        Get-UpdateDownload $asset.url $archive $config.timeoutSeconds $asset.size
        if ((Get-Item -LiteralPath $archive).Length -ne $asset.size -or
            (Get-FileHash -LiteralPath $archive).Hash -ine $asset.sha256) { throw 'Release archive digest mismatch.' }
        $stage = Join-Path $work 'stage'
        Expand-UpdateArchive $archive $stage
        $gameHash = (Get-FileHash -LiteralPath (Join-Path $Win64 'DeceiveIncServer-Win64-Shipping.exe')).Hash
        $manifest = Read-UpdatePackage $stage $asset.version $gameHash
        $installStarted = $true
        Install-UpdatePackage $Win64 $stage $manifest $id
        return "installed $($asset.version)"
    } catch {
        # A failed rollback is fatal. Never start a partially updated set of DLLs.
        if ($installStarted) { Restore-UpdateTransaction $Win64 }
        Write-Warning "Briefcase update unavailable; keeping installed version. $($_.Exception.Message)"
        return 'failed-kept-installed'
    }
}
