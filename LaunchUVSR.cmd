@echo off
setlocal DisableDelayedExpansion

if /i "%~1"=="-Menu" goto menu

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\launch_uvsr.ps1" %*
set "UVSR_LAUNCH_RESULT=%errorlevel%"
if not "%UVSR_LAUNCH_RESULT%"=="0" pause
exit /b %UVSR_LAUNCH_RESULT%

:menu
set "UVSR_EXECUTABLE=%~dp0build\bin\uvsr.exe"

:menu_prompt
echo.
echo UVSR is ready.
echo.
echo   1 - Launch uvsr.exe
echo   2 - Open the uvsr.exe file location
echo.
choice.exe /c 12 /n /m "Type 1 or 2: "
set "UVSR_MENU_CHOICE=%errorlevel%"
if "%UVSR_MENU_CHOICE%"=="1" goto launch
if "%UVSR_MENU_CHOICE%"=="2" goto open_location
echo.
echo FAILURE: Launcher input is unavailable.
exit /b 1

:launch
if not exist "%UVSR_EXECUTABLE%" goto executable_missing
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\launch_uvsr.ps1" 1>nul 2>&1
set "UVSR_ACTION_RESULT=%errorlevel%"
if not "%UVSR_ACTION_RESULT%"=="0" goto launch_failure
echo.
echo SUCCESS: UVSR launch request accepted.
goto menu_prompt

:launch_failure
echo.
echo FAILURE: Windows could not launch uvsr.exe.
goto menu_prompt

:open_location
if not exist "%UVSR_EXECUTABLE%" goto executable_missing
if not exist "%SystemRoot%\explorer.exe" goto open_location_failure
start "" "%SystemRoot%\explorer.exe" /select,"%UVSR_EXECUTABLE%" 1>nul 2>&1
set "UVSR_ACTION_RESULT=%errorlevel%"
if not "%UVSR_ACTION_RESULT%"=="0" goto open_location_failure
echo.
echo SUCCESS: File-location request accepted.
goto menu_prompt

:open_location_failure
echo.
echo FAILURE: Windows could not open the uvsr.exe file location.
goto menu_prompt

:executable_missing
echo.
echo FAILURE: uvsr.exe was not found at "%UVSR_EXECUTABLE%".
goto menu_prompt
