@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Quest.ps1" -RepairData %*
set "InstallResult=%ERRORLEVEL%"
if not "%InstallResult%"=="0" echo Data repair did not complete. Read the error above.
pause
exit /b %InstallResult%
