#requires -Version 7.0
# Read-only preflight shared by both platform builds.
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$header = Get-Content -LiteralPath (Join-Path $repoRoot 'Unreal/Plugins/ACEClient/Source/ACEClient/Public/ACEClientBuild.h') -Raw
$versions = [regex]::Matches($header, 'inline constexpr const TCHAR\* Version = TEXT\("([^"]+)"\);')
if ($versions.Count -ne 1) { throw 'Declare one shared client Version for Windows and Quest.' }
$version = $versions[0].Groups[1].Value
if ($header -notmatch 'ReleaseNumber = (\d+);') { throw 'Missing client ReleaseNumber.' }
$release = [int]$Matches[1]
if ($version -notmatch '^\d{4}\.\d{2}\.\d{2}\.(\d+)$' -or [int]$Matches[1] -ne $release) {
    throw 'Client Version must be YYYY.MM.DD.ReleaseNumber on every platform.'
}
foreach ($relative in @('Unreal/Config/DefaultGame.ini','Quest/Project/Config/DefaultGame.ini')) {
    $config = Get-Content -LiteralPath (Join-Path $repoRoot $relative) -Raw
    if ($config -notmatch '(?m)^ProjectVersion=([^\r\n]+)' -or $Matches[1].Trim() -ne $version) {
        throw "ProjectVersion in $relative differs from shared Version $version."
    }
}
$android = Get-Content -LiteralPath (Join-Path $repoRoot 'Quest/Project/Config/DefaultEngine.ini') -Raw
if ($android -notmatch '(?m)^VersionDisplayName=([^\r\n]+)' -or $Matches[1].Trim() -ne $version) {
    throw "Quest VersionDisplayName differs from shared Version $version."
}
if ($android -notmatch '(?m)^StoreVersion=(\d+)\s*$' -or [int]$Matches[1] -ne $release) {
    throw "Quest StoreVersion differs from shared ReleaseNumber $release."
}
Write-Host "Verified unified Windows/PC VR/Quest version $version (release $release)."
