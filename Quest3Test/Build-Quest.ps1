#requires -Version 7.0
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$JavaRoot = 'C:\Program Files\Android\openjdk\jdk-21.0.8',
    [string]$AndroidSdkRoot = (Join-Path $PSScriptRoot 'AndroidSDK')
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'Sync-ClientSource.ps1')
$env:ANDROID_HOME = [IO.Path]::GetFullPath($AndroidSdkRoot)
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
$env:NDKROOT = Join-Path $env:ANDROID_HOME 'ndk\27.2.12479018'
$env:NDK_ROOT = $env:NDKROOT
$env:JAVA_HOME = $JavaRoot
$project = Join-Path $PSScriptRoot 'Project\ACEViewer.uproject'
$output = Join-Path $PSScriptRoot 'Packaged'
$log = Join-Path $PSScriptRoot ('Logs\Build-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
New-Item -ItemType Directory -Force -Path (Split-Path $log), $output | Out-Null
Write-Host "Quest test packaging log: $log"
& (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun `
    "-project=$project" -noP4 -platform=Android -cookflavor=ASTC `
    -clientconfig=Development -build -cook -stage -pak -package -archive `
    '-cmdline=-UserDir=/data/user/0/com.acecommunity.questtest/files/ACEViewer/' -WaitForUATMutex '-ubtargs=-WaitMutex' `
    "-archivedirectory=$output" -unattended -utf8output *> $log
$code = $LASTEXITCODE
Get-Content -LiteralPath $log -Tail 35
if ($code -ne 0) { throw "Quest packaging failed (exit $code). See $log" }
# Android platform INIs can override the intended release version silently.
# Verify the exported APK against the one authoritative version declaration.
$versionConfig = Get-Content (Join-Path $PSScriptRoot 'Project/Config/DefaultEngine.ini') -Raw
if ($versionConfig -notmatch '(?m)^StoreVersion=(\d+)\s*$') { throw 'Missing StoreVersion in DefaultEngine.ini.' }
$expectedCode = $Matches[1]
if ($versionConfig -notmatch '(?m)^VersionDisplayName=([^\r\n]+)') { throw 'Missing VersionDisplayName in DefaultEngine.ini.' }
$expectedName = $Matches[1].Trim()
$apk = Join-Path $output 'Android_ASTC/ACEViewer-arm64.apk'
$metadata = & (Join-Path $env:ANDROID_HOME 'build-tools/35.0.1/aapt.exe') dump badging $apk | Out-String
if ($LASTEXITCODE -ne 0 -or $metadata -notmatch ("package: name='com\.acecommunity\.questtest' versionCode='" + [regex]::Escape($expectedCode) + "' versionName='" + [regex]::Escape($expectedName) + "'")) {
    throw 'Packaged Quest version differs from DefaultEngine.ini. Check Android configuration overrides before installing.'
}
Write-Host "Verified Quest package version $expectedName (code $expectedCode)."
Write-Host "Quest test packaged to $output"
