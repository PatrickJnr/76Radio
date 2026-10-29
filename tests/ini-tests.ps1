$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\scripts\common.ps1')

$failures = 0
$checks = 0

function Test-Condition {
    param([string]$Name, [bool]$Condition, $Actual)
    $script:checks++
    if ($Condition) {
        Write-Host "  [ok]   $Name"
    } else {
        Write-Host "  [fail] $Name (actual: $Actual)" -ForegroundColor Red
        $script:failures++
    }
}

$workRoot = Join-Path (Get-RepoRoot) 'work\ini-tests'
if (Test-Path -LiteralPath $workRoot) {
    Remove-Item -LiteralPath $workRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $workRoot -Force | Out-Null

$sample = @(
    '[Archive]',
    'sResourceArchiveList2 = PerkLoadoutManager-HML.ba2, EVB76Nevernude - Meshes.ba2',
    'sResourceArchive2List = 1259.ba2, MuteCampRoboShops.ba2, HUDModLoader.ba2',
    'bInvalidateOlderFiles=1',
    '',
    '[General]',
    'sIntroSequence=0'
) -join "`n"

$iniPath = Join-Path $workRoot 'Fallout76Custom.ini'
[System.IO.File]::WriteAllText($iniPath, $sample, (New-Object System.Text.UTF8Encoding($false)))
$originalBytes = [System.IO.File]::ReadAllBytes($iniPath)

Write-Host 'archive list count'
Test-Condition -Name 'count is zero before install' -Condition ((Get-ArchiveListCount -IniPath $iniPath -ArchiveName '76Radio.ba2') -eq 0) -Actual (Get-ArchiveListCount -IniPath $iniPath -ArchiveName '76Radio.ba2')

Write-Host 'add entry'
$result = Add-ArchiveListEntry -IniPath $iniPath -ArchiveName '76Radio.ba2'
Test-Condition -Name 'add reports added' -Condition ($result -eq 'added') -Actual $result
Test-Condition -Name 'count is one after install' -Condition ((Get-ArchiveListCount -IniPath $iniPath -ArchiveName '76Radio.ba2') -eq 1) -Actual (Get-ArchiveListCount -IniPath $iniPath -ArchiveName '76Radio.ba2')

$afterAddBytes = [System.IO.File]::ReadAllBytes($iniPath)
Test-Condition -Name 'no byte order mark' -Condition (-not ($afterAddBytes[0] -eq 0xEF -and $afterAddBytes[1] -eq 0xBB -and $afterAddBytes[2] -eq 0xBF)) -Actual ($afterAddBytes[0..2] -join ',')
Test-Condition -Name 'no carriage returns introduced' -Condition (($afterAddBytes | Where-Object { $_ -eq 0x0D }).Count -eq 0) -Actual (($afterAddBytes | Where-Object { $_ -eq 0x0D }).Count)
Test-Condition -Name 'first line unchanged' -Condition ((Read-IniText -Path $iniPath).StartsWith("[Archive]`n")) -Actual ((Read-IniText -Path $iniPath).Substring(0, 20))
Test-Condition -Name 'original entries preserved' -Condition ((Read-IniText -Path $iniPath) -match 'PerkLoadoutManager-HML.ba2, EVB76Nevernude - Meshes.ba2') -Actual 'pattern'
Test-Condition -Name 'other sections preserved' -Condition ((Read-IniText -Path $iniPath) -match "sIntroSequence=0") -Actual 'pattern'
Test-Condition -Name 'size grew by only the new entry' -Condition (($afterAddBytes.Length - $originalBytes.Length) -eq ', 76Radio.ba2'.Length) -Actual ($afterAddBytes.Length - $originalBytes.Length)

Write-Host 'add is idempotent'
$second = Add-ArchiveListEntry -IniPath $iniPath -ArchiveName '76Radio.ba2'
Test-Condition -Name 'second add reports present' -Condition ($second -eq 'present') -Actual $second
Test-Condition -Name 'count still one' -Condition ((Get-ArchiveListCount -IniPath $iniPath -ArchiveName '76Radio.ba2') -eq 1) -Actual (Get-ArchiveListCount -IniPath $iniPath -ArchiveName '76Radio.ba2')

$entries = Get-ArchiveListEntries -IniPath $iniPath
Test-Condition -Name 'archive entry is last' -Condition ($entries[-1] -eq '76Radio.ba2') -Actual $entries[-1]
Test-Condition -Name 'existing entry count preserved' -Condition ($entries.Count -eq 4) -Actual $entries.Count

Write-Host 'remove entry'
$removed = Remove-ArchiveListEntry -IniPath $iniPath -ArchiveName '76Radio.ba2'
Test-Condition -Name 'remove reports removed' -Condition ($removed -eq 'removed') -Actual $removed
Test-Condition -Name 'count is zero after uninstall' -Condition ((Get-ArchiveListCount -IniPath $iniPath -ArchiveName '76Radio.ba2') -eq 0) -Actual (Get-ArchiveListCount -IniPath $iniPath -ArchiveName '76Radio.ba2')
$restoredBytes = [System.IO.File]::ReadAllBytes($iniPath)
Test-Condition -Name 'file restored byte for byte' -Condition (($restoredBytes -join ',') -eq ($originalBytes -join ',')) -Actual $restoredBytes.Length
$absent = Remove-ArchiveListEntry -IniPath $iniPath -ArchiveName '76Radio.ba2'
Test-Condition -Name 'second remove reports absent' -Condition ($absent -eq 'absent') -Actual $absent

Write-Host 'section creation'
$emptyPath = Join-Path $workRoot 'Fallout76Custom-empty.ini'
[System.IO.File]::WriteAllText($emptyPath, "[General]`nsIntroSequence=0`n", (New-Object System.Text.UTF8Encoding($false)))
$created = Add-ArchiveListEntry -IniPath $emptyPath -ArchiveName '76Radio.ba2'
Test-Condition -Name 'missing section is created' -Condition ($created -eq 'added' -and (Read-IniText -Path $emptyPath) -match '\[Archive\]') -Actual $created
Test-Condition -Name 'created entry is registered' -Condition ((Get-ArchiveListCount -IniPath $emptyPath -ArchiveName '76Radio.ba2') -eq 1) -Actual (Get-ArchiveListCount -IniPath $emptyPath -ArchiveName '76Radio.ba2')

Write-Host 'section scoping'
$otherPath = Join-Path $workRoot 'Fallout76Custom-other.ini'
[System.IO.File]::WriteAllText($otherPath, "[Archive]`nsResourceArchive2List = base.ba2`n`n[Interface]`nsResourceArchive2List = notthis.ba2`n", (New-Object System.Text.UTF8Encoding($false)))
Add-ArchiveListEntry -IniPath $otherPath -ArchiveName '76Radio.ba2' | Out-Null
$otherText = Read-IniText -Path $otherPath
Test-Condition -Name 'only the Archive section is edited' -Condition ($otherText -match '\[Interface\]\nsResourceArchive2List = notthis.ba2') -Actual 'pattern'
Test-Condition -Name 'archive section received the entry' -Condition ((Get-ArchiveListCount -IniPath $otherPath -ArchiveName '76Radio.ba2') -eq 1) -Actual (Get-ArchiveListCount -IniPath $otherPath -ArchiveName '76Radio.ba2')

Write-Host ''
Write-Host ("{0} checks, {1} failures" -f $checks, $failures)
if ($failures -gt 0) {
    exit 1
}
exit 0
