@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "UPROJECT=%cd%\ACEViewer.uproject"
set "UE_ROOT="

for %%V in (5.8 5.7 5.6 5.5) do (
  if exist "C:\Program Files\Epic Games\UE_%%V\Engine\Build\BatchFiles\Build.bat" (
    set "UE_ROOT=C:\Program Files\Epic Games\UE_%%V"
    goto :found
  )
  if exist "D:\Program Files\Epic Games\UE_%%V\Engine\Build\BatchFiles\Build.bat" (
    set "UE_ROOT=D:\Program Files\Epic Games\UE_%%V"
    goto :found
  )
)

echo Unreal Engine 5.8 not found. Set UE_ROOT in GenerateProjectFiles.bat, e.g.
echo   set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
pause
exit /b 1

:found
echo Generating VS project files with %UE_ROOT% ...
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="%UPROJECT%" -game -rocket -progress
if errorlevel 1 (
  echo Generation failed.
  pause
  exit /b 1
)
echo Done. Open ACEViewer.sln or run OpenACEViewer.bat
pause
endlocal
