#requires -Version 5.1
# Uses tiny fixture executables; never runs an actual installer or touches a real game install.
param([string]$NativeHelper='')
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
using System.Runtime.InteropServices;
class Fixture {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    static extern IntPtr CreateWindowEx(int exStyle, string cls, string title, int style,
        int x, int y, int width, int height, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] static extern bool DestroyWindow(IntPtr window);
    static int Main(string[] args) {
        foreach (string arg in args) {
            if (arg.StartsWith("/DIR=")) {
                string path = arg.Substring(5);
                File.WriteAllLines(Path.Combine(path,"installer-args.txt"),args);
                return 0;
            }
        }
        // Like Unreal, the game's first ShowWindow must not inherit SW_HIDE
        // from the hidden update helper. Verify OS visibility, not script text.
        var window = CreateWindowEx(0, "STATIC", "Update restart test", 0x00CF0000,
            0, 0, 160, 80, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
        if (window == IntPtr.Zero) return 2;
        ShowWindow(window, 1);
        var visible = IsWindowVisible(window);
        DestroyWindow(window);
        File.WriteAllText(Path.Combine(Environment.CurrentDirectory,"game-window-visible.txt"),visible.ToString());
        File.WriteAllLines(Path.Combine(Environment.CurrentDirectory,"game-args.txt"),args);
        return 0;
    }
}
'@ | Set-Content -LiteralPath $fakeSource
$fixture = Join-Path $testRoot 'Fixture.exe'
& $compiler /nologo /target:winexe "/out:$fixture" $fakeSource
if ($LASTEXITCODE -ne 0) { throw 'Fixture compilation failed.' }
$helper = Join-Path $testRoot $(if($NativeHelper){'Update-Client.exe'}else{'Update-Client.ps1'})
Copy-Item -LiteralPath $(if($NativeHelper){$NativeHelper}else{Join-Path $PSScriptRoot 'Update-Client.ps1'}) -Destination $helper
Copy-Item -LiteralPath $fixture -Destination (Join-Path $gameRoot 'ACUnreal.exe')
Copy-Item -LiteralPath $fixture -Destination (Join-Path $gameBin 'ACUnreal.exe')
$hash = (Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash
$powerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
function Invoke-Helper([string]$Checksum,[string]$Mode) {
 $arguments=@('-Installer',('"'+$fixture+'"'),'-ExpectedSha256',$Checksum,'-InstallDirectory',('"'+$gameRoot+'"'),'-GameProcessId','2147483647','-Mode',$Mode,'-NoDialogs')
 if($NativeHelper) {$program=$helper} else {$program=$powerShell;$arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$helper+'"'))+$arguments}
 $process=Start-Process -FilePath $program -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
 return $process.ExitCode
}
foreach ($mode in @('Desktop','VR')) {
    $argsFile = Join-Path $gameRoot 'game-args.txt'
    if (Test-Path -LiteralPath $argsFile) { Remove-Item -LiteralPath $argsFile }
    # Already exited PID exercises the normal race when the game closes before the helper starts.
    if ((Invoke-Helper $hash $mode) -ne 0) { throw "Update helper failed. See $testRoot/update-result.txt" }
    $deadline = (Get-Date).AddSeconds(10)
    while (!(Test-Path -LiteralPath $argsFile) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
    $expected = if ($mode -eq 'VR') { '-vr' } else { '-nohmd' }
    if ((Get-Content -LiteralPath $argsFile -Raw).Trim() -ne $expected) { throw 'Wrong restart mode.' }
    if ((Get-Content -LiteralPath (Join-Path $gameRoot 'game-window-visible.txt') -Raw).Trim() -ne 'True') { throw 'Restarted game window is hidden.' }
    $installerArgs = Get-Content -LiteralPath (Join-Path $gameRoot 'installer-args.txt')
    if ($installerArgs -notcontains '/ACPORTABLE=1' -or $installerArgs -notcontains "/DIR=$gameRoot") { throw 'Portable install path/arguments were not preserved.' }
}
$before = (Get-Item -LiteralPath (Join-Path $gameRoot 'installer-args.txt')).LastWriteTimeUtc
if ((Invoke-Helper ('0'*64) 'Desktop') -ne 1) { throw 'Invalid payload was accepted.' }
if ((Get-Item -LiteralPath (Join-Path $gameRoot 'installer-args.txt')).LastWriteTimeUtc -ne $before) { throw 'Invalid installer was executed.' }
Write-Host "PASS: visible desktop/VR restart, quoted portable install path, and corrupt-payload rejection. Fixtures: $testRoot"
