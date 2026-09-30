#requires -Version 7.0
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$ToolchainRoot = $env:LINUX_MULTIARCH_ROOT
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'Test-ReleaseVersion.ps1')
$sdk = Get-Content -LiteralPath (Join-Path $EngineRoot 'Engine\Config\Linux\Linux_SDK.json') -Raw | ConvertFrom-Json
if (!$ToolchainRoot) { $ToolchainRoot = Join-Path 'C:\UnrealToolchains' $sdk.MainVersion }
$versionFile = Join-Path $ToolchainRoot 'ToolchainVersion.txt'
if (!(Test-Path -LiteralPath $versionFile) -or (Get-Content -LiteralPath $versionFile -Raw).Trim() -ne $sdk.MainVersion) {
    throw "Install Epic's $($sdk.MainVersion) Linux cross-compile SDK, then set LINUX_MULTIARCH_ROOT or pass -ToolchainRoot. See https://dev.epicgames.com/documentation/en-us/unreal-engine/linux-development-requirements-for-unreal-engine"
}
if (!(Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine\Binaries\Linux\UnrealGame.target'))) {
    throw 'Install the Linux engine platform in Epic Games Launcher > Unreal Engine > Library > UE 5.8 > Options.'
}
$env:LINUX_MULTIARCH_ROOT = $ToolchainRoot
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$project = Join-Path $projectRoot 'ACUnreal.uproject'
$archive = Join-Path $projectRoot 'Saved\LinuxArchive'
$package = Join-Path $archive 'Linux'
$log = Join-Path $projectRoot ('Saved\LinuxBuild-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
& (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun `
    "-project=$project" -noP4 -platform=Linux -architecture=x64 -clientconfig=Development `
    -build -cook -stage -pak -package -archive "-archivedirectory=$package" -WaitForUATMutex '-ubtargs=-WaitMutex' -unattended -utf8output *> $log
$code = $LASTEXITCODE
Get-Content -LiteralPath $log -Tail 25
if ($code -ne 0) { throw "Linux packaging failed (exit $code). See $log" }
$binary = Join-Path $package 'ACUnreal\Binaries\Linux\ACUnreal'
if (!(Test-Path -LiteralPath $binary)) { throw "Linux package is missing its native executable: $binary" }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'AC-Unreal.sh'), (Join-Path $PSScriptRoot 'README-LINUX.txt') -Destination $package -Force
Write-Host "AC:Unreal native Linux x86_64 build ready in $package. Build log: $log"
