# 76Radio technical reference

This document describes the implementation in this repository. It is written from the source, not from earlier design notes. It covers the native module, the Windows media integration, the document channel, the two Scaleform movies, the build pipeline and the security boundaries.

## Architecture

```text
Fallout76.exe
  dxgi.dll                           xScal extender, installed by the player
    Data\XSCAL\Plugins\76Radio.dll   76Radio plugin
      worker thread                  settings, media runtime and document channel
        MediaService                 immutable snapshots, bounded command queue
          Windows GSMTC              Spotify session found by application id
          StreamPlayer               WinRT MediaPlayer for internet stations
          IcyMetadataReader          second connection, station and song text
        DocumentChannel              polls commands, writes state, reads Pip-Boy colour
    modStorage                       named JSON documents under Data\modsdata
  interface\newpipboy_radiopage.swf  patched Pip-Boy RADIO page
  interface\76RadioHud.swf           HUD relay, loaded by HUDModLoader
```

The plugin is the only native host. It registers three Scaleform callbacks, but the movies and the plugin still meet through files: the page and the HUD read and write the same documents as before. The xScal movie interface is available for a later step and is not used yet.

| Piece | Language | Role |
| --- | --- | --- |
| `src/native/plugin` | C++20 | xScal entry point, Scaleform callbacks and worker lifetime |
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
src/native/{media,bridge,runtime,channel,settings,plugin,testing}
src/scaleform/pipboy        NewPipboy_RadioPage.as
src/scaleform/hud           HudRelay.as and shared HUDModLoader classes
config                      76Radio.ini and stations.example.json
scripts                     build, HUD build, dependency fetch, deploy, verify, package, uninstall
tools/ba2                   ba2pack.py and ba2inspect.py
tests                       PowerShell tests for the archive list editor
assets/branding/banner.png  README banner
```

`src/native/testing` is shared test support, not a shipped executable. `work` and `dist` are generated and ignored. `tools/vendor` holds FFDec and is ignored. The remaining branding artwork and `release` hold local release material and are not tracked.

## Native module

### Plugin loading

xScal scans `Data\XSCAL\Plugins\*.dll` in filename order and calls each plugin's `XScalPlugin_Query`, then `load`. 76Radio is built against plugin ABI 5 with `xscal_add_plugin`, so the DLL has a static C runtime and no dependency on a redistributable. The plugin ID is `patrickjnr_76radio`; the ID must start with the mod author's prefix in the developer certificate (`patrickjnr_`) or xScal rejects the plugin after querying it.

xScal loads only a DLL that has `76Radio.dll.sig` beside it, made for that exact build. The maintainer's certificate (`patrickjnr.xcert`) and key are private and are not in this repository or the package, so only the maintainer can produce a DLL that loads under the `patrickjnr_76radio` ID. Contributors can build and test everything else, and a fork needs its own certificate and author prefix. A missing signature, a signature from a different build or a revoked certificate is written to `xscal.log` with the reason; the plugin does not fall back to another loading mechanism.

`XScalPlugin_Query` only fills the descriptor and registers the callbacks `76Radio.GetState`, `76Radio.Command` and `76Radio.Version`. No work starts there.

### Lifetime

`load` checks the host with `xscal::isHostUsable`, resolves the executable directory from the running process and reads `Data\XSCAL\76Radio\76Radio.ini`. It then starts one detached worker thread and returns. xScal never unloads a plugin, so there is no unload callback and no process-detach path; the worker runs until the process exits.

The worker:

1. Resets `Data\XSCAL\76Radio\76Radio.log` when logging is enabled.
2. Waits for the configured startup delay before the first Windows media call.
3. Constructs the media runtime and starts its worker.
4. Stores the runtime pointer for the Scaleform callbacks.
5. Constructs the document channel and starts its worker.
6. Sleeps in one second steps for the lifetime of the process.

If the media service cannot start, or the channel cannot start, the worker logs the reason and stops; the plugin stays loaded but the callbacks report the runtime as unavailable. A media stop timeout is not reachable because the plugin has no shutdown path; the process exit reclaims both threads.

The plugin logs its version, process id, executable directory, parsed settings, document paths and every accepted or rejected command, each line with a local timestamp. The log is truncated each launch and rotates once at `MaxKb`.

### Scaleform callbacks

ActionScript calls them as `__SFCodeObj.call("76Radio.GetState")` and so on. They run on the ActionScript thread and use one thread-local string buffer each, because xScal only keeps a returned string pointer valid for the duration of the callback.

| Callback | Arguments | Returns |
| --- | --- | --- |
| `76Radio.GetState` | none | The same `key=value` state payload `NativeRuntime::StatePayload` produces |
| `76Radio.Command` | one command string | `accepted`, or the outcome code and detail, for example `unsupported: session does not support seek` |
| `76Radio.Version` | none | The plugin version string |

A command goes through the same `bridge::InterpretCommand`, capability check and bounded queue as the document channel, so both routes cannot disagree about what was accepted.

### Settings

The xScal PLUGINS menu is not wired to these settings yet; the plugin has no options provider, so all settings remain in the INI. An options provider in a later release will write the same file.

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

`StreamPlayer` uses WinRT `Windows.Media.Playback.MediaPlayer` with command manager integration disabled and audio category `Media`. `Start` records the address, publishes `Connecting` and returns at once. A worker thread resolves the address with `ResolveStreamUrl`, and `State`, which runs on the media worker, collects the result and only then creates a fresh `MediaPlayer` and `MediaSource`. Each attempt carries a generation number, so a callback from a replaced attempt is ignored.

Resolution is cancellable. `ResolveCancellation` holds the in-flight WinHTTP request handle, and `Stop`, a new `Start`, a switch to another source and `Shutdown` close that handle from the calling thread, which aborts the blocking call. A cancelled worker is retired and joined later, not detached, so no task outlives the player. `Start` and `Stop` measured under 1 ms against a stalled fixture server; total cleanup after a cancelled lookup is bounded by the thread exiting, which also measured under 1 ms. The startup budget is 20 seconds for the lookup plus the first audio, after which the attempt counts as a failure.

States are `Stopped`, `Connecting`, `Buffering`, `Playing`, `Paused`, `Reconnecting`, `Failed` and `Unsupported`. `Playing` is reported only when the player's playback session says so. A network failure schedules a reconnect after 2, 4, 8 and 16 seconds, each with a new `MediaSource` and a new ICY reader, and the retry counter resets once audio plays. A media failure with `MF_E_UNSUPPORTED_BYTESTREAM_TYPE`, `MF_E_INVALIDMEDIATYPE`, `MF_E_TOPO_CODEC_NOT_FOUND` or `MF_E_UNSUPPORTED_FORMAT` is classified as `Unsupported` and is not retried; other failures, including refused connections, are network failures.

`ResolveStreamUrl` uses WinHTTP with a 5 second connect timeout, an 8 second request timeout, five redirects, a 256 KB body limit, 32 candidates and two levels of playlist nesting. It detects M3U, M3U8, PLS and HLS from the content type, the file name or the body. A playlist yields candidates that are tried in order; a loop or a repeated address is not followed twice. HLS is handed to Windows as the playback address with no metadata address. A non-HTTP scheme is `Unsupported`. If the first request for a non-playlist address fails, the original address is still handed to Windows, which reports the real failure.

`IcyMetadataReader` opens its own HTTP connection with `WinHTTP`, requests `Icy-MetaData: 1`, reads `icy-metaint` and `icy-name` with case-insensitive header names, and parses `StreamTitle='Artist - Track'` blocks as UTF-8, falling back to Windows-1252. It runs on its own thread, stops when the stream stops, and clears its title and station name when stopped. The reader tolerates missing headers, unterminated blocks, trailing NULs and apostrophes in titles. Stations without `icy-metaint`, including many Ogg streams, produce no song title. When a station has no configured name, the `icy-name` value becomes the station name.

`StationCatalog` parses `Data\modsdata\76radio\stations.json` with a small JSON reader: a top-level `stations` array of objects with `name` and `streamurl` (or `url`), with other keys ignored. It skips entries without a usable HTTP or HTTPS address, drops repeated addresses, caps the list at 256 entries, and reloads when the file timestamp changes. An invalid file keeps the previous list. The page parses the same file independently, using a string-based parser because the Scaleform runtime has no `JSON` class, re-reads it every ten polls, compares the document text, and keeps the current selection when the list changes.

The stream volume follows the game's audio settings. The backend checks the timestamp of `Documents\My Games\Fallout 76\Fallout76Prefs.ini` on each poll and, when it changes, reads the `[AudioMenu]` section. The slider whose `uID` is 138006 (observed to be the Pip-Boy Radio slider) is multiplied by `fAudioMasterVolume`, clamped to 0 to 1 and applied to the `MediaPlayer` volume, including players created later by a reconnect. The game writes the file as the slider moves. A missing file or entry leaves the volume at full and logs it. The id was identified by changing the slider in-game and comparing files, and changing the volume in the game settings was confirmed to change the stream volume in-game. Because the id is observed and not documented, a game update that renumbers the categories would need it checked again.

Starting a stream pauses Spotify and sets `streamActive`. Starting a Spotify command stops the stream. The state document carries `stream=1` and the station name, which the HUD and panel use to show `ON AIR` and the station rather than a track.

### Windows Media sessions

The backend has two modes. In the default mode `FindSpotifySession` selects only Spotify. A `media|auto` command selects general mode, where `FindGeneralSession` picks the current non-Spotify session, preferring one that is playing, and remembers its application id. `media|next` moves to the next non-Spotify session in the session list, `media|spotify` returns to Spotify mode, and `next` does nothing outside general mode. In general mode the remembered application id is the only session used, so a session that closes leaves the panel with no session instead of switching to another application. The state document reports `genericMedia`, `genericAvailable` (whether a non-Spotify session exists), `genericSource` (the application id the entry would control) and `source`. The page maps `genericSource` to a short name and labels the entry with it. It shows the `Spotify Radio` entry only while `spotify` is 1, and the media entry only while `genericAvailable` is 1 or general mode is active. These commands contain `|`, so the page escapes it as `%7C` for the HUD route, and the relay decodes the `stream` and `media` prefixes before writing the command document.

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
media|spotify
media|auto
media|next
```

The document wrapper is `{"stamp":<milliseconds>,"command":"<text>"}`. A command is executed once per new stamp. The module leaves the document in place after reading it; the next writer is responsible for a newer stamp. Commands are capped at 64 bytes except `stream|`, which is capped at 900 bytes, and the whole document at 1024 bytes. Stream addresses must start with `http://` or `https://`, must have a non-empty host, may not contain user information before the host and may not contain control characters, quotes or backslashes.

State is a flat JSON object:

```json
{
  "ready": 1,
  "host": "76radio",
  "state": "playing",
  "spotify": 1,
  "genericMedia": 0,
  "genericAvailable": 0,
  "genericSource": "",
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

The page appends four kinds of row:

- A non-interactive divider at frequency `0.25`.
- `Spotify Radio` at frequency `0.5`, present only while the state document reports Spotify running.
- The media entry at frequency `0.75`, present only while the state document reports a non-Spotify session or general mode. It is labelled with the detected application and the `[BETA]` suffix.
- One row per custom station at negative frequencies, carrying `CustomUrl`.

The list is rebuilt, keeping the selection, when the Spotify or media entry appears or disappears, and the media entry's label changes in place.

Press and highlight handlers branch on these sentinels. A press on Spotify Radio stops any active stream when needed, sends `showcard` to the HUD and sends `media|spotify` if the page is in general mode, otherwise `toggle`. A press on the media entry sends `media|auto` when not already in general mode, otherwise `toggle`; the status line then reads `SOURCE: name >`, and a click on it sends `media|next`. A press on a custom station stops the current source, sends `stream|<url>`, marks the row active and updates the panel optimistically while the native state catches up. A press on a game station sends `pause` if the mod is playing, hides the card and dispatches the original `RADIO::ToggleStation` event. Separator rows play the cancel sound and do nothing.

The page polls `r76radio.state.json` from four relative bases at a 250 ms tick and applies it only when the text changes. It loads `stations.json` from the same bases with a string-based parser, because the Scaleform runtime has no `JSON` class, re-reads it every ten polls and rebuilds the list in place only when the text changed, decodes `r76radio.art.pixels` into a `BitmapData` (falling back to a mosaic if `setPixel` is unavailable), and scales the result into the wave area. Each of those requests is bounded by a timeout and retried without blocking the UI.

Commands use the first route that works:

1. xScal `modStorage` through the page's `__SFCodeObj` if the page can reach it.
2. A HUD message through `GlobalFunc.ShowHUDMessage`, handled by the relay.
3. The `r76radio` SharedObject, read by the relay.

Stream and media commands are percent encoded for the message route because the HUD message format uses `|` as its separator. The relay reverses `%7C`, `%23` and `%25` for the `stream` and `media` prefixes. The page and the relay both carry `RELAY_BUILD`, which must match, so a page paired with an older relay does not treat the relay as present.

The panel draws only while Spotify Radio or the media entry is selected, or a stream is active. It uses `$MAIN_Font` and `$MAIN_Font_Bold`, which the game maps to Roboto Condensed through `fontconfig_en.txt`, and white text so the Pip-Boy tint applies. Japanese titles are loaded from `fonts_ja.swf` when that archive is mounted. Clicking the panel returns focus to the station list so Left and Right keep changing stations.

The page reports its channel honestly: `Connected` with live data, `WAITING` before the first state document, `state file unreadable` when no base resolves, and the bridge diagnostic when only xScal was tried. It does not invent track data or claim a command was delivered when no route accepted it.

## HUD relay

`src/scaleform/hud/HudRelay.as` is compiled into `76RadioHud.swf` and loaded by HUDModLoader into HUDMenu. It registers `R76Radio_Hud` with `SharedHUDTools`, subscribes to `HUDColors` when available and draws a card near the game's notification group.

The relay has two jobs:

- Read `r76radio.state.json` and show the now-playing card, including the artwork, progress, live disc and Pip-Boy colour. With `KeepNowPlaying=1` the card stays visible while playing, except while a game notification is on screen.
- Take commands from the `r76radio` SharedObject and write them to xScal `modStorage` as `r76radio.command`, with the same stamp and command shape the page uses. It baselines the first stamp it sees so an old command is not replayed, and writes `r76radio.relay.json` plus SharedObject markers so the page can report `relay ok`, `relay loaded`, `relay none` or `relay no storage`.

It never sends a command it has already seen, never writes a document for a missing stamp, and sanitises quotes, backslashes and line breaks.

## Build pipeline

### Native

`CMakeLists.txt` builds static libraries for media, bridge, settings, runtime and channel, the `76Radio` plugin target and five test executables. C++20, `/W4`, `/permissive-`, `/EHsc`, `/utf-8`, static MSVC runtime. `WindowsApp.lib`, `windowscodecs.lib` and `winhttp.lib` provide WinRT, WIC and HTTP.

The plugin target needs the xScal SDK. Configure with `-DXSCAL_PLUGIN_SDK_DIR=<sdk>/cmake`, or pass `-DBUILD_XSCAL_PLUGIN=OFF` to build the libraries and tests without it. Public CI uses the second form, because the SDK and signing key are not redistributed; a release build supplies both locally. Set `XSCAL_SIGNING_KEY` and `XSCAL_SIGNING_CERTIFICATE` so the post-build step writes `76Radio.dll.sig`; without them CMake warns and the DLL will not load in xScal.

`scripts/build.ps1` takes `-XScalSdk`, configures `msvc-release`, builds, runs `ctest`, builds the relay, extracts `interface/newpipboy_radiopage.swf` from the local `SeventySix - Interface.ba2` if needed, applies the AS3 source with FFDec, exports the patched script again and checks for the state and relay markers, packs the BA2, copies the loose relay and `76Radio.dll` with its `.sig` into `dist`, and writes `build-manifest.json` with SHA-256 hashes.

### AS3

`scripts/build-hud.ps1` finds Flex at `work\flex` or `-FlexHome`, requires Flash Player 32 `playerglobal.swc`, fetches the shared HUDModLoader classes if absent, patches `flex-config.xml` placeholders in place, compiles `HudRelay.as` with `-target-player=32.0 -swf-version=14`, and verifies the SWF version byte and the exported class with FFDec when available.

`scripts/fetch-hud-deps.ps1` downloads the shared classes from the HUDModLoader repository. They are third-party compile inputs and are not part of this project's source.

### BA2

`tools/ba2/ba2pack.py` writes a GNRL version 1 archive using the Bethesda hash variant. `pack` takes include prefixes, and `verify` re-reads the archive and compares hashes and payloads. `ba2inspect.py` lists and extracts entries. The build packs only `interface/newpipboy_radiopage.swf` and `interface/76RadioHud.swf`.

### Package

`scripts/package.ps1` stages only installed files, checks each hash against `dist\build-manifest.json`, writes `INSTALL.txt` and `LICENSE.txt`, compresses `dist\76Radio-<version>.zip` and writes `dist\SHA256SUMS.txt`, including the plugin and its signature. Development tools, tests and the exported module ABI are not packaged.

## Deployment

`scripts/deploy.ps1` resolves the game through Steam library folders, the Bethesda.net install, an Xbox install, an uninstall entry or a running process. It refuses to run while the game is running, identifies the extender by its exports, validates artifact hashes, refuses a plugin without a signature, checks for conflicting archive files, backs up files it replaces under `Data\XSCAL\76Radio\backup`, installs the archive, the loose relay, the signed plugin under `Data\XSCAL\Plugins`, the settings and station templates only when absent, and registers `76Radio.ba2` once in `sResourceArchive2List`. It removes an old `version.dll` only when the previous deployment manifest owns it and its hash still matches. It optionally registers `76RadioHud` in `hudmodloader.ini` and the Japanese interface archive when needed.

It writes `Data\XSCAL\76Radio\deployment.json` with installed paths, hashes, backups and the INI state. `uninstall.ps1` removes only files whose hashes still match, restores backups, removes exactly one archive list entry and deletes the mod's state directory. `verify.ps1` checks hashes, BA2 round trip, installation, archive registration, log tail and state document.

`tests/ini-tests.ps1` exercises the archive list editor against temp files: byte preservation, no BOM, only `[Archive]` changed, idempotent add and exact removal.

## Security and privacy

The module is an xScal plugin in `Data\XSCAL\Plugins`, loaded by the extender the player already installed. There is no proxy DLL and nothing is placed beside `Fallout76.exe` with a system library's name. The plugin is inspectable and rebuildable from this repository, although loading it needs a signature from the xScal maintainer.

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
- Switching between Windows media sessions uses a mouse click on the source line, and not every application publishes a session.
- ICY titles are absent on stations that do not send them, including many Ogg streams.
- A redirecting station can take longer to start, and a dead station takes the retry budget before it is reported `FAILED`.
- The seek bar uses Windows' reported position; some Spotify tracks report coarse or stale positions.
- Only internet streams follow the game's volume sliders, through an observed category id. There is no other audio processing.
- Occasionally the radio page is blank the first time the Pip-Boy opens and appears normally after closing and reopening it. The trigger has not been identified.
- The signed plugin was loaded by xScal 0.4.27 on Fallout 76 1.7.27.11. Live Spotify metadata and pause/play transport through the document channel were verified, but the mod has not been tested against every game update, xScal release or HUD replacement.
- The Scaleform callbacks are registered but no ActionScript in this repository calls them yet. The page and HUD still use the document files.
- A plugin update must replace `76Radio.dll` and `76Radio.dll.sig` together. Replacing only the DLL makes xScal reject it.

## Contributing

Useful contributions are reproducible bug reports with the module log, fixes for game-version changes, AS3 layout fixes at 16:9 and 21:9, and tests for the command and document contracts. Keep the C++ and AS3 sides of a contract in step, and do not add a feature that depends on an account or a background service. The xScal plugin SDK is the one external build input, and it stays out of the repository.
