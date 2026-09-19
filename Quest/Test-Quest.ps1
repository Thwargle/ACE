#requires -Version 7.0
param([string]$Serial)
$ErrorActionPreference = 'Stop'
$adb = Join-Path $PSScriptRoot 'AndroidSDK\platform-tools\adb.exe'
$devices = @(& $adb devices | Select-String '^([^\s]+)\s+device$' | ForEach-Object { $_.Matches[0].Groups[1].Value })
if (!$Serial) {
    if ($devices.Count -ne 1) { throw 'Specify one authorized Quest using -Serial.' }
    $Serial = $devices[0]
}
if ($Serial -notin $devices) { throw 'Device is not authorized.' }
$deviceArgs = @('-s', $Serial)
$model = (& $adb @deviceArgs shell getprop ro.product.model | Out-String).Trim()
if ($model -notmatch '^Quest 3(S)?$') { throw "Expected Quest 3, found '$model'." }
$package = 'com.acecommunity.questtest'
$powerState = (& $adb @deviceArgs shell dumpsys power | Out-String)
if ($powerState -notmatch 'mWakefulness=Awake') {
    throw 'Put on the headset and keep it awake, then run this test again. Launching during sleep can stall Android startup.'
}
$reportName = 'QuestSmoke-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
$report = Join-Path $PSScriptRoot "Logs\$reportName"
New-Item -ItemType Directory -Force -Path $report | Out-Null
$remoteSaved = 'files/ACUnreal/Saved'
$absoluteSaved = "/data/user/0/$package/$remoteSaved"
$argsText = '-UserDir=/data/user/0/com.acecommunity.questtest/files/ACUnreal/ -ExecCmds="Automation RunTests ACE.Quest.NativeSmoke+ACE.Packaging.Materials+ACE.Network.LoginHandshake+ACE.RetailParity.LocalLoginSettings+ACE.VR.GesturesAndSettings+ACE.Packaging.TextureBudget+ACE.Packaging.LandscapeTextureFidelity+ACE.Rendering.SceneAudit" -ReportExportPath=' + $absoluteSaved + '/Automation/' + $reportName
# Stop only the isolated test app. Run this before logging into a character.
& $adb @deviceArgs shell am force-stop $package
$launch = "am start -W -n $package/com.epicgames.unreal.GameActivity --es cmdline '$argsText'"
& $adb @deviceArgs shell $launch
if ($LASTEXITCODE -ne 0) { throw 'Test activity launch failed.' }
Write-Host "Native tests launched. Report directory: $report"
$deadline = (Get-Date).AddMinutes(3)
$found = $false
while ((Get-Date) -lt $deadline) {
    & $adb @deviceArgs shell run-as $package test -f "$remoteSaved/Automation/$reportName/index.json"
    if ($LASTEXITCODE -eq 0) { $found = $true; break }
    $appProcess = (& $adb @deviceArgs shell pidof $package | Out-String).Trim()
    if (!$appProcess) { throw 'The test application exited before producing its report. Inspect headset startup logs.' }
    Start-Sleep -Seconds 5
}
if (!$found) { throw 'No automation report arrived. Inspect headset startup logs.' }
& $adb @deviceArgs exec-out run-as $package cat "$remoteSaved/Automation/$reportName/index.json" | Set-Content (Join-Path $report 'index.json') -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw 'Could not retrieve native test report.' }
$result = Get-Content (Join-Path $report 'index.json') -Raw | ConvertFrom-Json
$result | Select-Object succeeded,succeededWithWarnings,failed,notRun,inProcess | Format-List
if ($result.failed -gt 0 -or ($result.succeeded + $result.succeededWithWarnings) -lt 8 -or $result.notRun -gt 0 -or $result.inProcess -gt 0) {
    throw "Native smoke tests did not all pass. See $report"
}
Write-Host "Native smoke tests passed. Results: $report"
