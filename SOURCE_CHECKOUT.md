# Building the VR source checkout

The server changes, Windows/PC VR client, and native Quest client are kept here.
`Unreal` is the canonical client source. The Quest project keeps only its platform
configuration in Git; `Quest/Sync-ClientSource.ps1` recreates its shared source,
plugin descriptors, runtime UI layouts, test fixtures, and runtime materials.
Edit shared client code in `Unreal`, not the generated Quest copy.

## Prerequisites

- .NET 10 SDK for the server and tools.
- Unreal Engine 5.8 with a supported Visual Studio C++ toolchain and Windows SDK.
- PowerShell 7 for local build scripts.
- For Quest: Unreal's Android platform component, JDK 21, Android platform-tools,
  SDK platform 35, build-tools 35.0.1, and NDK 27.2.12479018. The scripts default to
  a local, ignored `Quest/AndroidSDK`; `Build-Quest.ps1` also accepts
  `-AndroidSdkRoot`, `-JavaRoot`, and `-EngineRoot` for externally installed tools.
- Your own retail DAT files. The default Windows location is
  `C:\Turbine\Asheron's Call`; server/client configuration can override it.
  SDKs, DAT files, engine binaries, account data, and database contents are not included.

## Server

From the repository root:

```powershell
dotnet build .\Source\ACE.sln -c Release -p:Platform=x64
dotnet test .\Source\ACE.Server.Tests\ACE.Server.Tests.csproj -c Release -p:Platform=x64
dotnet publish .\Source\ACE.Server\ACE.Server.csproj -c Release -p:Platform=x64 -o .\Unreal\PackagedVR\Server
```

Create local `Config.js` and `log4net.config` from the examples in `Source/ACE.Server`;
configure your own databases, passwords, and DAT directory. Put the local config
beside the published server before using `Quest/Start-Server.ps1`. Follow the
original [ACE README](README.md) for database setup. Local configuration stays ignored.

## Windows / PC VR

`Unreal/Launch.bat` builds and opens the editor. `Unreal/Launch-VR.ps1` launches PC VR
through OpenXR. To package:

```powershell
pwsh -File .\Unreal\Build\VR\Build-Windows.ps1
```

Output goes to `Unreal/Saved/VRWindowsArchive/Windows`. The editor-only MCP and
Terminal plugins are disabled in this source checkout; they are optional local
tools and are not needed to build or run the game. Android File Server is disabled,
and the development machine's file-server token is omitted.

## Quest

Build Windows first when changing runtime material factories, then:

```powershell
pwsh -File .\Quest\Build-Quest.ps1
```

The script synchronizes the generated Quest tree before building. The APK is
`Quest/Packaged/Android_ASTC/ACUnreal-arm64.apk`. Build logs go to `Quest/Logs`.
For installation and sharing, see [Quest instructions](Quest/README.md).
No build or installer archive is checked in.

After packaging and validation, `Quest/Build-SharePackage.ps1` creates the
Quest installer. `Unreal/Build/VR/Build-Release.py` combines that installer with the
Windows package into a versioned `Releases` folder and verifies ZIP contents and
checksums. Those output folders stay ignored.

## What belongs in Git

Commit source, tests/fixtures, project and default configuration, build/installer
scripts, documentation, the small runtime material assets, and resolved UI JSON.
Those JSON layouts are used at runtime; excluding them breaks the menus.

Do not commit generated DLLs/APKs/packages, Saved/Intermediate/Binaries folders,
SDKs, caches, DAT imports/bakes, local settings, credentials, database dumps, profiling
captures, or duplicate Quest source. Existing upstream `Source/lib` dependencies
remain as provided by ACE. The project-local ProceduralMeshComponent override is
required by both clients; its changes are documented in
`Unreal/Plugins/ProceduralMeshComponent/ACE-CHANGES.md`.

Generated test output belongs in `Saved`, `Logs`, `TestOutput`, `TestArtifacts`,
or `AutomationReports`; profiling captures and Unreal trace files are ignored
as well. Keep `Quest/Tests`, server test projects, client automation source, and
the checked-in test fixtures in Git. They are build inputs, not disposable output.

This export preserves newer upstream server changes already present in this ACE
repository. VR edits were compared against their original base and merged where
both versions changed the same file, instead of reverting upstream fixes or package
updates. `C:\dev\ACE` is now the canonical working directory for the server,
Windows client, and Quest client. Local tools, installed builds, and releases
also live here in Git-ignored directories. Historical source variants and
migration records are kept under the ignored `LocalArchive` directory.

The optional Docker Hub publishing workflow is manual. Normal Git pushes do not
require Docker Hub credentials. Windows and Quest releases use the build scripts
above and do not use Docker Hub.
