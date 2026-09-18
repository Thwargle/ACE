@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Quest.ps1" -SkipData %*
set "InstallResult=%ERRORLEVEL%"
if not "%InstallResult%"=="0" echo Update did not complete. Read the error above. For a first installation use Install-Quest.cmd.
pause
exit /b %InstallResult%
