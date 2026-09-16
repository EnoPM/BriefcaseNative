param([string]$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')))
$value = [IO.File]::ReadAllText((Join-Path $ProjectRoot 'VERSION')).Trim()
if ($value -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
    throw 'VERSION must contain a stable MAJOR.MINOR.PATCH version.'
}
return $value
