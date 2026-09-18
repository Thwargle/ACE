#requires -Version 7.0
param(
 [string]$Executable = (Join-Path $PSScriptRoot '../../Saved/VRWindowsArchive/Windows/ACEViewer/Binaries/Win64/ACEViewer.exe'),
 [string]$OutputRoot = (Join-Path $PSScriptRoot '../../Saved/PCPerformance/Review'),
 [string[]]$Scenes = @('outdoor','indoor','effects'),
 [ValidateSet('baseline','optimized')][string[]]$Variants = @('baseline','optimized'),
 [ValidateSet('scene','particles','cached-world','particle-distance','particle-idle')][string]$Comparison = 'scene'
)
$ErrorActionPreference='Stop'
foreach ($scene in $Scenes) {
 if ($scene -notin @('outdoor','indoor','effects','caul')) { throw "Unknown scene $scene" }
 foreach ($variant in $Variants) {
  $runDirectory=Join-Path $OutputRoot "$scene-$variant"
  if (Test-Path (Join-Path $runDirectory "Saved/Performance/$scene/summary.json")) { throw "Run already exists: $runDirectory" }
  New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null
  $enabled=if($variant -eq 'optimized'){1}else{0}
  $settings=switch($Comparison) {
   'particles' { "ace.UI.IndexedLookups 1,ace.Render.ReusePrimitiveBuffers 1,ace.Particles.ActivePrefix $enabled" }
   'cached-world' { "ace.UI.IndexedLookups 1,ace.Render.ReusePrimitiveBuffers 1,ace.Render.CachedWorldDraws $enabled" }
   'particle-distance' { "ace.UI.IndexedLookups 1,ace.Render.ReusePrimitiveBuffers 1,ace.Particles.DistanceCulling $enabled" }
   'particle-idle' { "ace.UI.IndexedLookups 1,ace.Render.ReusePrimitiveBuffers 1,ace.Particles.DistanceCulling 1,ace.Particles.IdleTickInterval $(if($enabled){'.1'}else{'0'})" }
   default { "ace.UI.IndexedLookups $enabled,ace.Render.ReusePrimitiveBuffers $enabled" }
  }
  $commands="r.VSync 0,t.MaxFPS 0,r.SetRes 2560x1440w,$settings,ace.PerfScene $scene"
  $arguments=@('-nohmd','-unattended','-nosound','-ACEPerfQuit',"-UserDir=`"$runDirectory/`"","-abslog=`"$runDirectory/run.log`"","-ExecCmds=`"$commands`"")
  $process=Start-Process -FilePath $Executable -ArgumentList $arguments -WindowStyle Hidden -PassThru
  Write-Host "Measuring $scene / $variant (PID $($process.Id))"
  if (!$process.WaitForExit(150000)) {
   $actual=Get-Process -Id $process.Id -ErrorAction SilentlyContinue
   if($actual -and $actual.Path -eq [IO.Path]::GetFullPath($Executable)) { Stop-Process -Id $actual.Id }
   throw "Benchmark timed out. See $runDirectory/run.log"
  }
  if($process.ExitCode -ne 0){throw "Benchmark failed: exit $($process.ExitCode). See $runDirectory/run.log"}
  $report=Join-Path $runDirectory "Saved/Performance/$scene/summary.json"
  if(!(Test-Path $report)){throw "No completed benchmark report: $runDirectory/run.log"}
  Get-Content -LiteralPath $report
 }
}
