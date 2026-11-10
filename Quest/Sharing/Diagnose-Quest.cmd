@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Diagnose-Quest.ps1" %*
set "DiagnosticResult=%ERRORLEVEL%"
if not "%DiagnosticResult%"=="0" echo Diagnostic collection did not complete. Read the error above.
pause
exit /b %DiagnosticResult%
