param(
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot
$build = Read-BuildManifest -RepoRoot $repoRoot
$stage = Join-Path $repoRoot 'work\package'
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}

$files = @(
    @{ kind = 'archive'; source = 'dist\76Radio.ba2'; target = 'Data\76Radio.ba2' },
    @{ kind = 'hud-relay'; source = 'dist\76RadioHud.swf'; target = 'Data\interface\76RadioHud.swf' },
    @{ kind = 'surface-module'; source = 'dist\version.dll'; target = 'version.dll' }
)

foreach ($file in $files) {
    $source = Join-Path $repoRoot $file.source
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Missing build output: $($file.source)"
    }
    $artifact = @($build.artifacts | Where-Object { $_.kind -eq $file.kind }) | Select-Object -First 1
    if (-not $artifact) {
        throw "No build manifest entry for $($file.kind)"
    }
    $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    if ($hash -ne $artifact.sha256) {
        throw "Hash mismatch for $($file.source). Rebuild before packaging."
    }
    $target = Join-Path $stage $file.target
    $directory = Split-Path -Parent $target
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    Copy-Item -LiteralPath $source -Destination $target -Force
}

$templates = @(
    @{ source = 'dist\76Radio.ini'; target = 'Data\XSCAL\76Radio\76Radio.ini' },
    @{ source = 'dist\stations.json'; target = 'Data\modsdata\76radio\stations.json' },
    @{ source = 'LICENSE'; target = 'LICENSE.txt' }
)

foreach ($file in $templates) {
    $source = Join-Path $repoRoot $file.source
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Missing template: $($file.source)"
    }
    $target = Join-Path $stage $file.target
    $directory = Split-Path -Parent $target
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    Copy-Item -LiteralPath $source -Destination $target -Force
}

$packageVersion = $build.version
$readme = @"
76Radio $packageVersion

What this is
A Pip-Boy radio page replacement that adds a Spotify Radio entry to the RADIO tab, plus
a native module that drives the Spotify desktop client from that panel. There is no
companion application, no service, no Spotify account access and no scripting interface.

What is in this package
Data\76Radio.ba2  the patched Pip-Boy radio page and a copy of the HUD relay.
Data\interface\76RadioHud.swf  the relay HUDModLoader opens by its ini name.
version.dll       the 76Radio native module that runs inside the game process.
Data\XSCAL\76Radio\76Radio.ini  settings: logging and polling intervals.
Data\modsdata\76radio\stations.json  custom internet stations, installed once.
LICENSE.txt      the PolyForm Noncommercial 1.0.0 terms for this package.

Requirements
Fallout 76 on Windows.
xScal, installed and deployed beside Fallout76.exe as dxgi.dll. This is required:
76Radio stores its documents in xScal's folder and cannot work without it.
HUDModLoader, for the HUD now-playing card and the transport relay. The Pip-Boy panel
still shows the track and artwork without it.
The Spotify desktop client for Spotify Radio. Internet stations do not need Spotify.

Installing with Vortex
1. Install xScal first, so dxgi.dll sits beside Fallout76.exe.
2. Install this zip in Vortex. Double-click the mod to open its details panel, set Mod
   Type to Engine Injector, then deploy. The default Data type would put version.dll
   under Data, where the game does not load it.
3. Deploy the mod, then make the edits under "Ini edits" below. Vortex does not edit the
   game's ini files.

Installing by hand
1. Close Fallout 76.
2. Open the zip, select everything and drag it into the Fallout 76 folder, the one with
   Fallout76.exe, and merge it with the existing Data folder. The zip already follows the
   game folder layout, so do not copy the files into Data by hand.
3. Make the edits under "Ini edits" below.
4. Start Fallout 76, open the Pip-Boy RADIO tab and select Spotify Radio.

If a version.dll is already beside Fallout76.exe, find out which mod owns it before
replacing it. Two mods cannot share that filename. Do not overwrite it blindly.

Ini edits
1. Add 76Radio.ba2 to the sResourceArchive2List line in the [Archive] section of
   Documents\My Games\Fallout 76\Fallout76Custom.ini. Create the section and the line
   if they are absent, and keep the archives already listed.
2. If HUDModLoader is installed, add 76RadioHud on its own line in Data\hudmodloader.ini.
   HUDModLoader loads only the SWFs named in that file, so without the line there is no
   HUD card and no transport relay. Data\interface\76RadioHud.swf is already in place.
3. Optional: if you listen to stations or tracks in another language, add the matching
   game archive to the same line: SeventySix - Interface_ja.ba2 (Japanese),
   SeventySix - Interface_ko.ba2 (Korean), SeventySix - Interface_zhhans.ba2 (Simplified
   Chinese), SeventySix - Interface_zhhant.ba2 (Traditional Chinese) or
   SeventySix - Interface_ru.ba2 (Russian). They ship with the game, not this package,
   and most players need none of them.
4. Optional: edit Data\XSCAL\76Radio\76Radio.ini for logging, polling and UI options.
   Without changes the module uses its defaults, and an update should not overwrite it.
5. Edit Data\modsdata\76radio\stations.json to add your stations. Keep an existing list
   when updating.

Uninstall
1. Close Fallout 76.
2. In Vortex, remove the mod and deploy again. For a manual install, delete
   Data\76Radio.ba2, Data\interface\76RadioHud.swf and version.dll from the game folder.
3. Remove 76Radio.ba2 from sResourceArchive2List and the 76RadioHud line from
   Data\hudmodloader.ini.
4. Optionally delete Data\XSCAL\76Radio and Data\modsdata\76radio (settings, stations,
   state and artwork), plus Data\modsdata\r76radio.command.json, r76radio.diag.json and
   r76radio.relay.json.

About version.dll
It is an ordinary module with the same name as a Windows system library, which is how
the game loads it at start up. It forwards every one of that library's functions to the
real system copy. It reads Data\modsdata\r76radio.command.json and writes its own state,
artwork and station documents under Data\modsdata\76radio, runs media queries and playback
control for Spotify and internet stations on its own thread, and logs to
Data\XSCAL\76Radio\76Radio.log. It does not patch the game, read game memory, change files
the game owns, listen on a network port or download anything.

Settings
Data\XSCAL\76Radio\76Radio.ini is read once at start up. Set Enabled=0 under [Logging]
to stop 76Radio writing its log at all; MaxKb sets the size at which the log rotates.
The log is also cleared on every launch. [Media] StartupDelayMs is how long the module
waits before its first Windows media request, and PollIntervalMs is how often it reads
Spotify; [Channel] PollIntervalMs is how often it checks for a Pip-Boy command. Delete
a line or the file to get its default back. Edits are never overwritten by an update.

What works
The Spotify Radio entry appears in the RADIO tab with the game's own stations, can be
highlighted and selected with the keyboard, gamepad or mouse, and keeps every existing
station untouched. Stations listed in stations.json appear beside it and play as internet
streams. The panel shows the current artist, track, playback state and connection status,
replaces the oscilloscope with the track's album artwork when Windows provides it, and
sends play, pause, previous and next.

What needs a working host
Track metadata and transport control need version.dll to be loaded by the game. Without
it the panel reports Status: Bridge offline and pressing the entry answers with that
state instead of pretending a command was delivered. Status: Channel ready, no host
means the panel reached its document store but no module answered.

Requires xScal and Fallout 76. The interface archive is built from one specific game
version, so reinstall the patch that ships with a new game version instead of forcing
the old copy in place. Unofficial. Not endorsed by Spotify, Bethesda or the authors of
any other mod. Licensed under PolyForm Noncommercial 1.0.0; see LICENSE.txt.
"@
$readmePath = Join-Path $stage 'INSTALL.txt'
$encoding = New-Object System.Text.UTF8Encoding($false)
$readme = $readme.Trim("`r","`n") -replace "(?<!`r)`n","`r`n"
[System.IO.File]::WriteAllText($readmePath, $readme + "`r`n", $encoding)

if ($OutputPath.Length -eq 0) {
    $OutputPath = Join-Path $repoRoot ('dist\76Radio-' + $packageVersion + '.zip')
}
if (Test-Path -LiteralPath $OutputPath) {
    Remove-Item -LiteralPath $OutputPath -Force
}
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $OutputPath -CompressionLevel Optimal

$sums = @()
foreach ($file in @('dist\76Radio.ba2', 'dist\76RadioHud.swf', 'dist\version.dll')) {
    $path = Join-Path $repoRoot $file
    $sums += ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash + '  ' + (Split-Path -Leaf $file))
}
$sums += ((Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash + '  ' + (Split-Path -Leaf $OutputPath))
$sumsPath = Join-Path $repoRoot 'dist\SHA256SUMS.txt'
[System.IO.File]::WriteAllText($sumsPath, (($sums -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding($false)))

Write-Section 'Package'
foreach ($file in $files) {
    Write-Result "packed $($file.target)" 'ok'
}
foreach ($file in $templates) {
    Write-Result "packed $($file.target)" 'ok'
}
Write-Result 'packed INSTALL.txt' 'ok'
Write-Result (("archive {0} ({1:N0} bytes)") -f $OutputPath, (Get-Item -LiteralPath $OutputPath).Length) 'ok'
Write-Result (("entries: " + ((Get-ChildItem -LiteralPath $stage -Recurse -File).Count))) 'ok'
Write-Result "checksums $sumsPath" 'ok'
