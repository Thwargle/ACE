#requires -Version 5.1
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $root 'Sharing/Quest-DataTransfer.ps1')
$fixture = Join-Path $root ("Logs\DataTransferTests-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + "\Friend's Quest")
$sourceDir = Join-Path $fixture 'DAT source'
$deviceDir = Join-Path $fixture 'device'
New-Item -ItemType Directory -Path $sourceDir,$deviceDir -Force | Out-Null
$oldPipeDir = $env:ACE_TEST_PIPE_DIR
$env:ACE_TEST_PIPE_DIR = $deviceDir
$AdbPath = Join-Path $fixture 'Fake Adb.exe'
# A real native subprocess reads the same binary stdin path used for ADB.
Add-Type -OutputAssembly $AdbPath -OutputType ConsoleApplication -TypeDefinition @'
using System;
using System.IO;
using System.Text.RegularExpressions;
public class FakeAdb {
 public static int Main(string[] args) {
  string root = Environment.GetEnvironmentVariable("ACE_TEST_PIPE_DIR");
  if (args.Length != 5 || args[0] != "-s" || args[1] != "QUEST_A" || args[2] != "shell" || args[3] != "-T") return 8;
  var match = Regex.Match(args[4], @"cat > files/ACUnreal/Saved/DAT/(client_\w+\.dat\.installing)'$");
  if (!match.Success) return 9;
  string path = Path.Combine(root, match.Groups[1].Value);
  using (var input = Console.OpenStandardInput()) using (var output = File.Create(path)) input.CopyTo(output);
  string mode = File.Exists(Path.Combine(root,"mode")) ? File.ReadAllText(Path.Combine(root,"mode")).Trim() : "";
  if (mode == "disconnect") { Console.Error.WriteLine("adb: error: device offline"); return 1; }
  if (mode == "corrupt") File.WriteAllText(path,"corrupted transfer");
  if (mode == "corrupt-once") { File.WriteAllText(path,"corrupted transfer"); File.WriteAllText(Path.Combine(root,"mode"),""); }
  if (mode == "storage") { Console.Error.WriteLine("No space left on device"); return 1; }
  return 0;
 }
}
'@
$package = 'com.acecommunity.questtest'
$Serial = 'QUEST_A'
$deviceArgs = @('-s',$Serial)
$script:commands = @()
$script:retries = 0
function Wait-QuestConnection { param([string]$RequestedSerial)
    if ($RequestedSerial -ne $Serial) { throw 'Retry changed headsets' }
    $script:retries++
    return [pscustomobject]@{Serial=$Serial;Model='Quest 3'}
}
function Invoke-Adb { param([string[]]$Arguments)
    $cmd = $Arguments -join ' '
    $script:commands += $cmd
    if ($cmd -match 'shell am force-stop com.acecommunity.questtest$|shell run-as com.acecommunity.questtest mkdir -p files/ACUnreal/Saved/DAT$') { return }
    if ($cmd -match 'if \[ -f files/ACUnreal/Saved/DAT/(client_\w+\.dat) \]') {
        $path = Join-Path $deviceDir $Matches[1]
        if (Test-Path -LiteralPath $path) { return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $path }
        return
    }
    if ($cmd -match 'stat -c %s files/ACUnreal/Saved/DAT/(client_\w+\.dat\.installing)$') {
        return [string](Get-Item -LiteralPath (Join-Path $deviceDir $Matches[1])).Length
    }
    if ($cmd -match 'sha256sum files/ACUnreal/Saved/DAT/(client_\w+\.dat\.installing)$') {
        return (Get-FileHash -LiteralPath (Join-Path $deviceDir $Matches[1]) -Algorithm SHA256).Hash.ToLowerInvariant() + '  file'
    }
    if ($cmd -match 'mv files/ACUnreal/Saved/DAT/(client_\w+\.dat\.installing) files/ACUnreal/Saved/DAT/(client_\w+\.dat)$') {
        # Test fixtures only; both resolved targets must remain in this device directory.
        $from = [IO.Path]::GetFullPath((Join-Path $deviceDir $Matches[1]))
        $to = [IO.Path]::GetFullPath((Join-Path $deviceDir $Matches[2]))
        if (!$from.StartsWith($deviceDir + '\') -or !$to.StartsWith($deviceDir + '\')) { throw 'Fixture path escaped' }
        [IO.File]::Copy($from,$to,$true)
        return
    }
    throw "Unexpected ADB command $cmd"
}
$bytes = New-Object byte[] (1024 * 1024 + 513)
$random = New-Object Random(27)
$random.NextBytes($bytes)
foreach ($name in @('client_portal.dat','client_cell_1.dat','client_local_English.dat','client_highres.dat')) {
    [IO.File]::WriteAllBytes((Join-Path $sourceDir $name),$bytes)
}
try {
    Install-QuestData -DatDirectory $sourceDir -DiagnosticDirectory $fixture
    foreach ($name in @('client_portal.dat','client_cell_1.dat','client_local_English.dat','client_highres.dat')) {
        if ((Get-FileHash -LiteralPath (Join-Path $deviceDir $name)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $sourceDir $name)).Hash) { throw 'Binary stream changed bytes' }
    }
    Write-Output 'PASS raw binary transfer through Windows PowerShell 5.1, including whitespace/apostrophe paths'
    $script:commands = @()
    Install-QuestData -DatDirectory $sourceDir -DiagnosticDirectory $fixture
    if (($script:commands -join '\n') -match '\bmv\b') { throw 'Replaced already verified data' }
    Write-Output 'PASS verified files are skipped'
    $target = Join-Path $deviceDir 'client_cell_1.dat'
    'old installed data' | Set-Content -LiteralPath $target
    'corrupt-once' | Set-Content -LiteralPath (Join-Path $deviceDir 'mode')
    Install-QuestData -DatDirectory $sourceDir -DiagnosticDirectory $fixture
    if ($script:retries -ne 1 -or (Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath (Join-Path $sourceDir 'client_cell_1.dat')).Hash) { throw 'Retry did not repair data' }
    Write-Output 'PASS corrupt transfer retries on the same headset and repairs the failed cell DAT'
    foreach ($mode in @('corrupt','disconnect','storage')) {
        'old installed data' | Set-Content -LiteralPath $target
        $before = (Get-FileHash -LiteralPath $target).Hash
        $mode | Set-Content -LiteralPath (Join-Path $deviceDir 'mode')
        $script:retries = 0
        $failed = $false
        try { Install-QuestData -DatDirectory $sourceDir -DiagnosticDirectory $fixture } catch { $failed = $_.Exception.Message -match 'Game data installation did not complete' }
        if (!$failed -or (Get-FileHash -LiteralPath $target).Hash -ne $before) { throw "Failure mode $mode replaced an unverified file or claimed success" }
        $expectedRetries = if ($mode -eq 'storage') { 0 } else { 2 }
        if ($script:retries -ne $expectedRetries) { throw "Unexpected retries for $mode" }
        Write-Output "PASS $mode preserves installed files and reports the failure"
    }
    Write-Output "All 6 data-transfer checks passed. Evidence: $fixture"
} finally { $env:ACE_TEST_PIPE_DIR = $oldPipeDir }
