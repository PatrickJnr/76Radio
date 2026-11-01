# Releasing 76Radio

This is the checklist for a release build. It exists so the package and checksums are produced the same way every time rather than from memory.

## Before building

1. Bump the version in `CMakeLists.txt` (`project(76Radio VERSION ...)`) and in the `$version` value at the top of `scripts\build.ps1`. The two must match.
2. Confirm that `XSCAL_SIGNING_KEY` and `XSCAL_SIGNING_CERTIFICATE` point at the signing key and certificate, and that the certificate covers the `patrickjnr_` mod-author prefix used by the `patrickjnr_76radio` plugin ID. The package step refuses an unsigned plugin.
3. Add the release to `CHANGELOG.md` under the new version. Do not rewrite the 0.1.0 entry.
4. Confirm that `config\76Radio.ini` and `config\stations.example.json` still match the parser and the documentation.

## Build and package

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\release.ps1
```

The script runs `build.ps1` (CMake configure and build with the xScal SDK, native tests, HUD relay, FFDec patch, BA2 pack), then `package.ps1` (staged files, hashes checked against the build manifest, zip, `dist\SHA256SUMS.txt`). It finishes by printing the hashes of the plugin and the package.

## Update the public text

`release\nexus` holds the store artwork and copy locally and is ignored by Git, so those files do not appear in the public repository.

1. Update the version and the download name anywhere the README or the Nexus page names the package.
2. Check that `README.md`, `INSTALL.txt` (the text in `scripts\package.ps1`) and `release\nexus\description.bbcode` describe the same settings, hotkeys and requirements as `config\76Radio.ini` and `CHANGELOG.md`.
3. Add or update `release\nexus\changelog-<version>.txt` with a condensed, one-entry-per-line changelog for the Nexus version field. The repository `CHANGELOG.md` is deliberately more detailed.
4. Do not rebuild after packaging. Two builds from the same source are not byte identical, so a rebuild changes the hashes in `dist\SHA256SUMS.txt` and the signature file.

## Verify the package

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify.ps1
```

The repository section must pass. The installation section reports the modules, documents and archive registration of the local game, and a hash mismatch there only means that the installed copy is not the one just built.

Extract `dist\76Radio-<version>.zip` to a temporary folder and confirm the layout:

```text
Data\76Radio.ba2
Data\interface\76RadioHud.swf
Data\XSCAL\Plugins\76Radio.dll
Data\XSCAL\Plugins\76Radio.dll.sig
Data\XSCAL\76Radio\76Radio.ini
Data\modsdata\76radio\stations.json
INSTALL.txt
LICENSE.txt
```

`dist\SHA256SUMS.txt` lists the archive, the relay, the plugin, its signature and the zip. Publish it beside the package.

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

To test the template without creating a tag, run the `Release draft` workflow from the Actions tab and enter the version to compose. A manual run writes the composed body to the job summary and skips the release creation step.

A fully automatic build would need a self-hosted Windows runner with Fallout 76 and the build tools installed. The hosted workflow deliberately stops at validation.

The 0.1.0 history was created as a single root commit and the tag points at it, and 0.2.0 is prepared the same way. Do not rewrite history once a release has been published.

## Publish

Publishing is a separate decision. Pushing a tag creates the draft release; publishing it, uploading to Nexus and making the repository public are done only when the project owner asks for them. The release script never uploads anywhere.
