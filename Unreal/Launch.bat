@echo off
REM Build the current editor client before opening it. Forward --build-only too.
call "%~dp0OpenACUnreal.bat" %*
exit /b %errorlevel%
