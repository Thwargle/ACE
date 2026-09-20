#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$ReleaseDirectory,
    [string]$Compiler = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8'
)
$ErrorActionPreference = 'Stop'
$releaseRoot = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
$manifest = Get-Content -LiteralPath (Join-Path $releaseRoot 'release-manifest.json') -Raw | ConvertFrom-Json
$version = [int]$manifest.questVersionCode
$archive = Join-Path $releaseRoot "AC-Unreal-and-AC-VR-Windows-v$version.zip"
$expectedHash = (Get-Content -LiteralPath (Join-Path $releaseRoot 'SHA256SUMS.txt') | Where-Object { $_.EndsWith([IO.Path]::GetFileName($archive)) }).Split(' ')[0]
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Windows release checksum mismatch.' }
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$stage = Join-Path $repoRoot ("Unreal/Saved/WebInstaller/v$version-" + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Expand-Archive -LiteralPath $archive -DestinationPath $stage
$payload = Join-Path $stage "AC-Unreal-and-AC-VR-Windows-v$version"
$exe = Join-Path $payload 'ACUnreal/Binaries/Win64/ACUnreal.exe'
if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $manifest.windowsExeSha256) { throw 'Packaged game does not match release manifest.' }
$unexpected = Get-ChildItem -LiteralPath $payload -Recurse -File | Where-Object { $_.Extension -in '.dat','.log','.pdb','.dmp' -or [IO.Path]::GetRelativePath($payload,$_.FullName) -match '(^|[\\/])(Saved|Intermediate|Logs|Source)[\\/]' }
if ($unexpected) { throw 'Development or personal files found in installer payload.' }
$icon = Join-Path $repoRoot 'Unreal/Build/Windows/Application.ico'
$banner = Join-Path $repoRoot 'Unreal/Build/Branding/Installer-Banner.png'
$mark = Join-Path $repoRoot 'Unreal/Build/Branding/AC-Icon.png'
$prereq = Join-Path $EngineRoot 'Engine/Extras/Redist/en-us/vc_redist.x64.exe'
$gameInput = Join-Path $EngineRoot 'Engine/Extras/Redist/en-us/GameInputRedist.msi'
foreach ($required in @($Compiler,$icon,$banner,$mark,$prereq,$gameInput)) { if (!(Test-Path -LiteralPath $required)) { throw "Missing installer input: $required" } }
& $Compiler "/DPayloadDir=$payload" "/DBrandIcon=$icon" "/DBrandWizard=$banner" "/DBrandMark=$mark" "/DPrerequisites=$prereq" "/DGameInput=$gameInput" "/DReleaseDir=$releaseRoot" "/DReleaseNumber=$version" "/DProductVersion=$($manifest.windowsVersion)" (Join-Path $PSScriptRoot 'Windows-Installer.iss')
if ($LASTEXITCODE -ne 0) { throw 'Windows setup compiler failed.' }
$setup = Join-Path $releaseRoot "AC-Unreal-Setup-v$version.exe"
$checksum = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash
$sumPath = Join-Path $releaseRoot 'SHA256SUMS.txt'
$setupName = [IO.Path]::GetFileName($setup)
$sumLines = @(Get-Content -LiteralPath $sumPath | Where-Object { $_ -and !($_.EndsWith("  $setupName")) })
$sumLines += "$checksum  $setupName"
$sumLines | Set-Content -LiteralPath $sumPath -Encoding utf8
$manifest | Add-Member -NotePropertyName windowsInstaller -NotePropertyValue ([ordered]@{ file = [IO.Path]::GetFileName($setup); bytes = (Get-Item -LiteralPath $setup).Length; sha256 = $checksum; signed = $false }) -Force
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $releaseRoot 'release-manifest.json') -Encoding utf8
Write-Host "Branded Windows installer ready: $setup"
