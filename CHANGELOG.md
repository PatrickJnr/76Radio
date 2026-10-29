# Changelog

All notable changes to 76Radio are recorded here. The first public version is 0.1.0.

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
