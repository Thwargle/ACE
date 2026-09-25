#requires -Version 7.0
<#
Capture a stationary A/B/A comparison without restarting the game. This is for
live console variables only. CachedActorDraws supports live changes from v76.
Keep the headset on, game visible, and view/menus unchanged. Screenshots and
heavy render audits must be taken outside these intervals.

Example:
  .\Measure-QuestRenderComparison.ps1 -Serial <serial> -Variable ShowFlag.PointLights -TestValue 0

Outputs are checkpointed under Quest/Performance. If the host is terminated,
comparison.json records the original value and exact console command to restore.
#>
[CmdletBinding()]
param(
    [string]$Serial,
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z][A-Za-z0-9_.]+$')][string]$Variable,
    [Parameter(Mandatory)][ValidatePattern('^-?[0-9]+(\.[0-9]+)?$')][string]$TestValue,
    [ValidateRange(10, 60)][int]$Seconds = 20
)
$ErrorActionPreference = 'Stop'
$adb = Join-Path $PSScriptRoot '..\AndroidSDK\platform-tools\adb.exe'
$package = 'com.acecommunity.questtest'
function Invoke-Adb([string[]]$Arguments) {
    $output = @(& $adb -s $Serial @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "ADB failed: $($output -join [Environment]::NewLine)" }
    return $output -join [Environment]::NewLine
}
if (!$Serial) {
    $devices = @(& $adb devices | Select-String '^([^\s]+)\s+device$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value })
    if ($devices.Count -ne 1) { throw 'Specify one authorized Quest using -Serial.' }
    $Serial = $devices[0]
}
if ($Variable -in @('r.Android.SupportsTimestampQueries','r.Mobile.Forward.LocalLightsSinglePermutation')) {
    throw 'Compare this setting in separate launches; existing cached shader/draw state is not refreshed by this live-toggle harness.'
}
if ($Variable -eq 'ace.Render.CachedActorDraws') {
    $packageInfo = Invoke-Adb @('shell', 'dumpsys', 'package', $package)
    if ($packageInfo -notmatch 'versionCode=(\d+)' -or [int]$Matches[1] -lt 76) {
        throw 'Live actor-cache comparisons require Quest v76 or newer.'
    }
}
function Get-GameProcessId {
    # Unreal's detached crash handler inherits the process name, so pidof
    # returns two IDs. Android's activity manager identifies the actual app.
    $activity = Invoke-Adb @('shell', 'dumpsys', 'activity', 'processes')
    $pattern = 'ProcessRecord\{\S+\s+(\d+):' + [regex]::Escape($package) + '/'
    $ids = @([regex]::Matches($activity, $pattern) |
        ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    if ($ids.Count -ne 1) { throw 'Expected one running game process.' }
    return $ids[0]
}
$gameProcessId = Get-GameProcessId
function Get-AppLog { Invoke-Adb @('logcat', '-d', '-v', 'threadtime', "--pid=$gameProcessId") }
function Assert-Visible {
    if ((Invoke-Adb @('shell', 'dumpsys', 'power')) -notmatch 'mWakefulness=Awake') {
        throw 'Headset asleep. Keep the game visible before starting a comparison.'
    }
    if ((Get-GameProcessId) -ne $gameProcessId) {
        throw 'Game process changed; this comparison cannot continue.'
    }
    $log = Get-AppLog
    $frames = @([regex]::Matches($log, 'ACE VR frame timings: [^\r\n]+'))
    if (!$frames.Count -or $frames[-1].Value -notmatch 'world=1 menus=0') {
        throw 'Log into the world and close menus before starting a comparison.'
    }
}
function Send-Console([string]$Command) {
    # Names and numeric values are validated above; never interpolate arbitrary shell text.
    $null = Invoke-Adb @('shell', "am broadcast -a android.intent.action.RUN -p $package --es cmd '$Command'")
}
function Read-Variable {
    # Only accept the response after our query; a previous query in logcat is insufficient.
    $stamp = (Invoke-Adb @('shell', "date '+%m-%d %H:%M:%S.000'")).Trim()
    Send-Console $Variable
    Start-Sleep -Milliseconds 500
    $log = Invoke-Adb @('logcat', '-d', '-v', 'threadtime', "--pid=$gameProcessId", '-T', $stamp)
    $pattern = [regex]::Escape($Variable) + ' = "([^"\r\n]+)"'
    $values = @([regex]::Matches($log, $pattern))
    if (!$values.Count) { throw "No live console response for $Variable. Is the game visible?" }
    return $values[-1].Groups[1].Value
}
Assert-Visible
$original = Read-Variable
if ($original -notmatch '^-?[0-9]+(\.[0-9]+)?$') { throw "Expected a numeric console value, got '$original'." }
$directory = Join-Path $PSScriptRoot ('..\Performance\Comparison-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$null = New-Item -ItemType Directory -Path $directory
$state = [ordered]@{ serial=$Serial; processId=$gameProcessId; variable=$Variable;
    original=$original; testValue=$TestValue; seconds=$Seconds; status='running';
    restoreCommand="$Variable $original"; restored=$true; phases=@() }
function Save-State { $state | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $directory 'comparison.json') -Encoding utf8 }
function Capture-Phase([string]$Name, [string]$Expected) {
    Assert-Visible
    if ((Read-Variable) -ne $Expected) { throw "Could not verify $Variable=$Expected." }
    Start-Sleep -Seconds 3
    $start = (Invoke-Adb @('shell', "date '+%m-%d %H:%M:%S'")).Trim()
    Start-Sleep -Seconds $Seconds
    $end = (Invoke-Adb @('shell', "date '+%m-%d %H:%M:%S'")).Trim()
    $log = Get-AppLog
    $log | Set-Content (Join-Path $directory "$Name-logcat.txt") -Encoding utf8
    $lines = @($log -split '\r?\n' | Where-Object {
        $_.Length -ge 18 -and $_.Substring(0,18) -ge $start -and $_.Substring(0,18) -lt $end
    })
    $samples = @($lines | Where-Object { $_ -match ' I VrApi\s+: FPS=' })
    $invalid = @($lines | Where-Object {
        $_ -match 'APP_EVENT_STATE_ON_PAUSE|APP_EVENT_STATE_ON_STOP|APP_EVENT_STATE_BACKGROUND|ACE VR frame timings:.*(?:world=0|menus=1)'
    })
    $phase = [ordered]@{name=$Name; value=$Expected; start=$start; end=$end; samples=$samples.Count;
        valid=($samples.Count -ge ($Seconds - 2) -and $invalid.Count -eq 0)}
    $state.phases += $phase
    Save-State
    if (!$phase.valid) { throw "Interrupted $Name capture. Partial samples saved but excluded from comparison." }
    Assert-Visible
    Write-Host "$Name saved: $($samples.Count) samples."
}
Save-State
try {
    Capture-Phase 'Baseline' $original
    $state.restored = $false
    Save-State
    Send-Console "$Variable $TestValue"
    Capture-Phase 'Changed' $TestValue
    Send-Console "$Variable $original"
    if ((Read-Variable) -ne $original) { throw 'Original value was not restored.' }
    $state.restored = $true
    Save-State
    Capture-Phase 'Restored' $original
    $state.status = 'complete'
} catch {
    $state.status = 'incomplete'
    $state.error = $_.Exception.Message
    throw
} finally {
    if (!$state.restored) {
        try {
            Send-Console "$Variable $original"
            $state.restored = (Read-Variable) -eq $original
        } catch { $state.restoreError = $_.Exception.Message }
    }
    Save-State
    Write-Host "Report: $directory"
    if (!$state.restored) { Write-Warning "Restoration not confirmed. Run the console command: $($state.restoreCommand)" }
}
