#requires -Version 5.1
param(
    [string]$AdbPath,
    [string]$Serial,
    [string]$OutputDirectory = $PSScriptRoot,
    [ValidateRange(1, 60)][int]$CommandTimeoutSeconds = 15
)
$ErrorActionPreference = 'Stop'
$package = 'com.acecommunity.questtest'

# Read-only: do not launch/stop the game, clear logs, repair data, or reset the
# headset. Capture the failing launch before a reboot or reinstall erases it.
function Invoke-DiagnosticAdb {
    param([string[]]$Arguments)
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $AdbPath
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.Encoding]::UTF8
    $start.StandardErrorEncoding = [Text.Encoding]::UTF8
    # Windows argv quoting; no shell and no evaluation of argument contents.
    $start.Arguments = ($Arguments | ForEach-Object {
        '"' + [regex]::Replace([regex]::Replace($_, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
    }) -join ' '
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    try {
        if (!$process.Start()) { throw 'ADB could not start.' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $finished = $process.WaitForExit($CommandTimeoutSeconds * 1000)
        if (!$finished) {
            # Stop only this read command, never the shared ADB server or app.
            $process.Kill()
            $process.WaitForExit()
        }
        $output = $stdout.GetAwaiter().GetResult()
        $errorText = $stderr.GetAwaiter().GetResult()
        [pscustomobject]@{
            Code = $(if ($finished) { $process.ExitCode } else { -1 })
            Text = $output
            Error = $(if ($finished) { $errorText } else { "ADB command timed out after $CommandTimeoutSeconds seconds. $errorText" })
        }
    } finally { $process.Dispose() }
}

if (!$AdbPath) {
    foreach ($candidate in @((Join-Path $PSScriptRoot 'platform-tools/adb.exe'),
        (Join-Path $PSScriptRoot '../AndroidSDK/platform-tools/adb.exe'))) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $AdbPath = $candidate; break }
    }
    if (!$AdbPath) {
        $command = Get-Command adb.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($command) { $AdbPath = $command.Source }
    }
}
if (!$AdbPath -or !(Test-Path -LiteralPath $AdbPath -PathType Leaf)) {
    throw 'ADB not found. Put platform-tools beside Diagnose-Quest.cmd or pass -AdbPath with the path to adb.exe.'
}
$AdbPath = (Resolve-Path -LiteralPath $AdbPath).Path
$listing = Invoke-DiagnosticAdb @('devices', '-l')
if ($listing.Code -ne 0) { throw "Cannot list USB devices: $($listing.Error)" }
$candidates = @(
    foreach ($line in ($listing.Text -split '\r?\n')) {
        if ($line -notmatch '^\s*([A-Za-z0-9._:-]+)\s+device(?:\s|$)') { continue }
        $deviceSerial = $Matches[1]
        if ($Serial -and $Serial -ne $deviceSerial) { continue }
        $model = Invoke-DiagnosticAdb @('-s', $deviceSerial, 'shell', 'getprop', 'ro.product.model')
        if ($model.Code -eq 0 -and $model.Text.Trim() -match '^Quest[ _]3(?:S)?$') {
            [pscustomobject]@{ Serial = $deviceSerial; Model = $model.Text.Trim() }
        }
    }
)
if ($candidates.Count -ne 1) {
    if ($candidates.Count -gt 1) { throw 'More than one Quest is connected. Pass -Serial with the intended device from adb devices -l.' }
    throw "No authorized Quest 3/3S found. Connect USB and accept Allow USB debugging inside the headset. Device list:`n$($listing.Text)"
}
$device = $candidates[0]
$deviceArgs = @('-s', $device.Serial)
$report = Join-Path $OutputDirectory ('Quest-Diagnostics-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 6))
New-Item -ItemType Directory -Path $report -Force | Out-Null
$notes = New-Object 'System.Collections.Generic.List[string]'
$notes.Add('AC:VR startup diagnostic (read-only)')
$notes.Add('Captured: ' + [DateTime]::Now.ToString('o'))
$notes.Add('Headset: ' + $device.Model)
function Read-Device {
    param([string]$Name, [string[]]$Arguments)
    Write-Host "Reading $Name..."
    $result = Invoke-DiagnosticAdb ($deviceArgs + $Arguments)
    if ($result.Code -ne 0) { $notes.Add("${Name}: unavailable (exit $($result.Code)): $($result.Error.Trim())") }
    return $result
}
function Save-Result {
    param([string]$Name, $Result)
    ($Result.Text + "`n" + $Result.Error) | Set-Content -LiteralPath (Join-Path $report $Name) -Encoding UTF8
}

$info = Read-Device 'installed package' @('shell', 'dumpsys', 'package', $package)
# Only package identity/version fields, not unrelated system packages/settings.
$identity = @($info.Text -split '\r?\n' | Where-Object { $_ -match 'userId=|versionCode=|versionName=|firstInstallTime=|lastUpdateTime=|pkgFlags=|primaryCpuAbi=|codePath=' })
$identity | Set-Content -LiteralPath (Join-Path $report 'package.txt') -Encoding UTF8
if ($info.Text -notmatch 'versionCode=') { $notes.Add('AC:VR package was not found or its version could not be read.') }
foreach ($property in @('ro.build.version.release', 'ro.build.version.sdk', 'ro.build.display.id')) {
    $value = Read-Device $property @('shell', 'getprop', $property)
    $notes.Add($property + ': ' + $value.Text.Trim())
}
Save-Result 'storage.txt' (Read-Device 'free storage' @('shell', 'df', '-h', '/data'))
$power = Read-Device 'headset wake state' @('shell', 'dumpsys', 'power')
($power.Text -split '\r?\n' | Where-Object { $_ -match 'mWakefulness=|mInteractive=|mWakefulnessChanging=' }) |
    Set-Content -LiteralPath (Join-Path $report 'wake-state.txt') -Encoding UTF8
Save-Result 'exit-info.txt' (Read-Device 'app exit history' @('shell', 'dumpsys', 'activity', 'exit-info', $package))
$anr = Read-Device 'last Android ANR' @('shell', 'dumpsys', 'activity', 'lastanr')
if ($anr.Text -match ('(?m)ANR in ' + [regex]::Escape($package) + '(?:\s|/|$)')) {
    Save-Result 'last-anr.txt' $anr
} else { $notes.Add('No retained last-ANR report for AC:VR (it may have expired, or a different app failed later).') }

# App UID captures previous processes as well as a still-running one. System
# lifecycle/ANR messages live under a different UID, captured separately below.
if ($info.Text -match '\buserId=(\d+)') {
    Save-Result 'app-logcat.txt' (Read-Device 'app logcat' @('logcat', '-d', '-b', 'main', '-b', 'system', '-b', 'crash', '-v', 'threadtime', '-t', '5000', ('--uid=' + $Matches[1])))
} else { $notes.Add('App logcat could not be scoped because the installed UID was unavailable.') }
$system = Read-Device 'Android launch messages' @('logcat', '-d', '-b', 'system', '-v', 'threadtime', '-t', '5000')
($system.Text -split '\r?\n' | Where-Object { $_ -match [regex]::Escape($package) }) |
    Set-Content -LiteralPath (Join-Path $report 'launch-events.txt') -Encoding UTF8
$processId = Read-Device 'app process' @('shell', 'pidof', $package)
$notes.Add('Current process: ' + $(if ($processId.Text.Trim()) { $processId.Text.Trim() } else { 'not running' }))
if ($processId.Text.Trim()) {
    Save-Result 'app-memory.txt' (Read-Device 'app memory' @('shell', 'dumpsys', 'meminfo', $package))
    # Android's native backtrace command can identify the blocked game/render
    # thread while the app is still hung. Some OS builds deny it; retain that
    # error and the other evidence rather than requiring root or restarting.
    if ($processId.Text.Trim() -match '^\d+$') {
        Save-Result 'native-threads.txt' (Read-Device 'native thread stacks' @('shell', 'debuggerd', '-b', $processId.Text.Trim()))
    }
}

# Fixed, app-owned paths only. Do not collect encrypted logins or configs.
$saved = 'files/ACUnreal/Saved'
foreach ($name in @('client_portal.dat', 'client_cell_1.dat', 'client_local_English.dat', 'client_highres.dat')) {
    $size = Read-Device $name @('shell', 'run-as', $package, 'stat', '-c', '%s', "$saved/DAT/$name")
    $notes.Add($name + ': ' + $(if ($size.Code -eq 0) { $size.Text.Trim() + ' bytes' } else { 'missing or unreadable (highres is optional)' }))
}
Save-Result 'ACUnreal-tail.log' (Read-Device 'recent AC:VR game log' @('shell', 'run-as', $package, 'tail', '-n', '2500', "$saved/Logs/ACUnreal.log"))
$notes.Add('No app settings, DATs, or saved-login files were collected or modified. Logs may contain character/chat text; review before sharing.')
$notes | Set-Content -LiteralPath (Join-Path $report 'summary.txt') -Encoding UTF8
$zip = $report + '.zip'
Compress-Archive -LiteralPath @(Get-ChildItem -LiteralPath $report -File | ForEach-Object FullName) -DestinationPath $zip -CompressionLevel Optimal
Write-Host "Diagnostic saved: $zip"
Write-Host 'Send this ZIP with the failure report. No installation or game data was changed.'
