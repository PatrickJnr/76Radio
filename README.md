# 76Radio

![76Radio](assets/branding/banner.png)

76Radio adds Spotify controls and internet radio to the Fallout 76 Pip-Boy RADIO tab. A `Spotify Radio` entry joins the game's own stations, your own stream list sits below a divider, and a now-playing panel shows the track, artist, artwork and transport controls. A matching HUD card can appear beside the game's notifications.

There is no companion application, no account login and no server. Spotify is controlled through the Windows media session that the desktop client already publishes, and internet stations play through the Windows media pipeline. The mod is client side, gameplay neutral and reversible.

## Features

- `Spotify Radio` in the RADIO list, selectable with the keyboard, controller or mouse.
- Play, pause, previous and next through the Spotify desktop client.
- Track title, artist, album, elapsed time and playback state.
- Album artwork from the Windows media session, with the radio wave strip as a fallback.
- A seekable progress bar for Spotify tracks.
- Custom internet stations from a JSON file, with a divider between the game's stations and yours.
- A starter list of 20 internet stations, so the tab is populated on first launch.
- Playback states that match what the player is doing: connecting, buffering, playing, reconnecting, failed and unsupported.
- Automatic reconnection after a dropped stream, and M3U, M3U8, PLS and HLS station addresses.
- ICY song metadata and station names from stations that broadcast them.
- Internet stations that follow the game's Pip-Boy Radio volume slider.
- An optional beta entry, named after the app it finds (for example `Chrome [BETA]`), that controls another media player, separate from Spotify Radio.
- Automatic handover: starting a station pauses Spotify, and returning to Spotify or a game station closes the stream.
- An active marker on the station that is playing.
- A HUD now-playing card that follows the Pip-Boy colour.
- An optional persistent mini player instead of the timed card.
- An INI file for logging, polling and UI options.

The mod does not change game files, game networking or gameplay values. It does not read Spotify credentials, use the Spotify Web API or capture audio.

## Requirements

| Requirement | Needed for |
| --- | --- |
| Fallout 76 on Windows 10 or 11 | The game |
| [xScal](https://www.nexusmods.com/fallout76/mods/4183) 0.4.22 or later, installed as `dxgi.dll` beside `Fallout76.exe` | Required. Loads the 76Radio plugin and stores its documents |
| [HUDModLoader](https://www.nexusmods.com/fallout76/mods/3144) | The HUD card and the transport relay behind the Pip-Boy buttons |
| The Spotify desktop client | The `Spotify Radio` entry |

xScal is required. It loads `Data\XSCAL\Plugins\76Radio.dll`, and it rejects the DLL unless `76Radio.dll.sig` matches it. Without a loaded plugin the panel reports the bridge as offline and the mod cannot work. HUDModLoader is required for the HUD card and for the transport relay behind the PREV, PLAY/PAUSE and NEXT buttons; the panel still shows the track and artwork without it. Neither dependency is included in this package.

## Installation

Close Fallout 76 before installing or deploying.

### With Vortex

1. Install xScal first and let Vortex deploy it, so `dxgi.dll` sits beside `Fallout76.exe`.
2. Install the 76Radio zip in Vortex and deploy it. Every file in the zip is relative to the game folder, including the plugin and its signature under `Data\XSCAL\Plugins`.
3. Deploy the mod.
4. Make the two required ini edits below. Vortex does not edit `Fallout76Custom.ini` or `hudmodloader.ini` for you.

### By hand

1. Open the zip, select everything and drag it into the Fallout 76 folder, the one containing `Fallout76.exe` (typically `...\steamapps\common\Fallout76`), and allow it to merge with the existing `Data` folder. The zip already follows the game folder layout, so nothing needs moving into `Data` by hand.
2. Add `76Radio.ba2` to `sResourceArchive2List` in the `[Archive]` section of `Documents\My Games\Fallout 76\Fallout76Custom.ini`. Create the section and the key if they are missing, and keep the archives already listed.
3. If HUDModLoader is installed, add `76RadioHud` on its own line in `Data\hudmodloader.ini`. HUDModLoader loads only the SWFs named in that file, so without the line the HUD card and the transport relay do not load. `Data\interface\76RadioHud.swf` is already in place from the zip. If you do not use HUDModLoader, skip this step.
4. Optionally edit `Data\XSCAL\76Radio\76Radio.ini` for logging, polling and UI options. Without changes the module uses its defaults, and an update should not overwrite it.
5. Edit `Data\modsdata\76radio\stations.json` to add your stations. Keep an existing list when updating.
6. If an old 76Radio `version.dll` is still beside `Fallout76.exe`, delete it. The plugin replaced that host, and leaving both in place makes two media hosts fight over the same documents.
7. Start the game, open the Pip-Boy and select the RADIO tab. `Spotify Radio` appears at the end of the game's stations, above the divider.

A Vortex install also needs the archive list entry and, with HUDModLoader, the `hudmodloader.ini` line. Vortex does not edit the game's ini files.

### Language archives

These are optional. Add the matching archive to `sResourceArchive2List` only if the station names or track titles you listen to are written in that language, since the radio panel needs the characters to display them:

- `SeventySix - Interface_ja.ba2` for Japanese
- `SeventySix - Interface_ko.ba2` for Korean
- `SeventySix - Interface_zhhans.ba2` for Simplified Chinese
- `SeventySix - Interface_zhhant.ba2` for Traditional Chinese
- `SeventySix - Interface_ru.ba2` for Russian

All five ship with the game, not with 76Radio, and most players need none of them. English uses the font the game already loads.

The same steps are repeated in `INSTALL.txt` inside the release package.

### Checking that it loaded

- Select `Spotify Radio`. The panel shows live data when Spotify is playing, or a clear unavailable state when it is not.
- Look for `Data\XSCAL\76Radio\76Radio.log` in the game folder. Its first line gives the 76Radio version, every line is timestamped, and it is rewritten on every launch and names the documents the module reads and writes.
- Look for `Data\XSCAL\Plugins\76Radio.dll` and `76Radio.dll.sig`. xScal writes to `xscal.log` beside `Fallout76.exe`; the line naming 76Radio says whether the plugin loaded or why it was rejected.
- If the panel reports the bridge as offline, the plugin did not load. A missing or stale `.sig`, a plugin built without signing, or a certificate mismatch all appear in `xscal.log` with the reason.

### Uninstalling

In Vortex, remove the mod and deploy again. For a manual install, delete `Data\76Radio.ba2`, `Data\interface\76RadioHud.swf`, `Data\XSCAL\Plugins\76Radio.dll` and `Data\XSCAL\Plugins\76Radio.dll.sig`. Either way, remove `76Radio.ba2` from `sResourceArchive2List`, remove the `76RadioHud` line from `Data\hudmodloader.ini`, and optionally delete `Data\XSCAL\76Radio` and `Data\modsdata\76radio`. Removing the command documents in `Data\modsdata` is optional; they are small and the module recreates what it needs.

## Configuration

Settings live in `Data\XSCAL\76Radio\76Radio.ini` and are read once, when the game starts. Every entry is optional, and a missing file means the defaults. Delete a line, or the file, to reset it.

```ini
[Logging]
Enabled=1
MaxKb=1024

[Media]
StartupDelayMs=4000
PollIntervalMs=900

[Channel]
PollIntervalMs=150

[UI]
ShowStatus=0
ShowArtStatus=0
ShowArtwork=1
KeepNowPlaying=0
```

Booleans accept `1/0`, `true/false`, `on/off` and `yes/no`. Out-of-range intervals are clamped: media polling 100 to 60000 ms, command polling 20 to 5000 ms, start delay 0 to 60000 ms, and the log size 16 to 65536 KB, where `0` disables rotation. `KeepNowPlaying=1` keeps the card on screen while a track plays and becomes a persistent mini player; it still steps aside while a game notification is visible.

`[Logging] Enabled=0` stops 76Radio writing its log at all. The log is truncated at the start of each launch and rotates once to `76Radio.log.1`, so the pair stays bounded.

### Stations

Stations are managed by editing `Data\modsdata\76radio\stations.json` in a text editor. The file is a small JSON document with one `stations` list:

```json
{
  "stations": [
    { "name": "Classic FM", "streamurl": "https://ice-sov.musicradio.com/ClassicFMMP3" },
    { "name": "Radio Dismuke", "streamurl": "http://stream1.radiodismuke.com:8078/;stream.mp3" }
  ]
}
```

To add a station, copy one `{ ... }` line, change the `name` and `streamurl`, and keep a comma between entries but not after the last one. To edit a station, change its name or address in place. To remove a station, delete its whole line and fix the commas. Save the file; the Pip-Boy list refreshes within a few seconds, even while the game is running, so you do not need to restart. If the file cannot be read, 76Radio keeps the previous list until you fix it.

The package ships with a starter list of 20 stations. An update never overwrites your `stations.json`, so your own list is kept. The same file is used by 0.1.0, so an existing list works unchanged.

Rules for entries:

- `name` is the text shown in the list, and may contain any characters, including non-English ones. Add the matching language archive from the section above if the font does not show them.
- `streamurl` must start with `http://` or `https://`. Entries with no name, a bad address or a repeated address are skipped, and the rest of the list still loads. At most 256 stations are read.
- Quote marks and backslashes inside a name must be escaped as `\"` and `\\`.

Supported station addresses:

- Direct HTTP and HTTPS audio streams that Windows can play, such as MP3, AAC and Ogg.
- M3U and M3U8 playlists, and PLS playlists. 76Radio opens the playlist, follows redirects and tries each entry in turn. A playlist address can be recognised by its file name, its content type or its contents.
- HLS playlists (`#EXT-X-` streams) are passed to Windows unchanged. They do not carry ICY metadata.
- Playlists are limited to 256 KB, 32 entries, two levels of nesting and five redirects, and each request has a short timeout.

### Playback states

The panel shows what the player is really doing:

- `CONNECTING` while the station address is being resolved and opened.
- `BUFFERING` while Windows is filling its buffer. `PLAYING` is only shown once audio is actually playing.
- `RECONNECTING` after a dropped stream. 76Radio opens a fresh connection and metadata reader after 2, 4, 8 and 16 seconds, then gives up and shows `FAILED`.
- `FAILED` when the station cannot be reached or stops responding.
- `UNSUPPORTED` when the station answers but Windows cannot decode what it sends.
- `STOPPED` when you stop the station or move to another source.

Looking up a station address happens on a background thread, so selecting a slow or dead station does not freeze the Pip-Boy. Stopping, picking another station, switching to Spotify or closing the game cancels a lookup that is still in progress, and the result of a cancelled lookup is ignored.

A station can be started and stopped, but not paused or seeked. ICY song titles appear only when the station sends them, in UTF-8, Windows-1252 or ISO-8859-1 text. If a station has no name in your list, the station name from its ICY headers is used. Metadata is cleared when a stream stops. Ogg stations frequently advertise the station name without song metadata.

### Windows Media

`Spotify Radio` always controls Spotify and nothing else. A separate `Windows Media` entry below it controls any other player that publishes a Windows media session, such as a browser tab or a desktop music player. Spotify Radio never switches to another application by itself, and Windows Media never starts by itself: it only takes over when you select it.

- The entry appears only while some other application has a media session, and takes that application's name: `Chrome`, `Edge`, `Firefox`, `Brave`, `Opera`, `VLC` or `Media Player` for the common ones, otherwise the application's own name. It reads `Windows Media` only when the name is unknown. The entry is marked `[BETA]` because it depends on what each application publishes to Windows, and some applications, Edge for example, may not be detected. `Spotify Radio` appears only while Spotify is running. Both vanish from the list when there is nothing to control, so the list stays short.
- Selecting `Windows Media` picks the session Windows currently treats as active, ignoring Spotify, shows its name on the status line as `SOURCE: name >`, and toggles play or pause on it, the same as selecting `Spotify Radio`. Select it while the media is already playing and it will pause, so press PLAY to carry on.
- With several players open, click the `SOURCE` line to move to the next one. This is a mouse action, and it never selects Spotify or starts or pauses anything.
- The panel shows the title, artist, album, state and progress that the application reports, and disables previous, next and seeking when the application does not allow them.
- Selecting `Spotify Radio` again returns to Spotify.
- If the selected application closes, the panel reports that there is no session instead of switching to another one.

Not every application publishes a media session, and some only do so while playing, so do not assume a particular player will work. Chrome has been tested in-game; Edge did not appear when tried. Many applications publish no artwork, in which case the panel shows the radio wave strip instead.

## Building from source

The native code and the ActionScript are public so that the module can be inspected and rebuilt rather than trusted as a binary.

The maintainer's signing certificate (`patrickjnr.xcert`) and key are private and are not published, so a build from this repository cannot produce a `76Radio.dll` that xScal will load under the `patrickjnr_76radio` ID. What anyone can do without them:

- Build and run every native library and test with `-DBUILD_XSCAL_PLUGIN=OFF` (`scripts\ci.ps1` does this). This covers the media, stream, playlist, ICY metadata, station catalogue, bridge and channel code.
- Compile the ActionScript with `scripts\build-hud.ps1` and the Pip-Boy page with `scripts\build.ps1 -SkipNative`.
- Build the plugin against the xScal SDK without signing. The result is an unsigned DLL that xScal rejects, which is enough to check that the plugin source compiles.
- Run a fork of the mod by obtaining your own certificate and author prefix from the xScal maintainer, changing `kPluginId`, and signing your own build.

Official releases are signed by the maintainer. To install the mod, use the signed release package rather than building the plugin yourself.

### Prerequisites

- Windows 10 or 11 x64, Visual Studio 2026 with the Desktop development with C++ workload, and CMake 3.25 or later.
- The xScal plugin SDK, which is not redistributed here. Ask the xScal maintainer for it, then point `XSCAL_PLUGIN_SDK_DIR` at its `cmake` folder.
- A signing key and certificate. Generate a key pair with `sdk\bin\xscal-sign.exe keygen dev.key dev.pub`, then ask the xScal maintainer for a certificate; it fixes the mod-author prefix you may use for plugin IDs. The project's `patrickjnr_76radio` ID is signed with PatrickJr's `patrickjnr_` certificate, so a third-party build needs its own author prefix and a matching `kPluginId` in `src\native\plugin\plugin_entry.cpp`. Set `XSCAL_SIGNING_KEY` and `XSCAL_SIGNING_CERTIFICATE` to your key and the returned certificate. Keep the key and certificate out of the repository; `work` is ignored.
- Python 3 for the archive tools.
- Java for FFDec.
- [JPEXS Free Flash Decompiler](https://github.com/jindrapetrik/jpexs-decompiler/releases) with `ffdec-cli.jar` at `tools\vendor\ffdec\ffdec-cli.jar`.
- [Apache Flex SDK 4.16.1](https://archive.apache.org/dist/flex/4.16.1/binaries/apache-flex-sdk-4.16.1-bin.tar.gz) extracted to `work\flex`, plus the Flash Player 32.0 `playerglobal.swc` at `frameworks\libs\player\32.0\playerglobal.swc`. Pass another location with `-FlexHome`.
- Fallout 76 installed. The patched radio page is compiled from the player's own `SeventySix - Interface.ba2`.

The HUD relay also needs the shared HUDModLoader ActionScript classes. `scripts\build-hud.ps1` downloads them into `src\scaleform\hud` from the HUDModLoader repository if they are absent. They are third-party source, not part of 76Radio, and are not committed here.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -XScalSdk C:\xscal-sdk\sdk\cmake
```

That configures and builds the plugin with CMake, runs the native tests, compiles `76RadioHud.swf` with Flex, applies `src\scaleform\pipboy` to the extracted radio page with FFDec, checks the recompiled script, packs `dist\76Radio.ba2`, verifies the archive and writes `dist\build-manifest.json`. Set `XSCAL_SIGNING_KEY` and `XSCAL_SIGNING_CERTIFICATE` before configuring so the build also writes `76Radio.dll.sig`; without them the DLL is unsigned and xScal will not load it.

The relay can be rebuilt on its own after editing the HUD source:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-hud.ps1
```

Direct CMake build and tests:

```powershell
cmake --preset msvc-release -DXSCAL_PLUGIN_SDK_DIR=C:\xscal-sdk\sdk\cmake
cmake --build --preset msvc-release
ctest --preset msvc-release
```

Pass `-DBUILD_XSCAL_PLUGIN=OFF` to build and test the native libraries without the SDK.

For a package, after a successful build:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

The package contains only installed files: the archive, the loose HUD relay, `Data\XSCAL\Plugins\76Radio.dll` with its `.sig`, the settings and station templates, `INSTALL.txt`, `LICENSE.txt`, and `dist\SHA256SUMS.txt`. It refuses to pack an artifact whose hash no longer matches the build manifest, and it refuses to pack an unsigned plugin.

The deployment, verification and uninstall scripts are optional convenience tools for a local installation. They detect the game through Steam, the Bethesda.net install, an Xbox install or a running process, and accept `-GameRoot` when it lives elsewhere. They refuse to overwrite files they do not own, back up what they replace and record what they installed. A deployment refuses to continue when the plugin has no signature:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\deploy.ps1
```

## Why an xScal plugin?

76Radio used to ship a `version.dll` proxy. That is gone. The native module is now an ordinary xScal plugin, `Data\XSCAL\Plugins\76Radio.dll`, loaded by the extender the player already installed. Nothing sits beside `Fallout76.exe` with a Windows library's name, and there is no forwarding code to audit.

xScal loads only signed plugins. The build signs the DLL with the project's key and writes `76Radio.dll.sig` next to it; the certificate ties the `patrickjnr_` author prefix used by the `patrickjnr_76radio` plugin ID to that key. A DLL with no signature, or a signature made for a different build, is rejected, and `xscal.log` says which. The signing key and certificate are not published and are not in this repository, so a rebuild needs a key and certificate issued by the xScal maintainer; the plugin SDK is likewise distributed by them and not vendored here.

The source for the plugin is in `src\native\plugin` and the media code it hosts is in the other `src\native` folders. `README-TECHNICAL.md` describes the loading, threading and document contract.

Each release records the plugin's SHA-256 and a VirusTotal report in `dist\virustotal.txt` and in the release notes, because the hash changes with every build. Check that the report hash matches the `76Radio.dll` you downloaded. A scan is not a guarantee of safety, which is why the source is published and can be rebuilt.

A process-resident module still carries risk that an archive-only interface mod does not. It is client side, it does not read game memory, patch game files, modify networking or bypass a signature check, but it does run inside the game process. Whether to install it is the player's decision.

## Compatibility and limitations

- The mod replaces the whole `newpipboy_radiopage.swf` with a page compiled from the installed game archive. Another mod that replaces the same page will conflict. The deployment script detects that case.
- The panel and archive are built against a particular game version. A game update can change the page or the interface archive, so use the package built for the installed version rather than forcing an old archive in place.
- Only one radio source plays at a time. Starting a stream pauses Spotify; starting a game station closes the stream.
- Internet stations cannot be paused or seeked. The panel's control stops them.
- ICY metadata depends on the station. Missing metadata is shown as the station name, not invented.
- The `Windows Media` entry works only with applications that publish a Windows media session, and switching between sessions uses the mouse.
- Transport buttons and the HUD card need HUDModLoader. The page still shows track and artwork without the relay, but commands have no route.
- Occasionally the RADIO page is blank the first time the Pip-Boy opens. Closing and reopening the Pip-Boy shows it normally. The cause has not been identified.
- Internet stations follow the game's Pip-Boy Radio volume and master volume sliders. Spotify and other media sessions keep their own volume, which you set in Windows or the player.
- The mod has not been tested against every game update, every HUD or Pip-Boy replacer, or every xScal release. A plugin built for an older xScal ABI keeps loading in newer releases, but that has not been exercised here.
- xScal rejects the plugin when the signature is missing or does not match the DLL. Updating the DLL without its `.sig` breaks the mod until both are replaced.
- Fallout 76 is an online game. The module is client side and does not provide a gameplay advantage, but use of any process-resident module remains the player's choice.

## Source and contributions

The repository is [github.com/patrickJnr/76Radio](https://github.com/patrickJnr/76Radio). The source is published so the implementation can be read, audited and built locally. Issues and pull requests are welcome; see [CONTRIBUTING.md](CONTRIBUTING.md). Report security problems privately through [SECURITY.md](SECURITY.md), not in a public issue. The licence is the [PolyForm Noncommercial License 1.0.0](LICENSE). It is source-available and permits noncommercial use, but it is not an OSI open source licence.

Third-party tools, shared ActionScript and the origin of the patched page script are recorded in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). The architecture, protocol and build pipeline are described in [README-TECHNICAL.md](README-TECHNICAL.md). Release steps, including the VirusTotal upload and the text that must match the new hash, are in [RELEASING.md](RELEASING.md).

## Credits

- HUDModLoader and its shared HUD ActionScript, by GitCrazy-wc: [Nexus Mods 3144](https://www.nexusmods.com/fallout76/mods/3144).
- xScal, by DCHoaxer, which loads the plugin and provides the document store: [Nexus Mods 4183](https://www.nexusmods.com/fallout76/mods/4183).
- JPEXS Free Flash Decompiler and the Apache Flex SDK, used to build the interface files.
- Fallout 76 and its interface belong to Bethesda Game Studios.

## AI disclosure

AI assistants were used for research during development: looking up Windows media and Scaleform behaviour, comparing implementation options and checking documentation. They were a research aid, not the source of the mod. The code, assets and release decisions were reviewed and remain the project owner's responsibility.

## Disclaimer

76Radio is an unofficial mod. It is not endorsed by Bethesda, Microsoft, Spotify or the authors of xScal or HUDModLoader. Fallout 76 assets are not redistributed; the patched page is built from the player's own installation. Spotify is controlled only through the media session that the desktop client publishes, with no account access, tokens or audio capture.
