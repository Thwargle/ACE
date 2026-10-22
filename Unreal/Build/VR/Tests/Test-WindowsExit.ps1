#requires -Version 7.0
$ErrorActionPreference = 'Stop'
$unreal = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$output = Join-Path $unreal 'Saved/Automation/WindowsExitGuard'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ tools are required.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$source = Join-Path $PSScriptRoot 'WindowsExitGuard.cpp'
$include = Join-Path $unreal 'Source/ACUnreal'
$exe = Join-Path $output 'WindowsExitGuard.exe'
$build = Join-Path $output 'build.cmd'
@"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /WX /I"$include" "$source" /Fo"$output/WindowsExitGuard.obj" /Fe"$exe"
"@ | Set-Content -LiteralPath $build
& $build
if ($LASTEXITCODE) { throw "Guard fixture compilation failed: $LASTEXITCODE" }

function Start-Fixture([string]$Mode) {
    Start-Process -FilePath $exe -ArgumentList $Mode -WindowStyle Hidden -PassThru
}
function Check-Fixture($Process, [string]$Mode, [int]$Expected) {
    try {
        if (!$Process.WaitForExit(5000)) {
            # Only the test child created by this script, never a game client.
            $Process.Kill()
            $Process.WaitForExit()
            throw "$Mode did not exit within five seconds"
        }
        if ($Process.ExitCode -ne $Expected) { throw "$Mode returned $($Process.ExitCode), expected $Expected" }
        Write-Host "PASS: $Mode (exit $Expected)"
    } finally { $Process.Dispose() }
}
foreach ($mode in @('active', 'normal', 'stuck', 'repeated', 'locked')) {
    $expected = if ($mode -in @('active', 'normal')) { 0 } else { 124 }
    Check-Fixture (Start-Fixture $mode) $mode $expected
}
# A shutdown timeout must never terminate a second, healthy instance.
$healthy = Start-Fixture 'active'
try { Check-Fixture (Start-Fixture 'stuck') 'stuck alongside healthy instance' 124 }
finally { Check-Fixture $healthy 'independent healthy instance' 0 }
