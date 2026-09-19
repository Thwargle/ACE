# AC:VR — native Quest client

This folder contains the Quest configuration and reproducible build/install tools.
Shared client code lives in `../Unreal`. `Build-Quest.ps1` calls
`Sync-ClientSource.ps1` to populate the ignored `Project/Source`, `Project/Plugins`,
and `Project/Content` folders from the canonical source. Do not edit or commit those
generated copies. Runtime UI JSON and test fixtures are included in synchronization.

See [source checkout setup](../SOURCE_CHECKOUT.md) for .NET, Unreal, SDK/NDK, JDK,
server setup, and Windows builds. Build Windows first if runtime materials changed.

In PowerShell 7:

```powershell
.\Build-Quest.ps1
.\Sync-ClientSource.ps1 -CheckOnly
```

The APK is written to `Packaged/Android_ASTC/ACUnreal-arm64.apk`. The application ID
is `com.acecommunity.questtest`, displayed as **AC:VR**. Updates preserve existing installations and encrypted login data. The build checks
the APK version against `Project/Config/DefaultEngine.ini` before reporting success.

Put the Android toolchain in the ignored `AndroidSDK` directory, or pass
`-AndroidSdkRoot <path>` to the build script. Installation/test/sharing helpers
expect `AndroidSDK/platform-tools/adb.exe` and `AndroidSDK/build-tools/35.0.1/aapt.exe`;
none of these external tools are checked in.

With Developer Mode enabled and USB debugging accepted in the headset:

```powershell
.\Install-Quest.ps1 -DatDirectory "C:\Turbine\Asheron's Call"
# Later updates, preserving installed game data and settings:
.\Install-Quest.ps1 -SkipData
```

Keep the headset awake when launching. Open **Apps > Unknown Sources > AC:VR**. Enter an ACE server address reachable from the headset; the server runs on
the PC, not inside the Quest. Configure your own local server credentials separately.
The guarded `Start-Server.ps1` helper expects a configured published server at
`../Unreal/PackagedVR/Server` and avoids starting a second server on occupied UDP ports.

`Build-SharePackage.ps1` creates a versioned installer ZIP from the current APK and
the scripts in `Sharing`. Each recipient supplies retail DAT files and Android
platform-tools. It preserves earlier archives and refuses to overwrite a release.

`Test-Quest.ps1` runs headset startup checks with the app visible. Installer tests
live in `Tests`; native client automation lives in `../Unreal/Plugins/ACEClient/Source`.
Successful builds and automation do not replace an in-headset gameplay check.

Landscape textures retain the retail 1024-pixel TexMerge output, uncompressed
BGRA8 pixels and full mipmaps. `ace.Texture.MaxWorldSize` applies to object
textures, not landscape, UI or sky. The Android ASTC package label does not change
this runtime DAT texture path. Terrain blends use retail's byte-color arithmetic;
older cached blends are automatically rebuilt. `ace.RenderAudit` reports unique
landscape texture sizes, formats and estimated GPU allocation.

For a live, stationary performance comparison, keep the headset on and the game
visible with menus closed, then run from this directory:

```powershell
.\Tools\Measure-QuestRenderComparison.ps1 -Variable ShowFlag.PointLights -TestValue 0
```

This records baseline, changed, and restored intervals for a live console variable.
It rejects sleep/loading/menu interruptions and checkpoints the original setting
and restoration status in `Performance/Comparison-*/comparison.json`. Check that
file if the host exits unexpectedly. Do not use it for settings that require a
restart or actor recreation, such as GPU timestamps or actor draw caching. Keep
screenshots, rendering audits, builds, and other heavy work outside sample windows.
`Tools/Summarize-VrApi.py` summarizes the saved log with the report's process ID
and time interval; use `--min-samples` to reject incomplete captures. Runtime App
durations are not GPU-only timings or frame-time percentiles.
