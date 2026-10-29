#requires -Version 7.0
param(
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [string]$JavaRoot = 'C:/Program Files/Android/openjdk/jdk-21.0.8',
    [string]$AndroidSdkRoot = (Join-Path $PSScriptRoot '../Quest/AndroidSDK')
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot '../Unreal/Build/VR/Test-ReleaseVersion.ps1')
$shared = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../Unreal/ACUnreal.uproject'))
$logs = Join-Path $PSScriptRoot 'Logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$prepare = Join-Path $logs 'Prepare.log'
& (Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat') ACUnrealEditor Win64 Development $shared -WaitMutex -NoHotReloadFromIDE *> $prepare
if ($LASTEXITCODE -ne 0) { throw "Editor build failed: $prepare" }
& (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') $shared -run=ACEPrepareRuntimeMaterials -unattended -nosplash -nosound -stdout *>> $prepare
if ($LASTEXITCODE -ne 0) { throw "Runtime material generation failed: $prepare" }
& (Join-Path $PSScriptRoot 'Sync-ClientSource.ps1')
$env:ANDROID_HOME = [IO.Path]::GetFullPath($AndroidSdkRoot)
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
$env:NDKROOT = Join-Path $env:ANDROID_HOME 'ndk/27.2.12479018'
$env:NDK_ROOT = $env:NDKROOT
$env:JAVA_HOME = $JavaRoot
$project = Join-Path $PSScriptRoot 'Project/ACUnreal.uproject'
$output = Join-Path $PSScriptRoot 'Packaged'
$log = Join-Path $logs ('Build-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
& (Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat') BuildCookRun `
    "-project=$project" -noP4 -platform=Android -cookflavor=ASTC -clientconfig=Development `
    -build -cook -stage -pak -package -archive -unattended -utf8output -WaitForUATMutex '-ubtargs=-WaitMutex' `
    '-cmdline=-nohmd -UserDir=/data/user/0/com.acecommunity.acunreal.mobile/files/ACUnreal/' `
    "-archivedirectory=$output" *> $log
if ($LASTEXITCODE -ne 0) { throw "Android packaging failed: $log" }
# The game target disables implicit engine plugins. Without the selector Android
# falls back to a generic profile that disables Vulkan, even on capable phones.
$receipt = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Project/Binaries/Android/ACUnreal.target') -Raw | ConvertFrom-Json
if ($receipt.BuildPlugins -notcontains 'AndroidDeviceProfileSelector') {
    throw 'Android package is missing the GPU device-profile selector required for Vulkan startup.'
}
$apk = Join-Path $output 'Android_ASTC/ACUnreal-arm64.apk'
$aapt = Join-Path $env:ANDROID_HOME 'build-tools/35.0.1/aapt.exe'
$metadata = & $aapt dump badging $apk | Out-String
if ($LASTEXITCODE -ne 0 -or $metadata -notmatch "package: name='com.acecommunity.acunreal.mobile'" `
    -or $metadata -notmatch "application-label:'AC:Unreal Mobile'" -or $metadata -notmatch "native-code: 'arm64-v8a'") {
    throw 'Android package identity/architecture verification failed.'
}
$config = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Project/Config/DefaultEngine.ini') -Raw
if ($config -notmatch '(?m)^StoreVersion=(\d+)\s*$') { throw 'Missing mobile StoreVersion.' }
$code = [int]$Matches[1]
if ($config -notmatch '(?m)^VersionDisplayName=([^\r\n]+)') { throw 'Missing mobile VersionDisplayName.' }
$version = $Matches[1].Trim()
if ($metadata -notmatch ("versionCode='"+$code+"' versionName='"+[regex]::Escape($version)+"'")) {
    throw 'Packaged Android version differs from the shared release.'
}
$manifest = & $aapt dump xmltree $apk AndroidManifest.xml | Out-String
if ($LASTEXITCODE -ne 0 -or $manifest -match 'com.oculus.intent.category.VR|android.hardware.vr.headtracking|org.khronos.openxr.intent.category.IMMERSIVE_HMD') {
    throw 'Phone package contains a headset-only manifest requirement.'
}
Get-FileHash -LiteralPath $apk -Algorithm SHA256
[ordered]@{package='com.acecommunity.acunreal.mobile';versionCode=$code;version=$version;bytes=(Get-Item $apk).Length;sha256=(Get-FileHash $apk -Algorithm SHA256).Hash} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'android-manifest.json') -Encoding utf8
Write-Host "Verified Android phone/tablet package: $apk"
