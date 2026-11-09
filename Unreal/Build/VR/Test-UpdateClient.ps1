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
using System.Diagnostics;
using System.Threading;
using System.Runtime.InteropServices;
class Fixture {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    static extern IntPtr CreateWindowEx(int exStyle, string cls, string title, int style,
        int x, int y, int width, int height, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] static extern bool DestroyWindow(IntPtr window);
    static int Main(string[] args) {
        if (args.Length == 4 && args[0] == "lock-game") {
            using (var held = new FileStream(args[1], FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite)) {
                File.WriteAllText(args[2], Process.GetCurrentProcess().Id.ToString());
                Thread.Sleep(Int32.Parse(args[3]));
            }
            return 0;
        }
        foreach (string arg in args) {
            if (arg.StartsWith("/DIR=")) {
                string path = arg.Substring(5);
                File.WriteAllLines(Path.Combine(path,"installer-args.txt"),args);
                File.AppendAllText(Path.Combine(path,"installer-runs.txt"),"install\n");
                var scenarioFile = Path.Combine(path,"scenario.txt");
                string scenario = File.Exists(scenarioFile) ? File.ReadAllText(scenarioFile).Trim() : "";
                if (scenario == "installer-failure") return 23;
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
 # Start-Process from PowerShell 7 inherits its module search path. Give the
 # Windows PowerShell 5.1 fixture its own inbox modules, as the game launch does.
 $modulePath=$env:PSModulePath
 try {
  if(!$NativeHelper){$env:PSModulePath=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\Modules'}
  $process=Start-Process -FilePath $program -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
 } finally {$env:PSModulePath=$modulePath}
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
foreach ($scenario in @('locked-restart-temporary','locked-restart-persistent','denied-restart','denied-installer','installer-failure')) {
    $scenario | Set-Content -LiteralPath (Join-Path $gameRoot 'scenario.txt')
    foreach ($file in @('game-args.txt','installer-runs.txt','lock-ready.txt')) {
        $path = Join-Path $gameRoot $file
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
    }
    $restoreAcl=$null; $deniedPath=$null
    try {
        if ($scenario.StartsWith('denied-')) {
            $deniedPath = if($scenario -eq 'denied-installer'){$fixture}else{Join-Path $gameBin 'ACUnreal.exe'}
            $restoreAcl=Get-Acl -LiteralPath $deniedPath
            $deniedAcl=Get-Acl -LiteralPath $deniedPath
            $rule=[System.Security.AccessControl.FileSystemAccessRule]::new([System.Security.Principal.WindowsIdentity]::GetCurrent().User,[System.Security.AccessControl.FileSystemRights]::ExecuteFile,[System.Security.AccessControl.AccessControlType]::Deny)
            $deniedAcl.AddAccessRule($rule);Set-Acl -LiteralPath $deniedPath -AclObject $deniedAcl
        }
        if ($scenario.StartsWith('locked-restart')) {
            $marker = Join-Path $gameRoot 'lock-ready.txt'
            $duration = if($scenario.EndsWith('temporary')){5000}else{30000}
            $lockArgs=@('lock-game',('"'+(Join-Path $gameBin 'ACUnreal.exe')+'"'),('"'+$marker+'"'),$duration)
            $locker = Start-Process -FilePath $fixture -ArgumentList $lockArgs -WindowStyle Hidden -PassThru
            $deadline=(Get-Date).AddSeconds(10)
            while (!(Test-Path -LiteralPath $marker) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 20 }
            if (!(Test-Path -LiteralPath $marker)) { throw 'File lock fixture did not start.' }
        }
        $exit = Invoke-Helper $hash 'Desktop'
        $result = Get-Content -LiteralPath (Join-Path $testRoot 'update-result.txt') -Raw
        $runsFile=Join-Path $gameRoot 'installer-runs.txt'
        if ($scenario -eq 'denied-installer') {
            if (Test-Path -LiteralPath $runsFile) { throw 'Denied installer unexpectedly ran.' }
        } elseif (@(Get-Content -LiteralPath $runsFile).Count -ne 1) { throw 'Installer reran during recovery.' }
        if ($scenario -eq 'locked-restart-temporary') {
            if ($exit -ne 0 -or $result -notmatch 'temporarily blocked; retry') { throw "Transient lock was not recovered: $result" }
            $deadline = (Get-Date).AddSeconds(10)
            while (!(Test-Path -LiteralPath $argsFile) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
            if (!(Test-Path -LiteralPath $argsFile)) { throw 'No game restart after transient lock.' }
        } elseif ($scenario -in @('locked-restart-persistent','denied-restart')) {
            if ($exit -ne 1 -or $result -notmatch 'update installed successfully, but the game could not reopen' -or $result -notmatch 'Restarting updated game failed' -or $result -notmatch 'Windows error (5|32|33)' -or !$result.Replace('/','\').Contains($gameBin)) { throw "Missing accurate restart failure diagnostics: $result" }
            if ($scenario -eq 'denied-restart' -and $result -notmatch 'Windows error 5:') { throw 'Access denial lost the Windows error code.' }
            if (Test-Path -LiteralPath $argsFile) { throw 'Locked game unexpectedly started.' }
        } elseif ($scenario -eq 'denied-installer') {
            if ($exit -ne 1 -or $result -notmatch 'Starting installer failed' -or $result -notmatch 'Windows error 5:' -or $result -match 'Update installed successfully' -or !$result.Contains($fixture)) { throw "Missing accurate installer launch failure diagnostics: $result" }
            if (Test-Path -LiteralPath $argsFile) { throw 'Game restarted after blocked installation.' }
        } else {
            if ($exit -ne 1 -or $result -notmatch 'Installer exit code: 23' -or $result -match 'Update installed successfully') { throw "Incorrect installation failure status: $result" }
            if (Test-Path -LiteralPath $argsFile) { throw 'Game restarted after failed installation.' }
        }
    } finally {
        if ($restoreAcl) { Set-Acl -LiteralPath $deniedPath -AclObject $restoreAcl }
        $marker = Join-Path $gameRoot 'lock-ready.txt'
        if (Test-Path -LiteralPath $marker) {
            $locker = Get-Process -Id ([int](Get-Content -LiteralPath $marker -Raw)) -ErrorAction SilentlyContinue
            # Only stop our isolated fixture, never an unrelated process.
            if ($locker -and $locker.Path -eq $fixture) { $locker.Kill(); $locker.WaitForExit() }
        }
    }
}
Write-Host "PASS: desktop/VR restart, paths, verification, launch locks, denied execute access, accurate install/restart diagnostics, and no installer reruns. Fixtures: $testRoot"
