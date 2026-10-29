# 76Radio technical reference

This document describes the implementation in this repository. It is written from the source, not from earlier design notes. It covers the native module, the Windows media integration, the document channel, the two Scaleform movies, the build pipeline and the security boundaries.

## Architecture

```text
Fallout76.exe
  version.dll                        76Radio surface module
    proxy exports                    seventeen version.dll entry points, forwarded to System32
    worker thread                    settings, media runtime and document channel
      MediaService                   immutable snapshots, bounded command queue
        Windows GSMTC                Spotify session found by application id
        StreamPlayer                 WinRT MediaPlayer for internet stations
        IcyMetadataReader            second connection, station and song text
      DocumentChannel                polls commands, writes state, reads Pip-Boy colour
  dxgi.dll                           xScal extender, installed by the player
    modStorage                       named JSON documents under Data\modsdata
  interface\newpipboy_radiopage.swf  patched Pip-Boy RADIO page
  interface\76RadioHud.swf           HUD relay, loaded by HUDModLoader
```

There is no plugin dependency on xScal. The native module never attaches to the Scaleform movie root; the movies and the module meet through files.

| Piece | Language | Role |
| --- | --- | --- |
| `src/native/surface` | C++20 | `version.dll`, export forwarding, process lifetime |
| `src/native/runtime` | C++20 | Owns the media service and its options |
| `src/native/media` | C++20 | Spotify, streams, ICY, artwork, snapshots |
| `src/native/bridge` | C++20 | Command grammar, state documents, limits |
| `src/native/channel` | C++20 | File polling, state publication, Pip-Boy theme |
| `src/native/settings` | C++20 | INI parsing and bounded log files |
| `src/scaleform/pipboy` | AS3 | RADIO page replacement |
| `src/scaleform/hud` | AS3 | HUD relay and now-playing card |
| `tools/ba2` | Python | BA2 inspection, packing and verification |
| `scripts` | PowerShell | Build, deploy, verify, package, uninstall |

## Repository layout

```text
src/native/{media,bridge,runtime,channel,settings,surface,testing}
src/scaleform/pipboy        NewPipboy_RadioPage.as
src/scaleform/hud           Radio76Hud.as and shared HUDModLoader classes
config                      76Radio.ini and stations.example.json
scripts                     build, HUD build, dependency fetch, deploy, verify, package, uninstall
tools/ba2                   ba2pack.py and ba2inspect.py
tests                       PowerShell tests for the archive list editor
assets/branding/banner.png  README banner
```

`src/native/testing` is shared test support, not a shipped executable. `work` and `dist` are generated and ignored. `tools/vendor` holds FFDec and is ignored. The remaining branding artwork and `release` hold local release material and are not tracked.

## Native module

### Loading and forwarding

The game imports `VERSION.dll`, so Windows searches the executable directory before `System32`. The built file is named `version.dll` and sits beside `Fallout76.exe`. `KnownDLLs` does not cover `version.dll` on the tested installation, which is why the local file wins.

`src/native/surface/surface_module.def` exports the same seventeen names as the system library. Each export is a small thunk in `proxy_exports.cpp` that resolves the real function from `%SystemRoot%\System32\version.dll` with `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)` and calls it. Resolution is lazy and cached per function. If the system library or an entry point cannot be resolved, the thunk returns the API's natural failure value (`FALSE` or `0`) rather than calling itself recursively. The proxy code contains no media logic.

The forwarded set is:

```text
GetFileVersionInfoA/W, GetFileVersionInfoExA/W, GetFileVersionInfoByHandle,
GetFileVersionInfoSizeA/W, GetFileVersionInfoSizeExA/W,
VerFindFileA/W, VerInstallFileA/W, VerLanguageNameA/W, VerQueryValueA/W
```

`GetFileVersionInfoByHandle` is undocumented and is forwarded without being exercised. The installer entry points (`VerFindFile`, `VerInstallFile`) are forwarded by the same path but are not used by the mod.

### Lifetime

`DllMain` handles `DLL_PROCESS_ATTACH` by disabling thread notifications, creating a manual-reset event, starting one worker thread and signalling the event. It does no file, network, COM or media work. On `DLL_PROCESS_DETACH` it sets the stop flag, waits up to three seconds for the worker when the process is not terminating, and closes its handles.

The worker waits up to five seconds for the start event, then:

1. Resolves the executable directory.
2. Reads `Data\XSCAL\76Radio\76Radio.ini`.
3. Resets `Data\XSCAL\76Radio\76Radio.log` when logging is enabled.
4. Waits for the configured startup delay before the first Windows media call.
5. Constructs the media runtime and starts its worker.
6. Constructs the document channel and starts its worker.
7. Sleeps in 100 ms steps until unload, then stops the channel and the runtime.

If the media service cannot stop within its timeout, the runtime object is intentionally released rather than destroyed, because its worker may still be inside a Windows media call. The log records `media stop timed out, runtime left to the process exit`. This trades a small leak at process exit for not deadlocking the game.

The module logs its process id, executable directory, module path, parsed settings, document paths, every accepted or rejected command, every published state and any stop timeout. The log is truncated each launch and rotates once at `MaxKb`.

### Settings

`RuntimeSettings` defaults are in `src/native/settings/include/radio76/settings.h`. `ParseSettings` is case-insensitive, ignores `;` and `#` comments, accepts the boolean spellings listed in the README and clamps numeric ranges. A file larger than 8 KB is ignored. A missing or unreadable file returns defaults.

The installed INI is read once. Updating it requires a game restart.

## Media core

`MediaService` owns one worker thread. Scaleform and the document channel never call Windows media APIs directly. The worker wakes on a command or at the poll deadline, executes queued commands, polls the backend, and publishes an immutable `PlaybackSnapshot` through an atomic shared pointer. `SnapshotsEquivalent` ignores position and capture time, so a playing track does not produce a new state document on every poll. A listener callback forwards real changes to the channel.

The queue is bounded at 32 commands. `Post` returns false when full; `Submit` waits for completion with a timeout. Capability checks (`canPlay`, `canPause`, `canNext`, `canPrevious`, `canSeek`) are applied before the backend sees a command. Stream commands bypass session availability because they do not belong to Spotify.

### Spotify through GSMTC

`WindowsMediaBackend` initialises a single-threaded WinRT apartment on the media worker. It asks `GlobalSystemMediaTransportControlsSessionManager` for the current sessions and selects the first whose source application id contains `spotify`, case-insensitively. Browsers and other players are ignored even when they are the system's current session. A process snapshot checks for `Spotify.exe` so the UI can distinguish "Spotify closed" from "Spotify open with no session".

Playback status maps to the mod's `PlaybackState`. Metadata comes from `TryGetMediaPropertiesAsync`; the thumbnail is read as a stream and decoded with WIC. Artwork is written twice under `Data\modsdata\76radio`:

- `r76radio.art.png` or `.jpg`, the original thumbnail bytes.
- `r76radio.art.pixels`, a text document with a `width height` header and base64-encoded RGBA pixels, capped at 512 by 512.

The `.pixels` form exists because the Scaleform `Loader` path for local images is unreliable in this UI; the page decodes the base64 itself. Artwork files are removed when the track has no thumbnail or when the session is lost.

Transport uses `TryPlayAsync`, `TryPauseAsync`, `TryTogglePlayPauseAsync`, `TrySkipNextAsync`, `TrySkipPreviousAsync` and `TryChangePlaybackPositionAsync`. A false result is reported as a failed command; the mod does not claim success for an operation Windows refused.

### Internet radio

`StreamPlayer` uses WinRT `Windows.Media.Playback.MediaPlayer` with command manager integration disabled and audio category `Media`. Opening a stream sets a `MediaSource` from the URI and calls `Play`. A new stream stops the old one first.

A stream is considered started when the player reports playing or paused, with a 20 second startup budget. Failures are retried up to four times with a four second delay, then the snapshot reports the stream as stopped. `Stop` pauses, detaches the source and clears the address. `Shutdown` releases the player during runtime shutdown.

`IcyMetadataReader` opens its own HTTP connection with `WinHTTP`, requests `Icy-MetaData: 1`, reads `icy-metaint` and `icy-name`, and parses `StreamTitle='Artist - Track'` blocks. It runs on its own thread and stops when the stream stops. Redirects are followed by WinHTTP within its normal limits. The reader tolerates missing headers, unterminated blocks, trailing NULs and apostrophes in titles. Stations without `icy-metaint`, including many Ogg streams, produce no song title.

`StationCatalog` parses `Data\modsdata\76radio\stations.json` with a small hand-written parser: a top-level `stations` array of objects with `name` and `streamurl`, in either key order and with or without whitespace. It skips entries without a usable HTTP or HTTPS address and reloads when the file timestamp changes. The page parses the same file independently so the list appears even before the module has published state.

Starting a stream pauses Spotify and sets `streamActive`. Starting a Spotify command stops the stream. The state document carries `stream=1` and the station name, which the HUD and panel use to show `ON AIR` and the station rather than a track.

## Communication protocol

Documents live in the game's `Data\modsdata` directory because xScal's `modStorage` maps a document name to that folder and rejects names containing a path separator. xScal's `modStorage` is reachable from the HUDMenu movie, not reliably from the Pip-Boy page, so the files are the stable contract.

| Document | Writer | Reader | Purpose |
| --- | --- | --- | --- |
| `Data\modsdata\r76radio.command.json` | Pip-Boy page or HUD relay | native module | Transport and stream commands |
| `Data\modsdata\r76radio.diag.json` | Pip-Boy page | native module | Page diagnostics, kept out of the command document |
| `Data\modsdata\r76radio.relay.json` | HUD relay | Pip-Boy page | Relay presence and build handshake |
| `Data\modsdata\76radio\r76radio.state.json` | native module | Pip-Boy page, HUD relay | Playback state and UI flags |
| `Data\modsdata\76radio\r76radio.art.png` or `.jpg` | native module | diagnostics only | Original artwork thumbnail |
| `Data\modsdata\76radio\r76radio.art.pixels` | native module | Pip-Boy page | Base64 RGBA artwork |
| `Data\modsdata\76radio\stations.json` | player | native module, page | Station list |
| `Data\XSCAL\76Radio\76Radio.ini` | player | native module | Settings |
| `Data\XSCAL\76Radio\76Radio.log` | native module | player | Diagnostics |

Commands are single-line strings:

```text
toggle
play
pause
next
previous
seek:90000
stream|https://example.invalid/live.mp3
```

The document wrapper is `{"stamp":<milliseconds>,"command":"<text>"}`. A command is executed once per new stamp. The module leaves the document in place after reading it; the next writer is responsible for a newer stamp. Commands are capped at 64 bytes except `stream|`, which is capped at 900 bytes, and the whole document at 1024 bytes. Stream addresses must start with `http://` or `https://`, must have a non-empty host, may not contain user information before the host and may not contain control characters, quotes or backslashes.

State is a flat JSON object:

```json
{
  "ready": 1,
  "host": "76radio",
  "state": "playing",
  "spotify": 1,
  "session": 1,
  "metadata": 1,
  "title": "Track",
  "artist": "Artist",
  "album": "Album",
  "canPlay": 1,
  "canPause": 1,
  "canToggle": 1,
  "canNext": 1,
  "canPrevious": 1,
  "canSeek": 1,
  "durationMs": 245000,
  "positionMs": 61000,
  "source": "Spotify.exe",
  "stream": 0,
  "station": "",
  "error": 0,
  "capturedMs": 1760000000000,
  "uiStatus": 0,
  "uiArtStatus": 0,
  "uiArtwork": 1,
  "uiKeepNowPlaying": 0,
  "uiTheme": "12FF14"
}
```

`positionMs` and `capturedMs` are excluded from the signature used to decide whether to rewrite the file, so an unchanged track produces no repeated writes. The document is written to a temporary file, flushed and moved into place. Quotes and backslashes inside metadata are replaced with apostrophes, and control characters become spaces, because the page's flat parser expects that.

`uiTheme` comes from `[Pipboy] fPipboyEffectColorR/G/B` in `Documents\My Games\Fallout 76\Fallout76Prefs.ini`. The channel re-reads the prefs file only when its timestamp changes. The theme is also mirrored to the `r76radio.theme` SharedObject by the HUD relay for the HUD card.

Diagnostics are prefixed `diag:`. The module ignores them for playback but remembers the last one, so the page can report a state the native side cannot otherwise see. Diagnostics are rate limited on the page.

## Pip-Boy ActionScript

`src/scaleform/pipboy/NewPipboy_RadioPage.as` extends `IPipBoyPage` and keeps the game's radio data contract: `PageData.StationsA` entries with `StationName`, `Frequency`, `InRange` and `Active`.

The page appends three kinds of row:

- `Spotify Radio` at frequency `0.5`.
- A non-interactive divider at frequency `0.25`.
- One row per custom station at negative frequencies, carrying `CustomUrl`.

Press and highlight handlers branch on these sentinels. A press on Spotify Radio stops any active stream when needed, sends `showcard` to the HUD and sends `toggle`. A press on a custom station stops the current source, sends `stream|<url>`, marks the row active and updates the panel optimistically while the native state catches up. A press on a game station sends `pause` if the mod is playing, hides the card and dispatches the original `RADIO::ToggleStation` event. Separator rows play the cancel sound and do nothing.

The page polls `r76radio.state.json` from four relative bases at a 250 ms tick and applies it only when the text changes. It loads `stations.json` from the same bases, decodes `r76radio.art.pixels` into a `BitmapData` (falling back to a mosaic if `setPixel` is unavailable), and scales the result into the wave area. Each of those requests is bounded by a timeout and retried without blocking the UI.

Commands use the first route that works:

1. xScal `modStorage` through the page's `__SFCodeObj` if the page can reach it.
2. A HUD message through `GlobalFunc.ShowHUDMessage`, handled by the relay.
3. The `r76radio` SharedObject, read by the relay.

Stream addresses are percent encoded for the message route because the HUD message format uses `|` as its separator. The relay reverses `%7C`, `%23` and `%25`.

The panel draws only while Spotify Radio is selected or a stream is active. It uses `$MAIN_Font` and `$MAIN_Font_Bold`, which the game maps to Roboto Condensed through `fontconfig_en.txt`, and white text so the Pip-Boy tint applies. Japanese titles are loaded from `fonts_ja.swf` when that archive is mounted. Clicking the panel returns focus to the station list so Left and Right keep changing stations.

The page reports its channel honestly: `Connected` with live data, `WAITING` before the first state document, `state file unreadable` when no base resolves, and the bridge diagnostic when only xScal was tried. It does not invent track data or claim a command was delivered when no route accepted it.

## HUD relay

`src/scaleform/hud/Radio76Hud.as` is compiled into `76RadioHud.swf` and loaded by HUDModLoader into HUDMenu. It registers `R76Radio_Hud` with `SharedHUDTools`, subscribes to `HUDColors` when available and draws a card near the game's notification group.

The relay has two jobs:

- Read `r76radio.state.json` and show the now-playing card, including the artwork, progress, live disc and Pip-Boy colour. With `KeepNowPlaying=1` the card stays visible while playing, except while a game notification is on screen.
- Take commands from the `r76radio` SharedObject and write them to xScal `modStorage` as `r76radio.command`, with the same stamp and command shape the page uses. It baselines the first stamp it sees so an old command is not replayed, and writes `r76radio.relay.json` plus SharedObject markers so the page can report `relay ok`, `relay loaded`, `relay none` or `relay no storage`.

It never sends a command it has already seen, never writes a document for a missing stamp, and sanitises quotes, backslashes and line breaks.

## Build pipeline

### Native

`CMakeLists.txt` builds static libraries for media, bridge, settings, runtime and channel, a shared `version` target with `surface_module.def`, and five test executables. C++20, `/W4`, `/permissive-`, `/EHsc`, `/utf-8`, static MSVC runtime. `WindowsApp.lib`, `windowscodecs.lib` and `winhttp.lib` provide WinRT, WIC and HTTP.

`scripts/build.ps1` configures `msvc-release`, builds, runs `ctest`, builds the relay, extracts `interface/newpipboy_radiopage.swf` from the local `SeventySix - Interface.ba2` if needed, applies the AS3 source with FFDec, exports the patched script again and checks for the state and relay markers, packs the BA2, copies the loose relay and `version.dll` into `dist`, writes `build-manifest.json` with SHA-256 hashes.

### AS3

`scripts/build-hud.ps1` finds Flex at `work\flex` or `-FlexHome`, requires Flash Player 32 `playerglobal.swc`, fetches the shared HUDModLoader classes if absent, patches `flex-config.xml` placeholders in place, compiles `Radio76Hud.as` with `-target-player=32.0 -swf-version=14`, and verifies the SWF version byte and the exported class with FFDec when available.

`scripts/fetch-hud-deps.ps1` downloads the shared classes from the HUDModLoader repository. They are third-party compile inputs and are not part of this project's source.

### BA2

`tools/ba2/ba2pack.py` writes a GNRL version 1 archive using the Bethesda hash variant. `pack` takes include prefixes, and `verify` re-reads the archive and compares hashes and payloads. `ba2inspect.py` lists and extracts entries. The build packs only `interface/newpipboy_radiopage.swf` and `interface/76RadioHud.swf`.

### Package

`scripts/package.ps1` stages only installed files, checks each hash against `dist\build-manifest.json`, writes `INSTALL.txt` and `LICENSE.txt`, compresses `dist\76Radio-0.1.0.zip` and writes `dist\SHA256SUMS.txt`. Development tools, tests and the exported module ABI are not packaged.

## Deployment

`scripts/deploy.ps1` resolves the game through Steam library folders, the Bethesda.net install, an Xbox install, an uninstall entry or a running process. It refuses to run while the game is running, identifies the extender by its exports, validates artifact hashes, checks for conflicting archive files, backs up files it replaces under `Data\XSCAL\76Radio\backup`, installs the archive, the loose relay, `version.dll` only with `-AllowRootModule version.dll`, the settings and station templates only when absent, and registers `76Radio.ba2` once in `sResourceArchive2List`. It optionally registers `76RadioHud` in `hudmodloader.ini` and the Japanese interface archive when needed.

It writes `Data\XSCAL\76Radio\deployment.json` with installed paths, hashes, backups and the INI state. `uninstall.ps1` removes only files whose hashes still match, restores backups, removes exactly one archive list entry and deletes the mod's state directory. `verify.ps1` checks hashes, BA2 round trip, installation, archive registration, log tail and state document.

`tests/ini-tests.ps1` exercises the archive list editor against temp files: byte preservation, no BOM, only `[Archive]` changed, idempotent add and exact removal.

## Security and privacy

The module is a proxy DLL with the same name as a Windows system library. That is the loading mechanism, not a hidden one. It is inspectable and rebuildable from this repository.

It does not read or write game memory, hook the game or xScal, patch game files, modify networking, disable signatures or bypass anti-cheat. It opens no listening socket. It does not download or execute code. It does not read Spotify credentials, tokens or caches. It uses only the Windows media session, which the user already exposes to the operating system.

Network access is limited to what the player asks for: the Windows media pipeline opens the station URL, and the ICY reader opens a second connection to the same URL. Spotify's own network traffic is Spotify's, not this module's.

Data written locally: the log, the state document, artwork files, the command and diagnostic documents, the relay document, the deployment manifest and the station list. The log includes the current artist and title, and for a stream the station name and URL, because those are the values being diagnosed. No data is transmitted to the project or any third party. There is no telemetry.

The log is truncated each launch and rotates at `MaxKb`. Set `[Logging] Enabled=0` to write none. Artwork is deleted by uninstall along with the rest of the state directory.

Running any process-resident module in an online game carries risk that an interface archive alone does not. The project cannot decide the publisher's position on that, and does not claim the mod is risk free or officially approved.

## Known limitations

- The patched page replaces the whole radio page SWF and conflicts with another mod that does the same.
- The page and archive are tied to a game version; a game update can invalidate the patch.
- Transport buttons and the HUD card depend on HUDModLoader and its shared classes.
- Streams cannot be paused or seeked, only stopped.
- ICY titles are absent on stations that do not send them, including many Ogg streams.
- A redirecting station can take longer to start, and a dead station takes the retry budget before it is reported stopped.
- The seek bar uses Windows' reported position; some Spotify tracks report coarse or stale positions.
- There is no volume control and no audio processing.
- Occasionally the radio page is blank the first time the Pip-Boy opens and appears normally after closing and reopening it. The trigger has not been identified.
- The mod has not been tested against every game update, every proxy DLL or every HUD replacement. Do not run two mods with the same `version.dll` name.
- `GetFileVersionInfoByHandle`, `VerFindFile` and `VerInstallFile` are forwarded but not exercised by the tests.

## Contributing

Useful contributions are reproducible bug reports with the module log, fixes for game-version changes, AS3 layout fixes at 16:9 and 21:9, and tests for the command and document contracts. Keep the C++ and AS3 sides of a contract in step, and do not add a feature that depends on a private SDK, an account or a background service.
