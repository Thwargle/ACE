# Package launcher template. Copy next to the packaged ACUnreal.exe.
$ErrorActionPreference = 'Stop'
$executable = Join-Path $PSScriptRoot 'ACUnreal.exe'
if (!(Test-Path -LiteralPath $executable)) { throw 'Keep this launcher beside ACUnreal.exe in the complete Windows package.' }
$gameExecutable = Join-Path $PSScriptRoot 'ACUnreal/Binaries/Win64/ACUnreal.exe'
if (!(Test-Path -LiteralPath $gameExecutable)) { throw 'The Windows package is incomplete. Keep its ACUnreal and Engine subfolders together.' }
if (Get-Process ACUnreal -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $gameExecutable }) {
    throw 'This VR client is already running. Close that game window before launching another instance.'
}
$runtime = (Get-ItemProperty 'HKLM:/SOFTWARE/Khronos/OpenXR/1' -ErrorAction SilentlyContinue).ActiveRuntime
if (!$runtime -or $runtime -notmatch 'steam') {
    throw 'Start the Meta PC software and SteamVR. In SteamVR settings, select SteamVR as the current OpenXR runtime, then run this launcher again.'
}
Write-Host 'SteamVR must show the headset as connected before the game starts. Open the headset PC/Link connection first if needed.'
$logDirectory = Join-Path $env:LOCALAPPDATA 'ACUnreal/Saved/Logs'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$logPath = Join-Path $logDirectory ('VR-launch-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.log')
$gameProcess = Start-Process -FilePath $gameExecutable -ArgumentList @('-vr', "-abslog=`"$logPath`"") -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru
$deadline = (Get-Date).AddSeconds(30)
while ((Get-Date) -lt $deadline) {
    $startupLog = if (Test-Path -LiteralPath $logPath) { Get-Content -LiteralPath $logPath -Raw -ErrorAction SilentlyContinue } else { '' }
    if ($startupLog -match 'ACE VR: OpenXR rig active') { Write-Host "VR rig active. Startup log: $logPath"; exit 0 }
    if ($startupLog -match 'Instance is not viable|Failed to initialize OpenXR|Failed to initialize core functions') {
        # Only close the client started by this invocation; an unavailable headset cannot recover by logging into ACE.
        if (!$gameProcess.HasExited) { $null = $gameProcess.CloseMainWindow() }
        throw "SteamVR could not provide a headset when the game started. Wait until the headset is connected in SteamVR, close any remaining 2D client window, and launch again. Log: $logPath"
    }
    if ($gameProcess.HasExited) { throw "The client exited before VR initialized. Log: $logPath" }
    Start-Sleep -Milliseconds 250
}
throw "VR initialization was not confirmed. Check that SteamVR detects the headset, then close this client before retrying. Log: $logPath"
