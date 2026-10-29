#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$Serial,
    [Parameter(Mandatory)][string]$DatDirectory,
    [string]$Apk = (Join-Path $PSScriptRoot 'Packaged/Android_ASTC/ACUnreal-arm64.apk'),
    [string]$AdbPath = (Join-Path $PSScriptRoot '../Quest/AndroidSDK/platform-tools/adb.exe'),
    [string]$AaptPath = (Join-Path $PSScriptRoot '../Quest/AndroidSDK/build-tools/35.0.1/aapt.exe')
)
$ErrorActionPreference = 'Stop'
# Require an explicit device so an attached Quest/second phone cannot be replaced.
if ($Serial -notmatch '^[A-Za-z0-9._:-]+$') { throw 'Invalid ADB serial.' }
$package = 'com.acecommunity.acunreal.mobile'
$metadata = & $AaptPath dump badging ([IO.Path]::GetFullPath($Apk)) | Out-String
if ($LASTEXITCODE -ne 0 -or $metadata -notmatch "package: name='com.acecommunity.acunreal.mobile'") {
    throw 'Choose the AC:Unreal Mobile APK. This installer does not install Quest or other applications.'
}
$deviceArgs = @('-s', $Serial)
function Invoke-Adb {
    param([string[]]$Arguments)
    $result = & $AdbPath @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "ADB failed: $result" }
    $result
}
function Wait-QuestConnection {
    param([string]$RequestedSerial)
    Invoke-Adb -Arguments @('-s', $RequestedSerial, 'get-state') | Out-Null
}
foreach ($name in @('client_portal.dat','client_cell_1.dat','client_local_English.dat')) {
    $file = Join-Path $DatDirectory $name
    if (!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -eq 0) { throw "Missing DAT: $file" }
}
Invoke-Adb -Arguments ($deviceArgs + @('get-state')) | Out-Null
$api = (Invoke-Adb -Arguments ($deviceArgs + @('shell','getprop','ro.build.version.sdk')) | Out-String).Trim()
$abis = (Invoke-Adb -Arguments ($deviceArgs + @('shell','getprop','ro.product.cpu.abilist')) | Out-String).Trim()
if ([int]$api -lt 29 -or $abis -notmatch 'arm64-v8a') { throw 'This build requires Android 10 or newer with ARM64 application support.' }
Invoke-Adb -Arguments ($deviceArgs + @('install','-r',[IO.Path]::GetFullPath($Apk))) | Out-Host
$diagnostics = Join-Path $PSScriptRoot 'Logs/Install'
New-Item -ItemType Directory -Force -Path $diagnostics | Out-Null
# Shared transfer verifies size/SHA256 before replacing each DAT, retains settings,
# and uses $package above; it never touches the Quest application.
. (Join-Path $PSScriptRoot '../Quest/Sharing/Quest-DataTransfer.ps1')
Install-QuestData -DatDirectory $DatDirectory -DiagnosticDirectory $diagnostics
Invoke-Adb -Arguments ($deviceArgs + @('shell','monkey','-p',$package,'-c','android.intent.category.LAUNCHER','1')) | Out-Host
