#requires -Version 7.0
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$JavaRoot = 'C:\Program Files\Android\openjdk\jdk-21.0.8',
    [string]$AndroidSdkRoot = (Join-Path $PSScriptRoot 'AndroidSDK')
)
$ErrorActionPreference = 'Stop'
$versionConfig = Get-Content (Join-Path $PSScriptRoot 'Project/Config/DefaultEngine.ini') -Raw
if ($versionConfig -notmatch '(?m)^VersionDisplayName=([^\r\n]+)') { throw 'Missing Quest VersionDisplayName.' }
$displayVersion = $Matches[1].Trim()
$buildHeader = Get-Content (Join-Path $PSScriptRoot '../Unreal/Plugins/ACEClient/Source/ACEClient/Public/ACEClientBuild.h') -Raw
if ($buildHeader -notmatch ('(?s)#if PLATFORM_ANDROID\s+inline constexpr const TCHAR\* Version = TEXT\("' + [regex]::Escape($displayVersion) + '"\);')) {
    throw 'Quest package version and the login-screen version differ. Update ACEClientBuild.h before packaging.'
}
# Quest disables the editor-only ACEWorldBake plugin. Generate shader parents
# with the freshly built shared editor BEFORE copying them into the Quest project.
# Otherwise new C++ material names ship with yesterday's assets (green terrain).
$sharedProject = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../Unreal/ACUnreal.uproject'))
$prepareLog = Join-Path $PSScriptRoot 'Logs/Prepare-RuntimeMaterials.log'
New-Item -ItemType Directory -Force -Path (Split-Path $prepareLog) | Out-Null
& (Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat') ACUnrealEditor Win64 Development $sharedProject -WaitMutex -NoHotReloadFromIDE *> $prepareLog
if ($LASTEXITCODE -ne 0) { throw "Shared editor build failed. See $prepareLog" }
& (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') $sharedProject -run=ACEPrepareRuntimeMaterials -unattended -nosplash -nosound -stdout '-FullStdOutLogOutput' *>> $prepareLog
if ($LASTEXITCODE -ne 0) { throw "Runtime material generation failed. See $prepareLog" }
& (Join-Path $PSScriptRoot 'Sync-ClientSource.ps1')
$env:ANDROID_HOME = [IO.Path]::GetFullPath($AndroidSdkRoot)
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
$env:NDKROOT = Join-Path $env:ANDROID_HOME 'ndk\27.2.12479018'
$env:NDK_ROOT = $env:NDKROOT
$env:JAVA_HOME = $JavaRoot
$project = Join-Path $PSScriptRoot 'Project\ACUnreal.uproject'
$output = Join-Path $PSScriptRoot 'Packaged'
$log = Join-Path $PSScriptRoot ('Logs\Build-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
New-Item -ItemType Directory -Force -Path (Split-Path $log), $output | Out-Null
Write-Host "AC:VR Quest packaging log: $log"
& (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun `
    "-project=$project" -noP4 -platform=Android -cookflavor=ASTC `
    -clientconfig=Development -build -cook -stage -pak -package -archive `
    '-cmdline=-UserDir=/data/user/0/com.acecommunity.questtest/files/ACUnreal/' -WaitForUATMutex '-ubtargs=-WaitMutex' `
    "-archivedirectory=$output" -unattended -utf8output *> $log
$code = $LASTEXITCODE
Get-Content -LiteralPath $log -Tail 35
if ($code -ne 0) { throw "Quest packaging failed (exit $code). See $log" }
# Verify every shared generated material/collection made it into the cook.
# This is independent of stale assets already present in the Quest checkout.
$materialRoot = Join-Path $PSScriptRoot '../Unreal/Content/ACE/RuntimeMaterials'
$cookedRoot = Join-Path $PSScriptRoot 'Project/Saved/Cooked/Android_ASTC/ACUnreal/Content/ACE/RuntimeMaterials'
foreach ($asset in Get-ChildItem -LiteralPath $materialRoot -Filter '*.uasset') {
    if (!(Test-Path -LiteralPath (Join-Path $cookedRoot $asset.Name))) {
        throw "Quest cook omitted runtime material dependency $($asset.Name). Do not install this package."
    }
}
# Android platform INIs can override the intended release version silently.
# Verify the exported APK against the one authoritative version declaration.
$versionConfig = Get-Content (Join-Path $PSScriptRoot 'Project/Config/DefaultEngine.ini') -Raw
if ($versionConfig -notmatch '(?m)^StoreVersion=(\d+)\s*$') { throw 'Missing StoreVersion in DefaultEngine.ini.' }
$expectedCode = $Matches[1]
if ($versionConfig -notmatch '(?m)^VersionDisplayName=([^\r\n]+)') { throw 'Missing VersionDisplayName in DefaultEngine.ini.' }
$expectedName = $Matches[1].Trim()
$apk = Join-Path $output 'Android_ASTC/ACUnreal-arm64.apk'
$metadata = & (Join-Path $env:ANDROID_HOME 'build-tools/35.0.1/aapt.exe') dump badging $apk | Out-String
if ($LASTEXITCODE -ne 0 -or $metadata -notmatch ("package: name='com\.acecommunity\.questtest' versionCode='" + [regex]::Escape($expectedCode) + "' versionName='" + [regex]::Escape($expectedName) + "'")) {
    throw 'Packaged Quest version differs from DefaultEngine.ini. Check Android configuration overrides before installing.'
}
Write-Host "Verified Quest package version $expectedName (code $expectedCode)."
if ($metadata -notmatch "(?m)^application-label:'AC:VR'\s*$") {
    throw 'Packaged Quest app label is not AC:VR. Check Android configuration overrides.'
}
Write-Host "AC:VR packaged to $output"
