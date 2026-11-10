#requires -Version 5.1
$ErrorActionPreference = 'Stop'
$quest = Split-Path $PSScriptRoot -Parent
$workspace = Join-Path $quest ('Logs/DiagnosticsTests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $workspace -Force | Out-Null
$exe = Join-Path $workspace "Fake ADB's tool.exe"
# A real child process tests argv quoting, stderr draining and process timeout
# under Windows PowerShell 5.1, not just a mocked PowerShell function.
Add-Type -OutputAssembly $exe -OutputType ConsoleApplication -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Threading;
public class FakeAdb {
  public static int Main(string[] args) {
    Console.OutputEncoding = new UTF8Encoding(false);
    string mode = Environment.GetEnvironmentVariable("ACE_QUEST_DIAGNOSTIC_TEST_CASE");
    string root = Environment.GetEnvironmentVariable("ACE_QUEST_DIAGNOSTIC_TEST_ROOT");
    string cmd = String.Join(" ", args);
    File.AppendAllText(Path.Combine(root, "commands.txt"), cmd + Environment.NewLine);
    if (cmd == "devices -l") {
      Console.WriteLine("List of devices attached");
      if (mode == "none") return 0;
      if (mode == "unauthorized") { Console.WriteLine("QUEST_A\tunauthorized"); return 0; }
      Console.WriteLine("QUEST_A\tdevice model:Quest_3");
      Console.WriteLine("PHONE\tdevice model:Pixel");
      if (mode.StartsWith("multiple")) Console.WriteLine("QUEST_B\tdevice model:Quest_3S");
      return 0;
    }
    if (cmd.EndsWith("getprop ro.product.model")) {
      Console.WriteLine(args[1] == "PHONE" ? "Pixel" : "Quest 3"); return 0;
    }
    if (cmd.Contains("dumpsys package")) {
      Console.WriteLine("userId=10234\nversionCode=152\nversionName=2026.11.07.152"); return 0;
    }
    if (cmd.Contains("getprop")) { Console.WriteLine("test OS build"); return 0; }
    if (cmd.Contains("dumpsys power")) { Console.WriteLine("mWakefulness=Awake\nunrelated private setting"); return 0; }
    if (cmd.Contains("df -h")) { Console.WriteLine("/data free 12G"); return 0; }
    if (cmd.Contains("activity exit-info")) { Console.WriteLine("com.acecommunity.questtest reason=6 (ANR)"); return 0; }
    if (cmd.Contains("activity lastanr")) {
      Console.WriteLine(mode == "other-anr" ? "ANR in com.other.app\nOTHER PRIVATE CONTENT" :
        "ANR in com.acecommunity.questtest (com.epicgames.unreal.GameActivity)\nReason: Input dispatching timed out");
      return 0;
    }
    if (cmd.Contains("logcat")) {
      if (cmd.Contains("--uid=10234")) Console.WriteLine("UE startup trace");
      else Console.WriteLine("Start proc com.acecommunity.questtest\nOTHER PRIVATE CONTENT");
      return 0;
    }
    if (cmd.Contains("pidof")) { Console.WriteLine("4567"); return 0; }
    if (cmd.Contains("dumpsys meminfo")) {
      if (mode == "timeout") Thread.Sleep(15000);
      Console.WriteLine("TOTAL PSS: 50000"); return 0;
    }
    if (cmd.Contains("debuggerd -b 4567")) {
      if (mode == "restricted") { Console.Error.WriteLine("Permission denied"); return 1; }
      Console.WriteLine("Native thread backtrace"); return 0;
    }
    if (cmd.Contains("stat -c %s")) {
      if (mode == "missing-data") { Console.Error.WriteLine("No such file or directory"); return 1; }
      Console.WriteLine("12345678"); return 0;
    }
    if (cmd.Contains("tail -n 2500 files/ACUnreal/Saved/Logs/ACUnreal.log")) { Console.WriteLine("AC:VR startup log"); return 0; }
    Console.Error.WriteLine("Unexpected command: " + cmd); return 42;
  }
}
'@
$previousCase = $env:ACE_QUEST_DIAGNOSTIC_TEST_CASE
$previousRoot = $env:ACE_QUEST_DIAGNOSTIC_TEST_ROOT
try {
    $env:ACE_QUEST_DIAGNOSTIC_TEST_ROOT = $workspace
    foreach ($case in @('healthy', 'other-anr', 'missing-data', 'timeout', 'restricted', 'none', 'unauthorized', 'multiple', 'multiple-selected')) {
        $env:ACE_QUEST_DIAGNOSTIC_TEST_CASE = $case
        $outputDirectory = Join-Path $workspace $case
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $options = @()
        if ($case -eq 'multiple-selected') { $options = @('-Serial', 'QUEST_B') }
        $ErrorActionPreference = 'Continue'
        $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $quest 'Sharing/Diagnose-Quest.ps1') -AdbPath $exe -OutputDirectory $outputDirectory -CommandTimeoutSeconds 1 @options 2>&1 | Out-String
        $code = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        $output | Set-Content -LiteralPath (Join-Path $workspace ($case + '.log'))
        if ($case -in @('none', 'unauthorized', 'multiple')) {
            if ($code -eq 0 -or $output -notmatch 'No authorized Quest|More than one Quest') { throw "Incorrect device failure: $case $output" }
            if (Test-Path -LiteralPath $outputDirectory) { throw 'Device-selection failure should not produce a misleading report.' }
        } else {
            if ($code -ne 0) { throw "Collector failed: $case $output" }
            $report = Get-ChildItem -LiteralPath $outputDirectory -Directory | Select-Object -First 1
            $summary = Get-Content -LiteralPath (Join-Path $report.FullName 'summary.txt') -Raw
            $appLog = Get-Content -LiteralPath (Join-Path $report.FullName 'app-logcat.txt') -Raw
            if ($appLog -notmatch 'UE startup trace') { throw 'App UID log capture failed.' }
            if ($summary -match 'exit 42') { throw 'Unexpected ADB request.' }
            if (!(Test-Path -LiteralPath ($report.FullName + '.zip'))) { throw 'Report ZIP missing.' }
            $all = Get-ChildItem -LiteralPath $report.FullName -File | Get-Content | Out-String
            if ($all -match 'OTHER PRIVATE CONTENT|unrelated private setting') { throw 'Unrelated app/settings leaked into report.' }
            if ($case -eq 'other-anr' -and (Test-Path -LiteralPath (Join-Path $report.FullName 'last-anr.txt'))) { throw 'Saved an unrelated ANR.' }
            if ($case -eq 'missing-data' -and $summary -notmatch 'client_portal.dat: missing or unreadable') { throw 'Missing DAT not reported.' }
            if ($case -eq 'timeout' -and ($summary -notmatch 'timed out' -or $watch.Elapsed.TotalSeconds -gt 12)) { throw 'Stalled ADB was not bounded.' }
            if ($case -eq 'restricted' -and $summary -notmatch 'native thread stacks: unavailable') { throw 'Denied backtrace not reported.' }
        }
        Write-Output "PASS $case"
    }
    $commands = Get-Content -LiteralPath (Join-Path $workspace 'commands.txt') -Raw
    if ($commands -match '\b(install|uninstall|force-stop|kill-server|reboot|clear|rm)\b|logcat -c|am start') { throw 'Diagnostics attempted a mutation.' }
    Write-Output "All 9 diagnostic cases passed. Evidence: $workspace"
} finally {
    $env:ACE_QUEST_DIAGNOSTIC_TEST_CASE = $previousCase
    $env:ACE_QUEST_DIAGNOSTIC_TEST_ROOT = $previousRoot
}
