param(
    [string]$GameRoot,
    [switch]$KeepBackups
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$resolvedRoot = Assert-GameRoot -GameRoot (Resolve-GameRoot -GameRoot $GameRoot)

Write-Section 'Fallout 76 target'
Write-Result "game root: $resolvedRoot"

Assert-GameStopped
Write-Result 'game process not running' 'ok'

$manifest = Read-DeploymentManifest -GameRoot $resolvedRoot
if (-not $manifest) {
    Write-Result 'no 76Radio deployment manifest found; nothing owned by 76Radio is recorded' 'warn'
    return
}

if ($manifest.gameRoot -and $manifest.gameRoot -ne $resolvedRoot) {
    throw "The recorded deployment belongs to $($manifest.gameRoot), not $resolvedRoot. Refusing to remove files."
}

Write-Section 'Removing owned files'
$removed = 0
foreach ($file in $manifest.files) {
    $target = Join-Path $resolvedRoot ($file.relativeTarget -replace '\\', '\')
    if (-not (Test-Path -LiteralPath $target)) {
        Write-Result "already absent: $($file.relativeTarget)"
        continue
    }
    $currentHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if ($currentHash -ne $file.sha256) {
        Write-Result "modified since deployment, leaving in place: $($file.relativeTarget)" 'warn'
        continue
    }
    if ($file.backup -and (Test-Path -LiteralPath $file.backup)) {
        Copy-Item -LiteralPath $file.backup -Destination $target -Force
        Write-Result "restored previous file: $($file.relativeTarget)" 'ok'
    } else {
        Remove-Item -LiteralPath $target -Force
        Write-Result "removed $($file.relativeTarget)" 'ok'
    }
    $removed++
}

Write-Section 'Removing channel documents'
$modsDataDirectory = Join-Path $resolvedRoot 'Data\modsdata'

foreach ($name in @('r76radio.command.json', 'r76radio.diag.json', 'r76radio.relay.json')) {
    $documentPath = Join-Path $modsDataDirectory $name
    if (Test-Path -LiteralPath $documentPath) {
        Remove-Item -LiteralPath $documentPath -Force
        Write-Result "removed Data\modsdata\$name" 'ok'
    }
}
$moduleDataDirectory = Join-Path $modsDataDirectory '76radio'
foreach ($name in @('r76radio.state.json', 'r76radio.probe.json', 'r76radio.art.png', 'r76radio.art.jpg', 'r76radio.art.pixels')) {
    $documentPath = Join-Path $moduleDataDirectory $name
    if (Test-Path -LiteralPath $documentPath) {
        Remove-Item -LiteralPath $documentPath -Force
        Write-Result "removed Data\modsdata\76radio\$name" 'ok'
    }
}

if ((Test-Path -LiteralPath $moduleDataDirectory) -and
    @(Get-ChildItem -LiteralPath $moduleDataDirectory -Force).Count -eq 0) {
    Remove-Item -LiteralPath $moduleDataDirectory -Force
    Write-Result 'removed the empty Data\modsdata\76radio folder' 'ok'
}

Write-Section 'Restoring archive configuration'
if ($manifest.ini -and $manifest.ini.path) {
    $iniPath = $manifest.ini.path
    if (Test-Path -LiteralPath $iniPath) {
        $state = Remove-ArchiveListEntry -IniPath $iniPath -ArchiveName $manifest.ini.archiveEntry
        if ($state -eq 'removed') {
            Write-Result 'removed 76Radio.ba2 from sResourceArchive2List' 'ok'
        } else {
            Write-Result 'archive entry was not present'
        }
        if ($manifest.ini.fontArchiveAdded -and $manifest.ini.fontArchive) {
            $fontState = Remove-ArchiveListEntry -IniPath $iniPath -ArchiveName $manifest.ini.fontArchive
            if ($fontState -eq 'removed') {
                Write-Result "removed $($manifest.ini.fontArchive) from sResourceArchive2List" 'ok'
            } else {
                Write-Result 'Japanese font archive entry was not present'
            }
        }
    } else {
        Write-Result "configuration file missing: $iniPath" 'warn'
    }
}

Write-Section 'Removing HUD loader entry'
if ($manifest.hudLoader -and $manifest.hudLoader.added -and $manifest.hudLoader.path) {
    $hudIniPath = $manifest.hudLoader.path
    if (Test-Path -LiteralPath $hudIniPath) {
        $hudText = [System.IO.File]::ReadAllText($hudIniPath)
        $updated = [regex]::Replace($hudText, '(?m)^\s*76RadioHud\s*\r?\n?', '')
        if ($updated -ne $hudText) {
            [System.IO.File]::WriteAllText(
                $hudIniPath,
                $updated,
                (New-Object System.Text.UTF8Encoding($false)))
            Write-Result 'removed 76RadioHud from hudmodloader.ini' 'ok'
        } else {
            Write-Result 'HUD loader entry was not present'
        }
    } else {
        Write-Result "HUD loader file missing: $hudIniPath" 'warn'
    }
} else {
    Write-Result 'no HUD loader entry owned by this deployment'
}

$stateDirectory = Join-Path $resolvedRoot 'Data\XSCAL\76Radio'
if ($KeepBackups) {
    $manifestPath = Get-ManifestPath -GameRoot $resolvedRoot
    if (Test-Path -LiteralPath $manifestPath) {
        Remove-Item -LiteralPath $manifestPath -Force
    }
    Write-Result "backups retained in $stateDirectory" 'warn'
} elseif (Test-Path -LiteralPath $stateDirectory) {
    Remove-Item -LiteralPath $stateDirectory -Recurse -Force
    Write-Result 'removed 76Radio state directory' 'ok'
}

Write-Host ''
Write-Host ("Uninstall complete, {0} file action(s)" -f $removed) -ForegroundColor Green
