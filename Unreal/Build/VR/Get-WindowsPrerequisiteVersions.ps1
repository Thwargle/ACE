#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$VisualCppRedist,
    [Parameter(Mandatory)][string]$GameInputMsi
)
$ErrorActionPreference = 'Stop'
$info = (Get-Item -LiteralPath $VisualCppRedist).VersionInfo
$vcVersion = [version]::new($info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart)
if ($vcVersion.Major -lt 14) { throw 'The Windows installer requires a v14 or newer Visual C++ x64 redistributable.' }

# Read the DLL version, not ProductVersion: GameInput's MSI and DLL versions differ.
# This also accepts newer packages whose MSI upgrade/product IDs have changed.
$installer = $database = $view = $record = $null
try {
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $database = $installer.OpenDatabase((Resolve-Path -LiteralPath $GameInputMsi).Path, 0)
    $view = $database.OpenView('SELECT `FileName`, `Version` FROM `File`')
    [void]$view.Execute()
    $versions = @()
    while ($record = $view.Fetch()) {
        if (($record.StringData(1) -split '\|')[-1] -eq 'GameInputRedist.dll') {
            $versions += [version]$record.StringData(2)
        }
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($record)
        $record = $null
    }
    $versions = @($versions | Sort-Object -Unique)
    if ($versions.Count -ne 1 -or $versions[0].Major -lt 1 -or $versions[0].Revision -lt 0) {
        throw 'Cannot determine an unambiguous four-part GameInput runtime version from the MSI.'
    }
    [pscustomobject]@{ VisualCppRuntimeVersion = $vcVersion.ToString(); GameInputRuntimeVersion = $versions[0].ToString() }
}
finally {
    if ($view) { [void]$view.Close() }
    foreach ($com in @($record, $view, $database, $installer)) {
        if ($null -ne $com) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($com) }
    }
}
