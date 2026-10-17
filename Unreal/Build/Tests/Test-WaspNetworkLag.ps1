#requires -Version 7.0
param(
 [string]$Executable='C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe',
 [string]$OutputRoot=(Join-Path $PSScriptRoot '../../Saved/WaspNetworkLag'),
 [int]$Width=1920,
 [int]$Height=1080,
 [int]$SampleSeconds=60,
 [double]$RttMs=23000,
 [double]$JitterMs=80,
 [double]$RecoveryMs=800,
 [double]$MinimumMeanFps=60,
 [double]$MaximumP99Ms=33.34,
 [double]$MaximumFrameMs=150
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../ACUnreal.uproject'))
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
if($SampleSeconds -lt 30 -or $SampleSeconds -gt 1800){throw 'Use a sample of 30 through 1800 seconds.'}
if($RttMs -le 0 -or $RttMs -gt 30000 -or $JitterMs -lt 0 -or $JitterMs -gt 2000 -or $RecoveryMs -le 0 -or $RecoveryMs -gt 10000){throw 'Invalid lag settings.'}
$results=@()
foreach($variant in @('baseline','lag')) {
 $directory=Join-Path $OutputRoot $variant
 if(Test-Path -LiteralPath $directory){throw "Use a fresh output directory: $directory"}
 New-Item -ItemType Directory -Path $directory -Force | Out-Null
 $lag=if($variant -eq 'lag'){$RttMs}else{0}
 $jitter=if($variant -eq 'lag'){$JitterMs}else{0}
 $recovery=if($variant -eq 'lag'){$RecoveryMs}else{0}
 # Both variants use identical crowd, movement, spells, chat, rendering and
 # loopback protocol handlers. Only packet delivery timing changes.
 $arguments=@("`"$project`"",'-game','-d3d12','-RenderOffscreen','-unattended','-nosound','-nohmd','-nop4',
  '-ACEPerfQuit','-ACEPerfNoTrace','-ACEPerfNoCapture','-ACEPerfNetworkLag','-ACEPerfPlayerMotion','-ACEPerfCombatChat','-ACEPerfConfigPanel',
  '-ACEPerfWaspCount=120','-ACEPerfVolleyWasps=8','-ACEPerfVolleyInterval=0.5',"-ACEPerfCastingSeconds=$SampleSeconds",
  "-ACEPerfLagRttMs=$lag","-ACEPerfLagJitterMs=$jitter","-ACEPerfLagRecoveryMs=$recovery","-ACEPerfResolution=${Width}x${Height}",
  "-UserDir=`"$directory`"","-abslog=`"$directory/run.log`"",'-ExecCmds="t.MaxFPS 0,r.VSync 0,ace.PerfScene wasps"')
 $process=Start-Process -FilePath $Executable -ArgumentList $arguments -WindowStyle Hidden -PassThru
 Write-Host "Wasp network ${variant}: 120 wasps, RTT $lag ms (PID $($process.Id))"
 if(!$process.WaitForExit(($SampleSeconds+150)*1000)){
  if(!$process.HasExited){$process.Kill()}
  throw "Benchmark timed out: $directory/run.log"
 }
 if($process.ExitCode -ne 0){throw "Benchmark exit $($process.ExitCode): $directory/run.log"}
 $reports=Join-Path $directory 'Saved/Performance/wasps'
 foreach($file in @('summary.json','network-lag.json','movement-summary.json')){
  if(!(Test-Path -LiteralPath (Join-Path $reports $file))){throw "Missing $file in $reports"}
 }
 $frames=Get-Content -LiteralPath (Join-Path $reports 'summary.json') -Raw | ConvertFrom-Json
 $network=Get-Content -LiteralPath (Join-Path $reports 'network-lag.json') -Raw | ConvertFrom-Json
 $movement=Get-Content -LiteralPath (Join-Path $reports 'movement-summary.json') -Raw | ConvertFrom-Json
 if($frames.mean_fps -lt $MinimumMeanFps -or $frames.p99_ms -gt $MaximumP99Ms -or $network.max_frame_ms -gt $MaximumFrameMs){throw "Frame-time regression in ${variant}: mean=$($frames.mean_fps) FPS p99=$($frames.p99_ms) ms max=$($network.max_frame_ms) ms"}
 if($network.send_errors -ne 0 -or $network.casts_received -lt 100 -or $network.corrections_received -lt 5 -or $network.correction_errors -ne 0){throw "Network workload or player reconciliation failed: $reports/network-lag.json"}
 if($movement.jumps -lt 5 -or $movement.travel_m -lt 10){throw "Movement/jump workload did not run: $reports/movement-summary.json"}
 if($variant -eq 'lag' -and ($network.largest_correction_cm -lt 10 -or $network.peak_buffered -le 0 -or $network.recovered -le 0)){throw 'Lag case did not exercise rubber-banding and packet recovery.'}
 $results+= [pscustomobject]@{variant=$variant;frames=$frames;network=$network;movement=$movement}
 Write-Host "$variant passed: $($frames.mean_fps) FPS; p99 $($frames.p99_ms) ms; max $($network.max_frame_ms) ms; corrections $($network.corrections_received)"
}
if($results[1].frames.mean_ms -gt ($results[0].frames.mean_ms*1.25+1) -or $results[1].frames.p99_ms -gt ($results[0].frames.p99_ms*1.5+2)){
 throw 'Simulated lag materially worsened frame times relative to the matching baseline.'
}
$results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputRoot 'comparison.json') -Encoding utf8
Write-Host 'PASS: delayed/reordered packets corrected the player without a sustained frame-rate collapse.'
