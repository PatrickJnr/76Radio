# Releasing 76Radio

This is the checklist for a release build. It exists so the package, checksums and VirusTotal report are produced the same way every time rather than from memory.

## Before building

1. Bump the version in `CMakeLists.txt` (`project(76Radio VERSION ...)`) and in the `$version` value at the top of `scripts\build.ps1`. The two must match.
2. Add the release to `CHANGELOG.md` under the new version. Do not rewrite the 0.1.0 entry.
3. Confirm that `config\76Radio.ini` and `config\stations.example.json` still match the parser and the documentation.

## Build and package

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\release.ps1
```

The script runs `build.ps1` (CMake configure and build, native tests, HUD relay, FFDec patch, BA2 pack), then `package.ps1` (staged files, hashes checked against the build manifest, zip, `dist\SHA256SUMS.txt`). It finishes by writing `dist\virustotal.txt`.

## VirusTotal

Every release uploads the shipped `version.dll`, because the hash changes with every build. Two builds from the same source are not byte identical, so a report from an earlier version tells a user nothing about the file they downloaded.

### Where the key lives

Get the key from the VirusTotal account menu under API key. Store it as a Windows user environment variable, outside the repository:

```powershell
setx VT_API_KEY "your key"
```

Open a new terminal after `setx`, then confirm that it is visible without printing the value:

```powershell
if ($env:VT_API_KEY) { 'key present' }
```

For a single session, `$env:VT_API_KEY = 'your key'` works instead. Never put the key in the repository, a config file, a script, a commit message or the Nexus page. `scripts\release.ps1` reads only the environment and never writes the key anywhere.

### Quotas

The standard free public API allows 1 lookup per minute, 1 lookup per day and 31 per month. An upload counts as a lookup. The script therefore uploads once and does not poll, because polling every few seconds would fail and could consume the whole allowance. Keep one upload per release.

With that allowance, uploading through the website is usually the better choice. The website upload at https://www.virustotal.com/gui/home/upload does not consume the API quota, and viewing the report in a browser does not either.

### Uploading

Manual upload, recommended for the free account:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\release.ps1
```

That builds and packages, writes the hash and both text snippets to `dist\virustotal.txt`, and does not touch the API. Upload `dist\version.dll` through the website, then confirm that the report hash matches the hash in the file.

One API request, using the stored key:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\release.ps1 -UploadToVirusTotal
```

That sends `dist\version.dll` once and prints the report URL. It does not wait for the analysis, because that needs more lookups. Open the report in a browser about a minute later and read the detection counts there.

Do not describe a build as clean because the upload finished. Read the report. A detection needs a false positive report to the vendor before release, and a report with any malicious detection blocks the release until it is explained.

## Update the public text

`release\nexus` holds the store artwork and copy locally and is ignored by Git, so those files do not appear in the public repository.

`dist\virustotal.txt` contains the version, both hashes, the report URL and two ready paragraphs.

1. Replace the existing VirusTotal paragraph in `README.md` with the README paragraph.
2. Replace the matching paragraph in `release\nexus\description.bbcode` with the Nexus paragraph.
3. Add the observed detection count and the date checked to both paragraphs, for example `VirusTotal reported 0 of 70 vendors flagging it when checked on 30 October 2026.` A count without a date ages badly.
4. Update the version and the download name anywhere the README or the Nexus page names the package.
5. Check that the SHA-256 in the text matches `dist\version.dll` and not an earlier build.
6. Add or update `release\nexus\changelog-<version>.txt` with a condensed, one-entry-per-line changelog for the Nexus version field. The repository `CHANGELOG.md` is deliberately more detailed.

## Verify the package

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify.ps1
```

The repository section must pass. The installation section reports the modules, documents and archive registration of the local game, and a hash mismatch there only means that the installed copy is not the one just built.

Extract `dist\76Radio-<version>.zip` to a temporary folder and confirm the layout:

```text
Data\76Radio.ba2
Data\interface\76RadioHud.swf
Data\XSCAL\76Radio\76Radio.ini
Data\modsdata\76radio\stations.json
version.dll
INSTALL.txt
LICENSE.txt
```

`dist\SHA256SUMS.txt` lists the archive, the relay, the module and the zip. Publish it beside the package.

## Checking CI locally

`scripts\ci.ps1` runs the same configure, build, native tests and archive-list checks as `.github/workflows/native.yml`:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\ci.ps1
```

`act` cannot run this workflow locally because it only provides Linux runners, while the build is MSVC and Windows only. For workflow syntax, use [actionlint](https://github.com/rhysd/actionlint) from the repository root: `actionlint`. Test the first real run in a private repository if you want no public failed runs; individual failed runs can be removed with `gh run delete <run-id>` without deleting the repository.

## Commit and tag

Commit after the documentation matches the artifacts. For a normal release the commit and tag are ordinary history:

```powershell
git add README.md CHANGELOG.md release\nexus\description.bbcode
git commit -m "<version>"
git tag -a "v<version>" -m "76Radio <version>"
```

Pushing the tag starts `.github/workflows/release-draft.yml`, which creates a draft GitHub Release titled `76Radio <version>`. The body combines the intro and footer in `.github/release-notes-template.md` with that version's section from `CHANGELOG.md`. It does not build or attach the package. The BA2 needs the player's interface archive, FFDec and Flex, so upload `dist\76Radio-<version>.zip` and `dist\SHA256SUMS.txt` to the draft, check them, then publish it.

A fully automatic build would need a self-hosted Windows runner with Fallout 76 and the build tools installed. The hosted workflow deliberately stops at validation.

The 0.1.0 history was created as a single root commit and the tag points at it. Later releases add commits normally; do not rewrite published history.

## Publish

Publishing is a separate decision. Pushing a tag creates the draft release; publishing it, uploading to Nexus and making the repository public are done only when the project owner asks for them. The release script never uploads anywhere except VirusTotal.
