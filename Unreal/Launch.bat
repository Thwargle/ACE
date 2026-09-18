@echo off
REM Build the current editor client before opening it. Forward --build-only too.
call "%~dp0OpenACEViewer.bat" %*
exit /b %errorlevel%
