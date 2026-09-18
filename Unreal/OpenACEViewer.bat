@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "ACE_BUILD_ONLY=0"
if /i "%~1"=="--build-only" set "ACE_BUILD_ONLY=1"
if not "%~1"=="" if /i not "%~1"=="--build-only" (
  echo Usage: Launch.bat [--build-only]
  exit /b 1
)

set "UPROJECT=%~dp0ACEViewer.uproject"
if not exist "%UPROJECT%" (
  echo Could not find "%UPROJECT%".
  goto :failed
)

REM Match ACEViewer.uproject. Do not silently load 5.8 binaries in an older editor.
set "UE_ROOT="
for %%V in (5.8) do (
  if exist "C:\Program Files\Epic Games\UE_%%V\Engine\Binaries\Win64\UnrealEditor.exe" (
    set "UE_ROOT=C:\Program Files\Epic Games\UE_%%V"
    goto :found
  )
  if exist "D:\Program Files\Epic Games\UE_%%V\Engine\Binaries\Win64\UnrealEditor.exe" (
    set "UE_ROOT=D:\Program Files\Epic Games\UE_%%V"
    goto :found
  )
  if exist "E:\Program Files\Epic Games\UE_%%V\Engine\Binaries\Win64\UnrealEditor.exe" (
    set "UE_ROOT=E:\Program Files\Epic Games\UE_%%V"
    goto :found
  )
)

echo.
echo Unreal Engine 5.8 not found in the usual Epic Games folders.
echo.
echo Fix: edit OpenACEViewer.bat and set UE_ROOT, e.g.
echo   set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
echo.
echo Or right-click ACEViewer.uproject -^> "Switch Unreal Engine version..." and pick 5.8.
echo.
goto :failed

:found
echo Using: %UE_ROOT%
echo Project: %UPROJECT%
echo Building ACEViewerEditor Win64 Development before launch...
echo Close this project's editor before rebuilding changed C++ code.
echo.

call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" ACEViewerEditor Win64 Development "-Project=%UPROJECT%" -WaitMutex -NoHotReloadFromIDE
if errorlevel 1 (
  echo.
  echo Build failed. Unreal was not opened with outdated binaries.
  goto :failed
)

echo.
echo Build succeeded.
if "%ACE_BUILD_ONLY%"=="1" exit /b 0
echo Opening: %UPROJECT%
start "" "%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe" "%UPROJECT%"
if errorlevel 1 goto :failed
exit /b 0

:failed
if "%ACE_BUILD_ONLY%"=="0" pause
exit /b 1
