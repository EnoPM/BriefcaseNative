[CmdletBinding()]
param([Parameter(Mandatory)][string]$Setup)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Security
$checks=0
function Check([bool]$Condition,[string]$Message) {
    $script:checks++
    if(-not $Condition){throw $Message}
}
$workspace=[IO.Path]::GetFullPath($PWD.Path)
$fixture=Join-Path $workspace ('admin-setup-fixture-'+[Guid]::NewGuid().ToString('N'))
$root=Join-Path $fixture 'Win64\Briefcase'
New-Item -ItemType Directory -Path $root -Force | Out-Null
New-Item -ItemType File -Path (Join-Path $fixture 'Win64\DeceiveIncServer-Win64-Shipping.exe') | Out-Null
$protected=Join-Path $fixture 'password.json'
$plain=$null
try {
    $arguments=@('--root',$root,'--listen','127.0.0.1','--port','50002','--endpoint','127.0.0.1:50002','--generate-password-file',$protected)
    $output=& $Setup @arguments 2>&1 | Out-String
    Check ($LASTEXITCODE -eq 0) 'Generated setup failed.'
    $data=Get-Content -LiteralPath $protected -Raw | ConvertFrom-Json
    Check ($data.version -eq 1) 'Password format version.'
    $encrypted=[Convert]::FromHexString($data.protectedPassword)
    $plain=[Security.Cryptography.ProtectedData]::Unprotect($encrypted,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
    $secret=[Text.Encoding]::UTF8.GetString($plain)
    Check ($secret -cmatch '^[0-9a-f]{64}$') 'Expected 256-bit generated password.'
    $server=Get-Content -LiteralPath (Join-Path $root 'Admin\server.json') -Raw
    $pairing=Get-Content -LiteralPath (Join-Path $root 'Admin\pairing.json') -Raw
    $saved=Get-Content -LiteralPath $protected -Raw
    Check (-not ($output+$pairing+$saved).Contains($secret)) 'Password was exposed.'
    $settings=$server | ConvertFrom-Json
    Check ($settings.version -eq 2 -and $settings.password -ceq $secret) 'Cleartext password missing from server configuration.'
    Check ($settings.protectedKey -cmatch '^([0-9a-f]{2})+$') 'TLS key protection was changed.'
    $serverAcl=Get-Acl -LiteralPath (Join-Path $root 'Admin\server.json')
    Check ($serverAcl.AreAccessRulesProtected -and $serverAcl.Access.Count -eq 2) 'Server password file permissions changed.'
    $acl=Get-Acl -LiteralPath $protected
    Check $acl.AreAccessRulesProtected 'Password file inherits permissions.'
    Check ($acl.Access.Count -eq 2) 'Unexpected password file permissions.'
    $before=(Get-FileHash -LiteralPath $protected).Hash
    $identityHash=(Get-FileHash -LiteralPath (Join-Path $root 'Admin\server.json')).Hash
    $duplicate=& $Setup @arguments 2>&1 | Out-String
    Check ($LASTEXITCODE -ne 0) 'Existing identity overwritten.'
    Check ((Get-FileHash -LiteralPath $protected).Hash -ceq $before) 'Existing password replaced.'
    Check ((Get-FileHash -LiteralPath (Join-Path $root 'Admin\server.json')).Hash -ceq $identityHash) 'Existing identity replaced.'
    $other=Join-Path $fixture 'Other\Win64\Briefcase'
    New-Item -ItemType Directory -Path $other -Force | Out-Null
    New-Item -ItemType File -Path (Join-Path $fixture 'Other\Win64\DeceiveIncServer-Win64-Shipping.exe') | Out-Null
    $failed=& $Setup --root $other --listen 127.0.0.1 --port 50002 --endpoint 127.0.0.1:50002 --generate-password-file $protected 2>&1 | Out-String
    Check ($LASTEXITCODE -ne 0) 'Existing password destination accepted.'
    Check (-not (Test-Path -LiteralPath (Join-Path $other 'Admin\server.json'))) 'Identity created without recoverable password.'
    Check ((Get-FileHash -LiteralPath $protected).Hash -ceq $before) 'Password file changed on rejected setup.'
    $generated=& $Setup --root $other --listen 127.0.0.1 --port 50002 --endpoint 127.0.0.1:50002 --generate-password 2>&1 | Out-String
    Check ($LASTEXITCODE -eq 0) 'Generation directly in server config failed.'
    $otherConfig=Get-Content -LiteralPath (Join-Path $other 'Admin\server.json') -Raw | ConvertFrom-Json
    Check ($otherConfig.password -cmatch '^[0-9a-f]{64}$') 'Expected generated password in server config.'
    Check (-not $generated.Contains($otherConfig.password)) 'Generated password printed by setup.'
    Check (@(Get-ChildItem -LiteralPath (Join-Path $other 'Admin') -File).Count -eq 2) 'Unnecessary recovery file generated.'
    "PASS $checks generated administration setup checks"
} finally {
    if($null -ne $plain){[Array]::Clear($plain,0,$plain.Length)}
    $secret=$null
    $resolved=[IO.Path]::GetFullPath($fixture)
    if([IO.Path]::GetDirectoryName($resolved) -ine $workspace -or [IO.Path]::GetFileName($resolved) -notlike 'admin-setup-fixture-*'){
        throw 'Unsafe fixture cleanup path.'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
