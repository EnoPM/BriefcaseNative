[CmdletBinding(DefaultParameterSetName='Recovery')]
param(
    [Parameter(Mandatory,ParameterSetName='Recovery')][string]$PasswordFile,
    [Parameter(Mandatory,ParameterSetName='Server')][string]$ServerConfig
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$inputFile=if($PSCmdlet.ParameterSetName -eq 'Server'){$ServerConfig}else{$PasswordFile}
$path=[IO.Path]::GetFullPath($inputFile)
$item=Get-Item -LiteralPath $path -Force
if($item.Length -gt 16384 -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){
    throw 'Invalid protected password file.'
}
try {$data=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json}
catch {throw 'Invalid password configuration.'}
if($PSCmdlet.ParameterSetName -eq 'Server'){
    if($data.version -ne 2 -or $data.password -isnot [string] -or
       [Text.Encoding]::UTF8.GetByteCount($data.password) -lt 12 -or
       [Text.Encoding]::UTF8.GetByteCount($data.password) -gt 256){throw 'Invalid configured password.'}
    Set-Clipboard -Value $data.password
    $data.password=$null
    Write-Host 'Mot de passe copie. Vous pouvez le coller dans Briefcase.'
    return
}
Add-Type -AssemblyName System.Security
if($data.version -ne 1 -or $data.protectedPassword -cnotmatch '^([0-9a-f]{2}){1,8192}$'){
    throw 'Invalid protected password format.'
}
$encrypted=New-Object byte[] ($data.protectedPassword.Length/2)
for($i=0;$i -lt $encrypted.Length;$i++){$encrypted[$i]=[Convert]::ToByte($data.protectedPassword.Substring($i*2,2),16)}
$plain=$null
try {
    $plain=[Security.Cryptography.ProtectedData]::Unprotect($encrypted,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
    # Only this explicit local action copies the password; it is never printed in console output.
    Set-Clipboard -Value ([Text.Encoding]::UTF8.GetString($plain))
    Write-Host 'Mot de passe copie. Vous pouvez le coller dans Briefcase.'
} finally {
    if($null -ne $plain){[Array]::Clear($plain,0,$plain.Length)}
    [Array]::Clear($encrypted,0,$encrypted.Length)
}
