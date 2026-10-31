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

    $pluginArtifact = $null
    $signatureArtifact = $null
    if ($build) {
        $pluginArtifact = $build.artifacts | Where-Object { $_.kind -eq 'xscal-plugin' } | Select-Object -First 1
        $signatureArtifact = $build.artifacts | Where-Object { $_.kind -eq 'xscal-plugin-signature' } | Select-Object -First 1
    }
    $pluginPath = Join-Path $resolvedRoot 'Data\XSCAL\Plugins\76Radio.dll'
    if (Test-Path -LiteralPath $pluginPath) {
        $pluginHash = (Get-FileHash -LiteralPath $pluginPath -Algorithm SHA256).Hash
        $pluginMatches = [bool]$pluginArtifact -and ($pluginHash -eq $pluginArtifact.sha256)
        Check -Text (("plugin installed: Data\XSCAL\Plugins\76Radio.dll, matches the build: {0}") -f $(if ($pluginMatches) { 'yes' } else { 'no' })) -Condition $pluginMatches
        $signaturePath = $pluginPath + '.sig'
        $signaturePresent = Test-Path -LiteralPath $signaturePath
        $signatureMatches = $signaturePresent -and [bool]$signatureArtifact -and
            ((Get-FileHash -LiteralPath $signaturePath -Algorithm SHA256).Hash -eq $signatureArtifact.sha256)
        Check -Text 'plugin signature installed and matches the build' -Condition $signatureMatches
    } else {
        Check -Text 'plugin not installed: Data\XSCAL\Plugins\76Radio.dll is absent, the panel reports bridge offline' -Condition $false -Level 'warn'
    }
    Check -Text 'legacy version.dll host absent from the game folder' -Condition (-not (Test-Path -LiteralPath (Join-Path $resolvedRoot 'version.dll'))) -Level 'warn'

    $xscalLogPath = Join-Path $resolvedRoot 'xscal.log'
    if (Test-Path -LiteralPath $xscalLogPath) {
        $pluginLines = @(Select-String -LiteralPath $xscalLogPath -Pattern '76Radio' | Select-Object -Last 3)
        $pluginLoaded = @($pluginLines | Where-Object { $_.Line -match '76Radio.*loaded' }).Count -gt 0
        $pluginRejected = @($pluginLines | Where-Object { $_.Line -match 'rejected|Rejected' }).Count -gt 0
        Check -Text (("xScal log names the plugin: {0}") -f $(if ($pluginLines.Count -gt 0) { 'yes' } else { 'no' })) -Condition ($pluginLines.Count -gt 0) -Level 'warn'
        Check -Text 'plugin loaded by xScal' -Condition $pluginLoaded -Level 'warn'
        if ($pluginRejected) {
            $pluginLines | ForEach-Object { Check -Text ('xScal: ' + $_.Line.Trim()) -Condition $false -Level 'warn' }
        }
    } else {
        Check -Text 'xscal.log absent, the extender has not run in this installation yet' -Condition $false -Level 'warn'
    }

    $pluginLogPath = Join-Path $resolvedRoot 'Data\XSCAL\76Radio\76Radio.log'
    if (Test-Path -LiteralPath $pluginLogPath) {
        $lastLines = Get-Content -LiteralPath $pluginLogPath -Tail 3
        Check -Text ('plugin log tail: ' + ($lastLines -join ' | ')) -Condition $true
    } else {
        Check -Text 'plugin log absent, the module has not run in this installation yet' -Condition $false -Level 'warn'
    }

    Write-Section 'Storage channel'
    $probePath = Join-Path $resolvedRoot 'Data\modsdata\76radio\r76radio.probe.json'
    $statePath = Join-Path $resolvedRoot 'Data\modsdata\76radio\r76radio.state.json'
    $commandPath = Join-Path $resolvedRoot 'Data\modsdata\r76radio.command.json'
    Check -Text (("channel probe document present: {0}") -f $(if (Test-Path -LiteralPath $probePath) { 'yes' } else { 'no' })) -Condition (Test-Path -LiteralPath $probePath)
    Check -Text (("module state document present: {0}") -f $(if (Test-Path -LiteralPath $statePath) { 'yes' } else { 'no, the plugin is not loaded' })) -Condition (Test-Path -LiteralPath $statePath) -Level 'warn'
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
