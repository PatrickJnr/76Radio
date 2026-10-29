# Third-party notices

76Radio is unofficial and is not endorsed by Bethesda, Microsoft or Spotify. This file records the external tools and sources used to build it.

## Build tools

None of these are committed or packaged.

| Tool | Used for | Licence | Source |
| --- | --- | --- | --- |
| Apache Flex SDK 4.16.1 | `mxmlc` builds the HUD relay SWF | Apache License 2.0 | [archive.apache.org](https://archive.apache.org/dist/flex/4.16.1/) |
| JPEXS Free Flash Decompiler | Patching and verifying Scaleform SWFs | GNU General Public License v3.0 | [github.com/jindrapetrik/jpexs-decompiler](https://github.com/jindrapetrik/jpexs-decompiler) |
| CMake 3.25 or later | Native build | BSD 3-Clause | [cmake.org](https://cmake.org) |
| Visual Studio 2026 | Native build with MSVC | Microsoft licence | [visualstudio.microsoft.com](https://visualstudio.microsoft.com) |

The Flash Player 32.0 `playerglobal.swc` that `mxmlc` links against comes from Adobe's Flash Player downloads and remains subject to Adobe's terms.

## Shared ActionScript

The HUD relay compiles against shared classes from [HUDModLoader](https://github.com/GitCrazy-wc/hudmodloader): `SharedHUDTools`, `Shared.HUDModes`, the `Shared.AS3.Data` classes, `Shared.AS3.Events.CustomEvent` and `com.adobe.serialization.json`. They are downloaded into `src\scaleform\hud` at build time by `scripts\fetch-hud-deps.ps1`, are not part of this project's source and are not committed here. The JSON classes originate in [as3corelib](https://github.com/mikechambers/as3corelib). Check each upstream repository for its own terms.

## Runtime dependencies

[xScal](https://github.com/DCHoaxer/xScal) by DCHoaxer provides the shared document store. It is installed separately by the player, is not modified or redistributed here, and remains subject to its own source-available licence.

## Game interface script

`src\scaleform\pipboy\NewPipboy_RadioPage.as` is a modified version of the radio page ActionScript from the player's installed `SeventySix - Interface.ba2`. It is kept so the patch can be reproduced and checked locally, and the patched SWF is built from that player's own archive. The original script remains Bethesda's.

## Not included

xScal (`dxgi.dll`), HUDModLoader, Fallout 76, Spotify and the Windows media components are installed separately by the player. 76Radio does not modify or redistribute them.
