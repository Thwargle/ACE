#requires -Version 7.0
param([string]$Version = '19', [ValidateSet('Packaged','Mobile')][string[]]$Renderers=@('Packaged','Mobile'))
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$unreal=Join-Path $root 'Unreal'
$tests='ACE.VR.RenderingAndReplication+ACE.RetailParity.Weather+ACE.Rendering.SceneAudit'
foreach($renderer in $Renderers) {
 $report=Join-Path $unreal "Saved/Automation/VR$Version-$renderer-RenderRegression"
 $log=Join-Path $root "Quest/Logs/VR$Version-$renderer-RenderRegression.log"
 $resultPath=Join-Path $report 'index.json'
 if(Test-Path -LiteralPath $resultPath) {
  throw "Report already exists: $resultPath. Use a new Version label or select the remaining Renderers."
 }
 $arguments=@('-unattended','-nosound','-nop4','-nohmd',
  '-TestExit="Automation Test Queue Empty"',"-ReportExportPath=`"$report`"","-abslog=`"$log`"")
 if($renderer -eq 'Packaged') {
  $exe=Join-Path $unreal 'Saved/VRWindowsArchive/Windows/ACUnreal/Binaries/Win64/ACUnreal.exe'
  $arguments+=@('-windowed','-ResX=1280','-ResY=720',"-UserDir=$unreal/Saved/VR$Version-RenderRegressionUser","-ExecCmds=`"Automation RunTests $tests`"")
 } else {
  $exe='C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
  $arguments=@("$unreal/ACUnreal.uproject")+$arguments+@('-game','-FeatureLevelES31',
   '-ini:Engine:[/Script/Engine.RendererSettings]:r.MobileHDR=False',"-ExecCmds=`"r.Mobile.AntiAliasing 3,Automation RunTests $tests`"")
 }
 $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
 $process.WaitForExit()
 if($process.ExitCode -ne 0){throw "$renderer regression failed; see $log"}
 $result=Get-Content -LiteralPath (Join-Path $report 'index.json') -Raw | ConvertFrom-Json
 if($result.tests.Count -ne 3 -or @($result.tests | Where-Object { $_.state -ne 'Success' -or $_.errors -ne 0 }).Count){throw "Incomplete $renderer regression report"}
 Write-Host "$renderer rendering: all 3 regression tests passed."
}
