# Contributing to 76Radio

Issues and pull requests are welcome. This is an unofficial mod for an online game, so changes are reviewed with the game, the other installed mods and the player's installation in mind.

## Reporting a bug

Include:

- The Fallout 76 version, from the in-game menu: press Esc, then Z, open Settings, and read it at the bottom left.
- The xScal version and HUDModLoader version, if used.
- Whether `version.dll` is installed beside `Fallout76.exe`.
- The relevant lines from `Data\XSCAL\76Radio\76Radio.log`, starting with `76Radio surface module starting`.
- What you expected and what happened.
- A screenshot of the Pip-Boy or HUD state if the problem is visual.

Remove your Windows user name and any personal folder names from pasted logs if you prefer not to share them.

Do not report a security problem in a public issue. Follow [SECURITY.md](SECURITY.md) instead.

## Pull requests

Keep a pull request focused on one problem. Describe the behaviour before and after, and say what you tested. For a UI change, include screenshots at 16:9 and, where possible, 3440 by 1440.

The C++ and ActionScript sides share a contract. If you change a command, a state field, a document path or a timing rule, change both sides and the tests in the same pull request. The contract is described in [README-TECHNICAL.md](README-TECHNICAL.md).

Do not add:

- A companion application, launcher, service or background updater.
- Spotify account access, OAuth, API keys or credential storage.
- A new injection or mod-loading mechanism.
- A dependency on a private SDK or a signature the project cannot provide.
- Game assets, extracted Bethesda files, proprietary SDK files or third-party binaries.
- Debug traces left in place, or comments that only narrate code.
- Telemetry, analytics or network calls the player did not ask for.

## Building and testing

Follow the build prerequisites in [README.md](README.md). A change to native code should pass:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

or, for the native targets alone:

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release
ctest --preset msvc-release
```

A change to the HUD source also needs `scripts\build-hud.ps1`. A change to the archive list editor should pass `tests\ini-tests.ps1`.

State clearly what you could not test. A compiler success or an out-of-game test is not an in-game verification, and the project does not describe it as one.

## AI assistance

If an AI assistant helped with a pull request, say so in the description and name what it helped with: research, code, tests or documentation. Disclosure is not a rejection. It helps the review focus on the right parts and keeps the project's account of its authorship accurate. You remain responsible for the change, its licence and the testing behind it.

## Releases

Maintainers follow [RELEASING.md](RELEASING.md). It covers the version bump, the release script, the VirusTotal upload and the text that has to be updated before a tag is created.

## Licence and contributions

The source is published so it can be inspected and built. The licence is the PolyForm Noncommercial License 1.0.0, a source-available noncommercial licence; by contributing you agree that your contribution may be distributed under those terms. Do not submit code you do not have the right to contribute.

Third-party tools and shared ActionScript are recorded in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). Keep those notices accurate if you change a build dependency.

The project does not maintain or endorse third-party builds of 76Radio. Official packages come only from the project owner.
