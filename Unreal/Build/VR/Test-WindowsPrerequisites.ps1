#requires -Version 7.0
# Compiles the production Pascal detection code with mock inputs, then runs a
# read-only probe against this PC. Never installs/uninstalls Microsoft runtimes.
param(
    [string]$Compiler = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8'
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$output = Join-Path $repoRoot ('Unreal/Saved/InstallerPrerequisiteTests/' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$versions = & (Join-Path $PSScriptRoot 'Get-WindowsPrerequisiteVersions.ps1') -VisualCppRedist (Join-Path $EngineRoot 'Engine/Extras/Redist/en-us/vc_redist.x64.exe') -GameInputMsi (Join-Path $EngineRoot 'Engine/Extras/Redist/en-us/GameInputRedist.msi')
$versions | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'bundled-versions.json')
$vcNeeded = $true
foreach ($registryView in @([Microsoft.Win32.RegistryView]::Registry64, [Microsoft.Win32.RegistryView]::Registry32)) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $registryView)
    $key = $base.OpenSubKey('SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64')
    try {
        if ($key -and $key.GetValue('Installed') -eq 1) {
            # Independently compare the documented Version string instead of DWORDs.
            $installed = [version]($key.GetValue('Version').TrimStart('v'))
            if ($installed -ge [version]$versions.VisualCppRuntimeVersion) { $vcNeeded = $false }
        }
    }
    finally { if ($key) { $key.Dispose() }; $base.Dispose() }
}
$gameInput = Join-Path $env:SystemRoot 'System32/GameInputRedist.dll'
$gameInputNeeded = !(Test-Path -LiteralPath $gameInput)
if (!$gameInputNeeded) {
    $gameInputNeeded = [version](Get-Item -LiteralPath $gameInput).VersionInfo.FileVersion -lt [version]$versions.GameInputRuntimeVersion
}
foreach ($mode in @('fixtures', 'live')) {
    $argsList = @('/Q', "/DTestOutput=$output", "/DTestName=$mode", "/DVisualCppRuntimeVersion=$($versions.VisualCppRuntimeVersion)", "/DGameInputRuntimeVersion=$($versions.GameInputRuntimeVersion)")
    if ($mode -eq 'fixtures') { $argsList += '/DPrerequisiteTestHarness' }
    else { $argsList += @("/DExpectedVcNeeded=$vcNeeded", "/DExpectedGameInputNeeded=$gameInputNeeded") }
    & $Compiler @argsList (Join-Path $PSScriptRoot 'Tests/Windows-Prerequisites.Tests.iss') *> (Join-Path $output "$mode-compile.log")
    if ($LASTEXITCODE -ne 0) { throw "Test compilation failed; see $output/$mode-compile.log" }
    $result = Join-Path $output "$mode-result.txt"
    $runArgs = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', ('/LOG="' + (Join-Path $output "$mode.log") + '"'), ('/RESULTFILE="' + $result + '"'))
    $process = Start-Process -FilePath (Join-Path $output "$mode.exe") -ArgumentList $runArgs -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(60000)) { Stop-Process -Id $process.Id; throw 'Prerequisite detection tests timed out' }
    if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $result) -or (Get-Content -LiteralPath $result -Raw) -ne 'PASS') { throw "Prerequisite detection test failed; see $output/$mode.log" }
    Get-Content -LiteralPath (Join-Path $output "$mode.log") | Select-String 'PASS:' | ForEach-Object { $_.Line }
}
Write-Host "PASS: prerequisite detection fixtures and native read-only checks. Evidence: $output"
