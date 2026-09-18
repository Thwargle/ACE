@echo off
setlocal

REM Full Dereth world build: DAT export, content import, tiled landscape, landblock T3D.
REM Requires UE 5.8 and portal DAT at C:/Turbine/Asheron's Call (or set DATPATH=).

set UE_ROOT=C:\Program Files\Epic Games\UE_5.8
set PROJECT=%~dp0ACEViewer.uproject

if not exist "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" (
  echo UE 5.8 not found at %UE_ROOT%
  exit /b 1
)

echo Building ACEViewerEditor if needed...
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" ACEViewerEditor Win64 Development -Project="%PROJECT%" -WaitMutex

echo Running ACEWorldBakeBuildWorld commandlet...
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -run=ACEWorldBakeBuildWorld -DATPATH="C:/Turbine/Asheron's Call" -unattended -nopause -nosplash -log

endlocal
