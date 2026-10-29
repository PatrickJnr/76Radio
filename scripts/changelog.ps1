param(
    [Parameter(Mandatory = $true)]
    [string]$Version
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$path = Join-Path $repoRoot 'CHANGELOG.md'
if (-not (Test-Path -LiteralPath $path)) {
    throw "CHANGELOG.md not found at $path"
}

$lines = @(Get-Content -LiteralPath $path)
$heading = '^##\s+' + [regex]::Escape($Version) + '\s*$'
$start = -1
for ($index = 0; $index -lt $lines.Count; $index++) {
    if ($lines[$index] -match $heading) {
        $start = $index
        break
    }
}

if ($start -lt 0) {
    throw "CHANGELOG.md has no section for $Version"
}

$end = $lines.Count
for ($index = $start + 1; $index -lt $lines.Count; $index++) {
    if ($lines[$index] -match '^##\s+') {
        $end = $index
        break
    }
}

$body = (@($lines[($start + 1)..($end - 1)]) -join "`n").Trim()
if (-not $body) {
    throw "CHANGELOG.md section for $Version is empty"
}

$body
