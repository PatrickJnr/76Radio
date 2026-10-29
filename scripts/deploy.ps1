param(
    [string]$GameRoot,
    [switch]$WhatIf,
    [string]$AllowRootModule
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot
$resolvedRoot = Assert-GameRoot -GameRoot (Resolve-GameRoot -GameRoot $GameRoot)

Write-Section 'Fallout 76 target'
Write-Result "game root: $resolvedRoot"
$exe = Get-Item -LiteralPath (Join-Path $resolvedRoot 'Fallout76.exe')
Write-Result ("executable: {0:N0} bytes, modified {1}" -f $exe.Length, $exe.LastWriteTimeUtc.ToString('u'))

Assert-GameStopped
Write-Result 'game process not running' 'ok'

Write-Section 'Extender status'
$xscal = Get-XScalState -GameRoot $resolvedRoot
if (-not $xscal.dxgiPresent) {
    throw 'No extender was found beside Fallout76.exe. Install xScal before deploying 76Radio.'
}
if ($xscal.extenderKind -ne 'xscal') {
    throw "The module beside Fallout76.exe identifies itself as $($xscal.extenderKind), not xScal. 76Radio.deploy stops rather than replacing another extender."
}
Write-Result 'dxgi.dll present and identified as xScal' 'ok'
Write-Result ("xScal version: {0}" -f ($(if ($xscal.extenderVersion) { $xscal.extenderVersion } else { 'unknown' }))) $(if ($xscal.extenderVersion) { 'ok' } else { 'warn' })
Write-Result ("executable version: {0}" -f ($(if ($xscal.executableVersion) { $xscal.executableVersion } else { 'unknown' })))
Write-Result ("game runtime reported by the log: {0}" -f ($(if ($xscal.gameVersion) { $xscal.gameVersion } else { 'unknown' })))
Write-Result ("address library for this executable: {0}" -f ($(if ($xscal.addressLibraryFile) { $xscal.addressLibraryFile } else { 'missing' }))) $(if ($xscal.addressLibraryMatchesExecutable) { 'ok' } else { 'warn' })
Write-Result ("plugin directory: {0}" -f $xscal.pluginsDirectory)
Write-Result ("root level modules present: {0}" -f ($(if ($xscal.proxyInventory.Count -gt 0) { $xscal.proxyInventory -join ', ' } else { 'none' })))
$build = Read-BuildManifest -RepoRoot $repoRoot
Write-Section 'Build artifacts'
foreach ($artifact in $build.artifacts) {
    $path = Join-Path $repoRoot ('dist\' + $artifact.name)
    if ($artifact.relativeTarget -and -not (Test-Path -LiteralPath $path)) {
        throw "Missing build artifact $($artifact.name)"
    }
    if ($artifact.relativeTarget) {
        $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ($actual -ne $artifact.sha256) {
            throw "Artifact hash mismatch for $($artifact.name). Rebuild before deploying."
        }
        Write-Result "$($artifact.name) verified" 'ok'
    }
}

Write-Section 'Module deployment rules'
$moduleArtifacts = @($build.artifacts | Where-Object { $_.relativeTarget -and $_.name -match '\.dll$' })
foreach ($artifact in $moduleArtifacts) {
    $relative = $artifact.relativeTarget -replace '\\', '\'
    if ($relative -notmatch '\\') {
        if ($AllowRootModule -ine $artifact.name) {
            throw "Refusing to deploy $($artifact.name) into the game root. A root level module loads inside the game process and competes with the installed extender. Pass -AllowRootModule $($artifact.name) only if that is intended."
        }
        Write-Result "root level module $($artifact.name) approved on the command line" 'warn'
    }
}

$previousManifest = Read-DeploymentManifest -GameRoot $resolvedRoot
$backupRoot = Join-Path (Join-Path $resolvedRoot 'Data\XSCAL\76Radio') 'backup'
$timestamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')

Write-Section 'Conflict checks'
$conflicts = @()
$archiveArtifacts = @($build.artifacts | Where-Object { $_.relativeTarget })
foreach ($artifact in $archiveArtifacts) {
    $target = Join-Path $resolvedRoot ($artifact.relativeTarget -replace '\\', '\')
    $owned = $false
    if ($previousManifest -and $previousManifest.files) {
        $owned = @($previousManifest.files | Where-Object { $_.relativeTarget -eq $artifact.relativeTarget }).Count -gt 0
    }
    if ((Test-Path -LiteralPath $target) -and -not $owned) {
        $conflicts += $target
    }
}
if ($conflicts.Count -gt 0) {
    foreach ($conflict in $conflicts) {
        Write-Result "conflict: $conflict exists and is not owned by 76Radio" 'fail'
    }
    throw 'Deployment stopped because a target file is not owned by a previous 76Radio deployment.'
}
Write-Result 'no conflicting files' 'ok'

Write-Section 'Custom ini'
$iniPath = Get-CustomIniPath -GameRoot $resolvedRoot
if ($iniPath) {
    Write-Result "configuration: $iniPath"
    $entries = Get-ArchiveListEntries -IniPath $iniPath
    Write-Result ("archive entries present: {0}" -f $entries.Count)
} else {
    Write-Result 'no Fallout76Custom.ini found; archive registration will be skipped' 'warn'
}

if ($WhatIf) {
    Write-Section 'Dry run'
    foreach ($artifact in $build.artifacts | Where-Object { $_.relativeTarget }) {
        Write-Result "would install $($artifact.relativeTarget)"
    }
    if ($iniPath) {
        Write-Result 'would register 76Radio.ba2 in sResourceArchive2List if absent'
    }
    Write-Result 'no files were written'
    return
}

Write-Section 'Backup and install'
$installed = @()
foreach ($artifact in $build.artifacts | Where-Object { $_.relativeTarget }) {
    $target = Join-Path $resolvedRoot ($artifact.relativeTarget -replace '\\', '\')
    $relative = $artifact.relativeTarget
    $record = [ordered]@{
        relativeTarget = $relative
        kind = $artifact.kind
        sha256 = $artifact.sha256
        backup = $null
    }
    $previousRecord = $null
    if ($previousManifest -and $previousManifest.files) {
        $previousRecord = @($previousManifest.files | Where-Object { $_.relativeTarget -eq $relative }) | Select-Object -First 1
    }
    if (Test-Path -LiteralPath $target) {
        if ($previousRecord) {
            $record.backup = $previousRecord.backup
        } else {
            $backupPath = Join-Path $backupRoot ($relative -replace '\\', '_')
            if (-not (Test-Path -LiteralPath $backupRoot)) {
                New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
            }
            Copy-Item -LiteralPath $target -Destination $backupPath -Force
            $record.backup = $backupPath
        }
    }
    $directory = Split-Path -Parent $target
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    $source = Join-Path $repoRoot ('dist\' + $artifact.name)
    Copy-Item -LiteralPath $source -Destination $target -Force
    $installedHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if ($installedHash -ne $artifact.sha256) {
        throw "Installed file verification failed for $target"
    }
    Write-Result "installed $relative" 'ok'
    $installed += $record
}

Write-Section 'Storage channel'
$storageDirectory = Join-Path $resolvedRoot 'Data\modsdata'
if (-not (Test-Path -LiteralPath $storageDirectory)) {
    New-Item -ItemType Directory -Path $storageDirectory -Force | Out-Null
    Write-Result 'created Data\modsdata' 'ok'
} else {
    Write-Result 'Data\modsdata present' 'ok'
}
$moduleDataRelative = 'Data\modsdata\76radio'
$moduleDataDirectory = Join-Path $resolvedRoot ($moduleDataRelative -replace '\\', '\')
if (-not (Test-Path -LiteralPath $moduleDataDirectory)) {
    New-Item -ItemType Directory -Path $moduleDataDirectory -Force | Out-Null
    Write-Result 'created Data\modsdata\76radio' 'ok'
} else {
    Write-Result 'Data\modsdata\76radio present' 'ok'
}
$probeRelative = 'Data\modsdata\76radio\r76radio.probe.json'
$probeTarget = Join-Path $resolvedRoot ($probeRelative -replace '\\', '\')
$probeRecord = $null
if ($previousManifest -and $previousManifest.files) {
    $probeRecord = @($previousManifest.files | Where-Object { $_.relativeTarget -eq $probeRelative }) | Select-Object -First 1
}
if (-not (Test-Path -LiteralPath $probeTarget) -or $probeRecord) {
    $probeDocument = '{"probe":1,"build":"' + $build.version + '"}'
    $probeEncoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($probeTarget, $probeDocument, $probeEncoding)
    $probeHash = (Get-FileHash -LiteralPath $probeTarget -Algorithm SHA256).Hash
    $installed += [ordered]@{
        relativeTarget = $probeRelative
        kind = 'channel-probe'
        sha256 = $probeHash
        backup = $null
    }
    Write-Result 'probe document written' 'ok'
} else {
    Write-Result 'probe document already present and not owned, left untouched' 'warn'
}

Write-Section 'Custom stations list'

$stationsRelative = 'Data\modsdata\76radio\stations.json'
$stationsTarget = Join-Path $resolvedRoot ($stationsRelative -replace '\\', '\')
$stationsRecord = $null
if ($previousManifest -and $previousManifest.files) {
    $stationsRecord = @($previousManifest.files | Where-Object { $_.relativeTarget -eq $stationsRelative }) | Select-Object -First 1
}
if (-not (Test-Path -LiteralPath $stationsTarget)) {
    $stationsTemplate = Join-Path $repoRoot 'dist\stations.json'
    if (-not (Test-Path -LiteralPath $stationsTemplate)) {
        throw 'The custom stations template is missing from dist.'
    }
    Copy-Item -LiteralPath $stationsTemplate -Destination $stationsTarget -Force
    $installed += [ordered]@{
        relativeTarget = $stationsRelative
        kind = 'stations'
        sha256 = (Get-FileHash -LiteralPath $stationsTarget -Algorithm SHA256).Hash
        backup = $null
    }
    Write-Result "installed $stationsRelative, edit it to add stations" 'ok'
} elseif ($stationsRecord) {
    $installed += [ordered]@{
        relativeTarget = $stationsRelative
        kind = 'stations'
        sha256 = $stationsRecord.sha256
        backup = $stationsRecord.backup
    }
    Write-Result 'stations list present and owned, left untouched' 'ok'
} else {
    Write-Result 'stations list present and not owned by 76Radio, left untouched' 'warn'
}

Write-Section 'Legacy channel files'

$legacyState = Join-Path $storageDirectory 'r76radio.state.json'
$legacyOurs = $false
if (Test-Path -LiteralPath $legacyState) {
    $legacyText = [System.IO.File]::ReadAllText($legacyState)
    if ($legacyText -match '"host"\s*:\s*"76radio"') {
        $legacyOurs = $true
    }
}
if ($legacyOurs) {
    foreach ($name in @('r76radio.state.json', 'r76radio.art.png', 'r76radio.art.jpg', 'r76radio.art.pixels')) {
        $legacyPath = Join-Path $storageDirectory $name
        if (Test-Path -LiteralPath $legacyPath) {
            Remove-Item -LiteralPath $legacyPath -Force
            Write-Result "removed the old Data\modsdata\$name" 'ok'
        }
    }
} else {
    Write-Result 'no old state document to clean up' 'ok'
}
foreach ($legacy in @($previousManifest.files | Where-Object {
            $_.kind -eq 'channel-probe' -and $_.relativeTarget -ne $probeRelative })) {
    $legacyPath = Join-Path $resolvedRoot ($legacy.relativeTarget -replace '\\', '\')
    if (Test-Path -LiteralPath $legacyPath) {
        $legacyHash = (Get-FileHash -LiteralPath $legacyPath -Algorithm SHA256).Hash
        if ($legacyHash -eq $legacy.sha256) {
            Remove-Item -LiteralPath $legacyPath -Force
            Write-Result "removed the old $($legacy.relativeTarget)" 'ok'
        } else {
            Write-Result "old probe changed since deployment, left in place: $($legacy.relativeTarget)" 'warn'
        }
    }
}

Write-Section 'Loose font cleanup'
$fontRelative = 'Data\interface\fonts_ja.swf'
$fontTarget = Join-Path $resolvedRoot ($fontRelative -replace '\\', '\')
$fontRecord = $null
if ($previousManifest -and $previousManifest.files) {
    $fontRecord = @($previousManifest.files | Where-Object { $_.relativeTarget -eq $fontRelative }) | Select-Object -First 1
}
if ($fontRecord -and (Test-Path -LiteralPath $fontTarget)) {
    $looseHash = (Get-FileHash -LiteralPath $fontTarget -Algorithm SHA256).Hash
    if ($looseHash -eq $fontRecord.sha256) {
        Remove-Item -LiteralPath $fontTarget -Force
        Write-Result 'removed the loose fonts_ja.swf left by the previous build' 'ok'
    } else {
        Write-Result 'loose fonts_ja.swf changed since deployment, left in place' 'warn'
    }
} else {
    Write-Result 'no loose font file owned by 76Radio' 'ok'
}

Write-Section 'Settings file'
$settingsRelative = 'Data\XSCAL\76Radio\76Radio.ini'
$settingsTarget = Join-Path $resolvedRoot ($settingsRelative -replace '\\', '\')
$settingsTemplate = Join-Path $repoRoot ('dist\' + $build.settingsTemplate)
$settingsRecord = $null
if ($previousManifest -and $previousManifest.files) {
    $settingsRecord = @($previousManifest.files | Where-Object { $_.relativeTarget -eq $settingsRelative }) | Select-Object -First 1
}
if (-not (Test-Path -LiteralPath $settingsTarget)) {
    if (-not $build.settingsTemplate -or -not (Test-Path -LiteralPath $settingsTemplate)) {
        throw "Settings template missing: $settingsTemplate"
    }
    $settingsDirectory = Split-Path -Parent $settingsTarget
    if (-not (Test-Path -LiteralPath $settingsDirectory)) {
        New-Item -ItemType Directory -Path $settingsDirectory -Force | Out-Null
    }
    Copy-Item -LiteralPath $settingsTemplate -Destination $settingsTarget -Force
    $installed += [ordered]@{
        relativeTarget = $settingsRelative
        kind = 'settings'
        sha256 = (Get-FileHash -LiteralPath $settingsTarget -Algorithm SHA256).Hash
        backup = $null
    }
    Write-Result "installed $settingsRelative, edit it to change logging and timings" 'ok'
} elseif ($settingsRecord) {

    $installed += [ordered]@{
        relativeTarget = $settingsRelative
        kind = 'settings'
        sha256 = $settingsRecord.sha256
        backup = $settingsRecord.backup
    }
    Write-Result 'settings file present and owned, left untouched' 'ok'
} else {
    Write-Result 'settings file present and not owned by 76Radio, left untouched' 'warn'
}

Write-Section 'HUD loader entry'
$hudEntry = '76RadioHud'
$hudIniPath = Join-Path $resolvedRoot 'Data\hudmodloader.ini'
$hudEntryOwned = $false
$hudIniBackup = $null
if (Test-Path -LiteralPath $hudIniPath) {
    $hudText = [System.IO.File]::ReadAllText($hudIniPath)
    if ($hudText -notmatch '(?m)^\s*76RadioHud\s*$') {
        $hudIniBackup = $hudIniPath + '.76radio.' + $timestamp + '.bak'
        Copy-Item -LiteralPath $hudIniPath -Destination $hudIniBackup -Force
        $prefix = if ($hudText.EndsWith("`n")) { '' } else { "`r`n" }
        [System.IO.File]::AppendAllText(
            $hudIniPath,
            ($prefix + '76RadioHud' + "`r`n"),
            (New-Object System.Text.UTF8Encoding($false)))
        $hudEntryOwned = $true
        Write-Result 'registered 76RadioHud in hudmodloader.ini' 'ok'
    } elseif ($previousManifest -and $previousManifest.hudLoader -and $previousManifest.hudLoader.added) {

        $hudEntryOwned = $true
        $hudIniBackup = $previousManifest.hudLoader.backup
        Write-Result 'HUD loader entry present and owned' 'ok'
    } else {
        Write-Result 'HUD loader entry already present and not owned, left untouched' 'warn'
    }
} else {
    Write-Result 'hudmodloader.ini not found; the HUD relay will not load' 'warn'
}

$iniChanged = $false
$iniBackup = $null
if ($iniPath) {
    $iniBackup = $iniPath + '.76radio.' + $timestamp + '.bak'
    Copy-Item -LiteralPath $iniPath -Destination $iniBackup -Force
}
if ($iniPath -and @($build.artifacts | Where-Object { $_.requiresArchiveList }).Count -gt 0) {
    $state = Add-ArchiveListEntry -IniPath $iniPath -ArchiveName '76Radio.ba2'
    if ($state -eq 'added') {
        $iniChanged = $true
        Write-Result 'registered 76Radio.ba2 in sResourceArchive2List' 'ok'
    } else {
        Write-Result 'archive entry already present' 'ok'
    }
    $count = Get-ArchiveListCount -IniPath $iniPath -ArchiveName '76Radio.ba2'
    if ($count -ne 1) {
        throw 'Archive registration verification failed'
    }
}

$fontArchiveName = 'SeventySix - Interface_ja.ba2'
$fontArchiveAdded = $false
if ($iniPath) {
    $fontArchivePath = Join-Path $resolvedRoot ('Data\' + $fontArchiveName)
    if (Test-Path -LiteralPath $fontArchivePath) {
        $fontState = Add-ArchiveListEntry -IniPath $iniPath -ArchiveName $fontArchiveName
        if ($fontState -eq 'added') {
            $iniChanged = $true
            $fontArchiveAdded = $true
            Write-Result "registered $fontArchiveName in sResourceArchive2List" 'ok'
        } elseif ($previousManifest -and $previousManifest.ini -and
                  $previousManifest.ini.fontArchiveAdded -and
                  $previousManifest.ini.fontArchive -eq $fontArchiveName) {

            $fontArchiveAdded = $true
            Write-Result "$fontArchiveName present and owned" 'ok'
        } else {
            Write-Result "$fontArchiveName already present, left untouched" 'ok'
        }
        $fontCount = Get-ArchiveListCount -IniPath $iniPath -ArchiveName $fontArchiveName
        if ($fontCount -ne 1) {
            throw 'Japanese font archive registration verification failed'
        }
    } else {
        Write-Result "$fontArchiveName not found; CJK labels will use the fallback" 'warn'
    }
}

$manifest = [ordered]@{
    product = '76Radio'
    version = $build.version
    installedAt = (Get-Date).ToUniversalTime().ToString('o')
    gameRoot = $resolvedRoot
    xscalVersion = $xscal.version
    files = $installed
    ini = [ordered]@{
        path = $iniPath
        changed = $iniChanged
        backup = $iniBackup
        archiveEntry = '76Radio.ba2'
        fontArchive = $fontArchiveName
        fontArchiveAdded = $fontArchiveAdded
    }
    hudLoader = [ordered]@{
        path = $hudIniPath
        entry = $hudEntry
        added = $hudEntryOwned
        backup = $hudIniBackup
    }
}
$manifestPath = Save-DeploymentManifest -GameRoot $resolvedRoot -Manifest $manifest
Write-Result "manifest written to $manifestPath" 'ok'

Write-Section 'Installed'
foreach ($record in $installed) {
    Write-Host "  $($record.relativeTarget)"
}
if ($iniChanged) {
    Write-Host "  archive registration in $iniPath"
}
if ($fontArchiveAdded) {
    Write-Host "  $fontArchiveName registration in $iniPath"
}
Write-Host ''
Write-Host 'Deployment complete' -ForegroundColor Green
