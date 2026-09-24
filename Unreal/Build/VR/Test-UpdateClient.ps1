#requires -Version 5.1
# Uses tiny fixture executables; never runs an actual installer or touches a real game install.
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('ACUpdateTest-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$gameRoot = Join-Path $testRoot 'Game with spaces & punctuation'
$gameBin = Join-Path $gameRoot 'ACUnreal\Binaries\Win64'
New-Item -ItemType Directory -Path $gameBin -Force | Out-Null
$compiler = Join-Path $env:SystemRoot 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$fakeSource = Join-Path $testRoot 'Fixture.cs'
@'
using System;
using System.IO;
class Fixture {
    static int Main(string[] args) {
        foreach (string arg in args) {
            if (arg.StartsWith("/DIR=")) {
                string path = arg.Substring(5);
                File.WriteAllLines(Path.Combine(path,"installer-args.txt"),args);
                return 0;
            }
        }
        File.WriteAllLines(Path.Combine(Environment.CurrentDirectory,"game-args.txt"),args);
        return 0;
    }
}
'@ | Set-Content -LiteralPath $fakeSource
$fixture = Join-Path $testRoot 'Fixture.exe'
& $compiler /nologo /target:winexe "/out:$fixture" $fakeSource
if ($LASTEXITCODE -ne 0) { throw 'Fixture compilation failed.' }
$helper = Join-Path $testRoot 'Update-Client.ps1'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Update-Client.ps1') -Destination $helper
Copy-Item -LiteralPath $fixture -Destination (Join-Path $gameRoot 'ACUnreal.exe')
Copy-Item -LiteralPath $fixture -Destination (Join-Path $gameBin 'ACUnreal.exe')
$hash = (Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash
$powerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
foreach ($mode in @('Desktop','VR')) {
    $argsFile = Join-Path $gameRoot 'game-args.txt'
    if (Test-Path -LiteralPath $argsFile) { Remove-Item -LiteralPath $argsFile }
    # Already exited PID exercises the normal race when the game closes before the helper starts.
    & $powerShell -NoProfile -ExecutionPolicy Bypass -File $helper -Installer $fixture -ExpectedSha256 $hash -InstallDirectory $gameRoot -GameProcessId 2147483647 -Mode $mode -NoDialogs
    if ($LASTEXITCODE -ne 0) { throw 'Update helper failed.' }
    $deadline = (Get-Date).AddSeconds(10)
    while (!(Test-Path -LiteralPath $argsFile) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
    $expected = if ($mode -eq 'VR') { '-vr' } else { '-nohmd' }
    if ((Get-Content -LiteralPath $argsFile -Raw).Trim() -ne $expected) { throw 'Wrong restart mode.' }
    $installerArgs = Get-Content -LiteralPath (Join-Path $gameRoot 'installer-args.txt')
    if ($installerArgs -notcontains '/ACPORTABLE=1' -or $installerArgs -notcontains "/DIR=$gameRoot") { throw 'Portable install path/arguments were not preserved.' }
}
$before = (Get-Item -LiteralPath (Join-Path $gameRoot 'installer-args.txt')).LastWriteTimeUtc
& $powerShell -NoProfile -ExecutionPolicy Bypass -File $helper -Installer $fixture -ExpectedSha256 ('0' * 64) -InstallDirectory $gameRoot -GameProcessId 2147483647 -NoDialogs
if ($LASTEXITCODE -ne 1) { throw 'Invalid payload was accepted.' }
if ((Get-Item -LiteralPath (Join-Path $gameRoot 'installer-args.txt')).LastWriteTimeUtc -ne $before) { throw 'Invalid installer was executed.' }
Write-Host "PASS: desktop/VR restart, quoted portable install path, and corrupt-payload rejection. Fixtures: $testRoot"
