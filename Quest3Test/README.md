# Native Quest client

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

The APK is written to `Packaged/Android_ASTC/ACEViewer-arm64.apk`. The application ID
is `com.acecommunity.questtest`, displayed as **ACE Quest Test**. The build checks
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

Keep the headset awake when launching. Open **Apps > Unknown Sources > ACE Quest
Test**. Enter an ACE server address reachable from the headset; the server runs on
the PC, not inside the Quest. Configure your own local server credentials separately.
The guarded `Start-Server.ps1` helper expects a configured published server at
`../Unreal/PackagedVR/Server` and avoids starting a second server on occupied UDP ports.

`Build-SharePackage.ps1` creates a versioned installer ZIP from the current APK and
the scripts in `Sharing`. Each recipient supplies retail DAT files and Android
platform-tools. It preserves earlier archives and refuses to overwrite a release.

`Test-Quest.ps1` runs headset startup checks with the app visible. Installer tests
live in `Tests`; native client automation lives in `../Unreal/Plugins/ACEClient/Source`.
Successful builds and automation do not replace an in-headset gameplay check.
