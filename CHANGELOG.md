# Changelog

All notable changes to 76Radio are recorded here. The first public version is 0.1.0.

## 0.2.0

Moves the native module from a `version.dll` proxy to an xScal plugin. Nothing sits beside
`Fallout76.exe` any more: xScal loads `Data\XSCAL\Plugins\76Radio.dll` and checks its
signature before the plugin runs.

### Native module

- Replaces the `version.dll` proxy with a signed xScal plugin.
- Keeps Spotify controls, artwork, internet stations and settings working as before.

### Internet radio

- Resolves station addresses on a background thread, so selecting a slow or unreachable station no longer holds up the Pip-Boy. Stopping, switching station or source, or closing the game cancels a lookup in progress, and a cancelled lookup can no longer start playback.
- Reports connecting, buffering, playing, reconnecting, failed and unsupported states from the player, so a station is no longer shown as playing before audio starts.
- Reconnects with a fresh media source and metadata reader after an interruption, with four attempts spaced 2, 4, 8 and 16 seconds apart.
- Separates unsupported media formats from network failures.
- Resolves M3U, M3U8 and PLS station links, with limits on redirects, playlist depth, size and candidates. HLS playlists are handed to Windows unchanged and have no ICY metadata.
- Reads ICY metadata with case-insensitive field names and UTF-8, Windows-1252 and ISO-8859-1 text, and clears it when a stream stops.
- Falls back to the ICY station name when a station has no configured name.
- Follows the game's Pip-Boy Radio and master volume sliders, read from the game's settings file, so internet stations change volume with the rest of the radio.
- Writes the 76Radio version at the top of the log and a time on every line, and no longer logs the routine theme notice.
- Reloads `stations.json` while the game runs, keeps the selection, and keeps the previous list when the file is invalid.

### Windows Media

- Adds an entry that controls a non-Spotify media session, separate from `Spotify Radio`. It is named after the detected application, such as Chrome or Edge, and never replaces Spotify control by itself. The entry is marked `[BETA]`, since not every application publishes a media session.
- Shows the source application on the status line and cycles through the open sessions when it is clicked.
- Shows `Spotify Radio` only while Spotify is running and the Windows Media entry only while another session exists.

### Tests

- Adds loopback fixture tests for the playlist resolver, the ICY reader and the stream player, including slow, cancelled and replaced lookups.

### Requirements

- xScal 0.4.22 or later.

## 0.1.0

Initial release.

### Pip-Boy

- Adds a `Spotify Radio` entry to the RADIO list alongside the game's stations.
- Adds custom internet stations from `Data\modsdata\76radio\stations.json` below a divider.
- Supports keyboard, controller and mouse selection without changing the game's existing radio entries.
- Shows track title, artist, album, playback state, elapsed and total time.
- Shows album artwork from the Windows media session, with the radio wave strip as a fallback.
- Provides previous, play or pause and next controls, plus a seek bar for Spotify tracks.
- Marks the station that is currently playing.
- Pauses Spotify when a stream starts and closes the stream when Spotify or a game station takes over.
- Follows the Pip-Boy interface colour.
- Reports connection, waiting and unavailable states instead of showing stale or invented data.

### Internet radio

- Plays HTTP and HTTPS stations through the Windows media pipeline.
- Reads ICY station and song metadata where the station provides it.
- Retries a dropped stream a bounded number of times.
- Skips malformed station entries while keeping the rest of the list.
- Reloads the station list when the file changes.

### HUD

- Adds a now-playing card beside the game's notifications.
- Keeps the card in step with the current track and the Pip-Boy colour.
- Adds an optional persistent mini player with `KeepNowPlaying=1`.
- Steps aside while a game notification is visible.
- Carries transport commands from the Pip-Boy to the native module through HUDModLoader.

### Native module

- Adds `version.dll`, loaded by the game because the executable imports that name.
- Forwards all seventeen system `version.dll` entry points to the real system library.
- Controls Spotify through Windows Global System Media Transport Controls, with no Spotify account, token or Web API use.
- Runs media work on a worker thread with immutable snapshots and a bounded command queue.
- Handles Spotify starting, stopping, changing track or losing its media session.
- Stops cleanly without leaving threads or blocked calls behind, and reports a media call that refuses to return.
- Writes artwork and state for the Pip-Boy and HUD without sending anything off the machine.

### Configuration and diagnostics

- Adds `Data\XSCAL\76Radio\76Radio.ini` for logging, startup delay, polling intervals and UI options.
- Writes a readable log that is truncated each launch and rotates once at a configurable size.
- Provides deployment, verification, packaging and uninstall scripts that detect the game, validate hashes, back up replaced files and refuse to overwrite files they do not own.
- Produces `dist\76Radio-0.1.0.zip` with SHA-256 checksums.

### Requirements

- Fallout 76 on Windows.
- xScal for the shared document store.
- HUDModLoader for the HUD card and transport relay.
- The Spotify desktop client for Spotify Radio.

### Known limitations

- Another mod that replaces the Pip-Boy radio page SWF will conflict.
- A game update can require a new build of the patched page.
- Streams can be stopped but not paused or seeked.
- ICY song titles are available only when the station sends them.
- Occasionally the RADIO page is blank the first time the Pip-Boy opens; closing and reopening shows it normally.
- There is no volume control.
