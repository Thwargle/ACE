@echo off
REM Desktop mode uses the same installed client and saved settings as PC VR.
start "AC:Unreal" "%~dp0ACUnreal.exe" -nohmd %*
