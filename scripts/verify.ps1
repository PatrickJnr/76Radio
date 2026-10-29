param(
    [string]$GameRoot
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot
$failures = 0
$warnings = 0

function Check {
    param([string]$Text, [bool]$Condition, [string]$Level = 'fail')
    if ($Condition) {
        Write-Result $Text 'ok'
    } elseif ($Level -eq 'warn') {
        Write-Result $Text 'warn'
        $script:warnings++
    } else {
        Write-Result $Text 'fail'
        $script:failures++
    }
}

Write-Section 'Repository'
Check -Text 'build manifest present' -Condition (Test-Path -LiteralPath (Join-Path $repoRoot 'dist\build-manifest.json'))

$build = $null
if (Test-Path -LiteralPath (Join-Path $repoRoot 'dist\build-manifest.json')) {
    $build = Read-BuildManifest -RepoRoot $repoRoot
    foreach ($artifact in $build.artifacts | Where-Object { $_.relativeTarget }) {
        $path = Join-Path $repoRoot ('dist\' + $artifact.name)
        $hashOk = (Test-Path -LiteralPath $path) -and ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $artifact.sha256)
        Check -Text "artifact hash $($artifact.name)" -Condition $hashOk
    }
    $verifyOutput = & python (Join-Path $repoRoot 'tools\ba2\ba2pack.py') verify (Join-Path $repoRoot 'dist\76Radio.ba2') 2>&1
    Check -Text 'BA2 round trip verified' -Condition (($verifyOutput -join "`n") -match 'verified 2 of 2')
}

Write-Section 'Game installation'
$resolvedRoot = $null
try {
    $resolvedRoot = Assert-GameRoot -GameRoot (Resolve-GameRoot -GameRoot $GameRoot)
    Check -Text "game root detected: $resolvedRoot" -Condition $true
} catch {
    Check -Text $_.Exception.Message -Condition $false
}

if ($resolvedRoot) {
    $xscal = Get-XScalState -GameRoot $resolvedRoot
    Check -Text 'dxgi.dll module present' -Condition $xscal.dxgiPresent
    Check -Text ("module identity: {0}" -f $xscal.extenderKind) -Condition ($xscal.extenderKind -eq 'xscal')
    Check -Text ("extender version: {0}" -f $xscal.extenderVersion) -Condition ([bool]$xscal.extenderVersion) -Level 'warn'
    Check -Text ("executable version: {0}" -f $xscal.executableVersion) -Condition ([bool]$xscal.executableVersion)
    Check -Text ("address library file: {0}" -f $xscal.addressLibraryFile) -Condition $xscal.addressLibraryMatchesExecutable -Level 'warn'
    Check -Text ("address library loaded: {0}" -f $xscal.addressLibraryLoaded) -Condition $xscal.addressLibraryLoaded -Level 'warn'
    Check -Text 'xScal log readable' -Condition $xscal.logPresent -Level 'warn'
    Check -Text (("root level modules present: {0}") -f ($(if ($xscal.proxyInventory.Count -gt 0) { $xscal.proxyInventory -join ', ' } else { 'none' }))) -Condition $true

    $surfaceArtifact = $null
    if ($build) {
        $surfaceArtifact = $build.artifacts | Where-Object { $_.kind -eq 'surface-module' } | Select-Object -First 1
    }
    $surfaceModulePath = Join-Path $resolvedRoot 'version.dll'
    if (Test-Path -LiteralPath $surfaceModulePath) {
        $surfaceHash = (Get-FileHash -LiteralPath $surfaceModulePath -Algorithm SHA256).Hash
        $surfaceMatches = [bool]$surfaceArtifact -and ($surfaceHash -eq $surfaceArtifact.sha256)
        Check -Text (("surface module installed: version.dll, matches the build: {0}") -f $(if ($surfaceMatches) { 'yes' } else { 'no' })) -Condition $surfaceMatches
    } else {
        Check -Text 'surface module not installed: version.dll is absent, the panel reports bridge offline' -Condition $true -Level 'warn'
    }

    $surfaceLogPath = Join-Path $resolvedRoot 'Data\XSCAL\76Radio\76Radio.log'
    if (Test-Path -LiteralPath $surfaceLogPath) {
        $lastLines = Get-Content -LiteralPath $surfaceLogPath -Tail 3
        Check -Text ('surface module log tail: ' + ($lastLines -join ' | ')) -Condition $true
    } else {
        Check -Text 'surface module log absent, the module has not run in this installation yet' -Condition $false -Level 'warn'
    }

    Write-Section 'Storage channel'
    $probePath = Join-Path $resolvedRoot 'Data\modsdata\76radio\r76radio.probe.json'
    $statePath = Join-Path $resolvedRoot 'Data\modsdata\r76radio.state.json'
    $commandPath = Join-Path $resolvedRoot 'Data\modsdata\r76radio.command.json'
    Check -Text (("channel probe document present: {0}") -f $(if (Test-Path -LiteralPath $probePath) { 'yes' } else { 'no' })) -Condition (Test-Path -LiteralPath $probePath)
    Check -Text (("module state document present: {0}") -f $(if (Test-Path -LiteralPath $statePath) { 'yes' } else { 'no, the surface module is not loaded' })) -Condition (Test-Path -LiteralPath $statePath) -Level 'warn'
    if (Test-Path -LiteralPath $commandPath) {
        $document = Get-Content -LiteralPath $commandPath -Raw
        Write-Result ("last command document: {0}" -f $document.Trim())
    }

    $manifest = Read-DeploymentManifest -GameRoot $resolvedRoot
    if ($manifest) {
        Write-Section 'Deployment'
        foreach ($file in $manifest.files) {
            $target = Join-Path $resolvedRoot ($file.relativeTarget -replace '\\', '\')
            $present = Test-Path -LiteralPath $target
            if ($file.kind -eq 'settings') {
                $state = if ($present) { 'present, defaults apply when entries are missing' } else { 'missing, the module uses its defaults' }
                Check -Text "settings $($file.relativeTarget): $state" -Condition $present -Level 'warn'
                continue
            }
            if ($file.kind -eq 'stations') {
                $state = if ($present) { 'present, player edits are kept' } else { 'missing, no custom stations are listed' }
                Check -Text "stations $($file.relativeTarget): $state" -Condition $present -Level 'warn'
                continue
            }
            $same = $present -and ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq $file.sha256)
            Check -Text "installed $($file.relativeTarget)" -Condition $same
        }
        if ($manifest.ini -and $manifest.ini.path -and (Test-Path -LiteralPath $manifest.ini.path)) {
            $count = Get-ArchiveListCount -IniPath $manifest.ini.path -ArchiveName $manifest.ini.archiveEntry
            Check -Text 'archive registered exactly once' -Condition ($count -eq 1)
        }
    } else {
        Write-Result '76Radio is not deployed in this installation' 'warn'
        $warnings++
    }
}

Write-Section 'Spotify'
$spotify = Get-Process -Name 'Spotify' -ErrorAction SilentlyContinue
Check -Text 'Spotify process running' -Condition ([bool]$spotify) -Level 'warn'

Write-Host ''
if ($failures -eq 0) {
    Write-Host ("Verification passed with {0} warning(s)" -f $warnings) -ForegroundColor Green
    exit 0
}
Write-Host ("Verification failed: {0} failure(s), {1} warning(s)" -f $failures, $warnings) -ForegroundColor Red
exit 1
