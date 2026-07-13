@echo off
setlocal

set "CODEX_PYTHON=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if exist "%CODEX_PYTHON%" goto run_codex_python

where py >nul 2>nul
if not errorlevel 1 goto run_py

where python3 >nul 2>nul
if not errorlevel 1 goto run_python3

where python >nul 2>nul
if errorlevel 1 goto missing_python
python -c "import sys; raise SystemExit(sys.version_info.major != 3)" >nul 2>nul
if not errorlevel 1 goto run_python

:missing_python
echo Python 3 is required to repair the Bistro GLB metadata. 1>&2
exit /b 1

:run_codex_python
"%CODEX_PYTHON%" "%~dp0repair_bistro_orm.py" %*
exit /b %errorlevel%

:run_py
py -3 "%~dp0repair_bistro_orm.py" %*
exit /b %errorlevel%

:run_python3
python3 "%~dp0repair_bistro_orm.py" %*
exit /b %errorlevel%

:run_python
python "%~dp0repair_bistro_orm.py" %*
exit /b %errorlevel%
