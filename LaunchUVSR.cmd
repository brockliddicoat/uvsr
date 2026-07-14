@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\launch_uvsr.ps1" -Experiment main %*
if errorlevel 1 pause
