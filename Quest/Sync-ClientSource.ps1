#requires -Version 7.0
param([switch]$CheckOnly, [string]$TargetRoot = (Join-Path $PSScriptRoot 'Project'))
$ErrorActionPreference = 'Stop'
$shared = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\Unreal'))
$target = [IO.Path]::GetFullPath($TargetRoot)
$differences = @()
# Windows' cook regenerates runtime material assets. Quest deliberately disables
# ACEWorldBake, so it must consume those generated assets as well as shared code.
foreach ($subdirectory in @('Source', 'Plugins\ACEClient\Source', 'Plugins\ACEClient\Docs\UI\Resolved', 'Plugins\ACEClient\Tests\Fixtures', 'Plugins\ProceduralMeshComponent\Source', 'Content\ACE\RuntimeMaterials', 'Build\Android\res')) {
    foreach ($source in Get-ChildItem (Join-Path $shared $subdirectory) -File -Recurse) {
        $relative = [IO.Path]::GetRelativePath($shared, $source.FullName)
        $destination = Join-Path $target $relative
        $different = !(Test-Path -LiteralPath $destination)
        if (!$different) { $different = (Get-FileHash $source.FullName).Hash -ne (Get-FileHash $destination).Hash }
        if (!$different) { continue }
        $differences += $relative
        if (!$CheckOnly) {
            New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
            Copy-Item -LiteralPath $source.FullName -Destination $destination -Force
        }
    }
    # Fail on Quest-only source files instead of silently retaining stale code.
    if (!(Test-Path -LiteralPath (Join-Path $target $subdirectory))) { continue }
    foreach ($file in Get-ChildItem (Join-Path $target $subdirectory) -File -Recurse) {
        $relative = [IO.Path]::GetRelativePath($target, $file.FullName)
        if (!(Test-Path -LiteralPath (Join-Path $shared $relative))) {
            throw "Quest-only source must be reviewed and shared or removed: $relative"
        }
    }
}
foreach ($relative in @('Plugins\ACEClient\ACEClient.uplugin', 'Plugins\ProceduralMeshComponent\ProceduralMeshComponent.uplugin', 'Config\DefaultDeviceProfiles.ini', 'Config\DefaultInput.ini')) {
    $source = Join-Path $shared $relative
    $destination = Join-Path $target $relative
    if (!(Test-Path $destination) -or (Get-FileHash $source).Hash -ne (Get-FileHash $destination).Hash) {
        $differences += $relative
        if (!$CheckOnly) {
            New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
            Copy-Item -LiteralPath $source -Destination $destination -Force
        }
    }
}
if ($CheckOnly -and $differences.Count) { throw "Client source differs: $($differences -join ', ')" }
Write-Host "Shared Unreal/Quest source and runtime materials verified ($($differences.Count) files $(if ($CheckOnly) {'differ'} else {'updated'}))."
