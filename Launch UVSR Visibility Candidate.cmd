@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\launch_uvsr.ps1" -Experiment "adaptive AO-GI visibility candidate" %*
if errorlevel 1 pause
