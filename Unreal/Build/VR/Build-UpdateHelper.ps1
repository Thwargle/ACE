#requires -Version 7.0
param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'Visual C++ build tools are required for the Windows updater.'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$output=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$object=Join-Path ([IO.Path]::GetTempPath()) ('ACUpdater-'+[guid]::NewGuid().ToString('N')+'.obj')
try {
 & cl.exe /nologo /EHsc /std:c++17 /O2 /MT /W4 /WX (Join-Path $PSScriptRoot 'Update-Client.cpp') "/Fo$object" "/Fe$output/Update-Client.exe" /link /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT
 if($LASTEXITCODE -ne 0){throw 'Native update helper compilation failed.'}
} finally {if(Test-Path -LiteralPath $object){Remove-Item -LiteralPath $object}}
