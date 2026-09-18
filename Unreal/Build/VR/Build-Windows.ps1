#requires -Version 7.0
param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$project = Join-Path $projectRoot 'ACEViewer.uproject'
$archive = Join-Path $projectRoot 'Saved\VRWindowsArchive'
$log = Join-Path $projectRoot ('Saved\VRWindowsBuild-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
& (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun `
    "-project=$project" -noP4 -platform=Win64 -clientconfig=Development `
    -build -cook -stage -pak -package -archive "-archivedirectory=$archive" -WaitForUATMutex '-ubtargs=-WaitMutex' -unattended -utf8output *> $log
$code = $LASTEXITCODE
Get-Content -LiteralPath $log -Tail 25
if ($code -ne 0) { throw "Windows packaging failed (exit $code). See $log" }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Launch-VR.ps1'), (Join-Path $PSScriptRoot 'Launch-VR.bat') -Destination (Join-Path $archive 'Windows') -Force
Write-Host "Windows build ready in $archive\Windows. Build log: $log"
