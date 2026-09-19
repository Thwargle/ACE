param([switch]$BuildOnly)
$ErrorActionPreference = 'Stop'
$engine = 'C:/Program Files/Epic Games/UE_5.8/Engine'
$project = Join-Path $PSScriptRoot 'ACUnreal.uproject'
if (!(Test-Path -LiteralPath "$engine/Binaries/Win64/UnrealEditor.exe")) { throw 'Unreal Engine 5.8 was not found. Update the engine path in Launch-VR.ps1.' }
$runtime = (Get-ItemProperty 'HKLM:/SOFTWARE/Khronos/OpenXR/1' -ErrorAction SilentlyContinue).ActiveRuntime
Write-Host "OpenXR runtime: $runtime"
if (!$BuildOnly -and (!$runtime -or $runtime -notmatch 'steam')) {
    throw 'In SteamVR Settings > OpenXR, set SteamVR as the current OpenXR runtime, then launch again. Start the Meta PC software and connect the Rift first.'
}
& "$engine/Build/BatchFiles/Build.bat" ACUnrealEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE -gather
if ($LASTEXITCODE -ne 0) { throw 'VR build failed; the client was not started.' }
if (!$BuildOnly) {
    # Start-Process stays hidden; Unreal creates its own normal game window and headset presentation.
    Start-Process -FilePath "$engine/Binaries/Win64/UnrealEditor.exe" -ArgumentList @("`"$project`"", '-game', '-vr') -WindowStyle Hidden
}
