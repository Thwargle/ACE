#requires -Version 7.0
[CmdletBinding(SupportsShouldProcess)]
param()
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../Unreal/Saved/VRServer'))
$targetRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../Unreal/PackagedVR/Server'))
$targetExe = Join-Path $targetRoot 'ACE.Server.exe'
foreach ($name in 'ACE.Server.exe', 'ACE.Server.dll', 'ACE.Server.deps.json', 'ACE.Server.runtimeconfig.json') {
    if (!(Test-Path -LiteralPath (Join-Path $sourceRoot $name) -PathType Leaf)) {
        throw "The staged server is incomplete: $name is missing from $sourceRoot. Publish the server before updating."
    }
}
$running = @(Get-CimInstance Win32_Process -Filter "name='ACE.Server.exe'" | Where-Object ExecutablePath -eq $targetExe)
if ($running.Count -and !$WhatIfPreference) {
    throw 'This server is still running. Enter exit in its ACE console and wait for it to finish saving, then rerun Update-Server.ps1. No live files were changed.'
}
# Only published binaries are replaced. Config.js, log4net.config, plugins,
# databases, DAT files, logs and local configuration stay in the live directory.
$changes = @(foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -File -Recurse) {
    if ($file.Extension -notin '.dll', '.exe', '.pdb' -and
        $file.Name -notin 'ACE.Server.deps.json', 'ACE.Server.runtimeconfig.json') { continue }
    $relative = [IO.Path]::GetRelativePath($sourceRoot, $file.FullName)
    $destination = [IO.Path]::GetFullPath((Join-Path $targetRoot $relative))
    if (!$destination.StartsWith($targetRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'A published file resolved outside the server directory.'
    }
    $newHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    $oldHash = if (Test-Path -LiteralPath $destination) { (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash } else { '' }
    if ($newHash -ne $oldHash) {
        [pscustomobject]@{ RelativePath = $relative; Source = $file.FullName; Destination = $destination; OldSHA256 = $oldHash; NewSHA256 = $newHash }
    }
})
if (!$PSCmdlet.ShouldProcess($targetRoot, "Back up and install $($changes.Count) changed server binaries, then start ACE")) { return }
$backup = Join-Path $PSScriptRoot ('../Unreal/Saved/ServerBackups/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
[void](New-Item -ItemType Directory -Path $backup -Force)
foreach ($change in $changes) {
    if (!$change.OldSHA256) { continue }
    $backupFile = Join-Path $backup $change.RelativePath
    [void](New-Item -ItemType Directory -Path (Split-Path $backupFile) -Force)
    Copy-Item -LiteralPath $change.Destination -Destination $backupFile
}
$changes | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $backup 'manifest.json')
foreach ($change in $changes) {
    [void](New-Item -ItemType Directory -Path (Split-Path $change.Destination) -Force)
    Copy-Item -LiteralPath $change.Source -Destination $change.Destination -Force
    if ((Get-FileHash -LiteralPath $change.Destination -Algorithm SHA256).Hash -ne $change.NewSHA256) {
        throw "Verification failed for $($change.RelativePath). ACE was not started. Previous binaries are in $backup."
    }
}
Write-Host "Server binaries verified. Backup: $([IO.Path]::GetFullPath($backup))"
& (Join-Path $PSScriptRoot 'Start-Server.ps1')
