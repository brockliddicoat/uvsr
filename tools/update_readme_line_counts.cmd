@echo off
setlocal

set "CODEX_PYTHON=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if not exist "%CODEX_PYTHON%" goto check_py
"%CODEX_PYTHON%" -c "import sys; raise SystemExit(sys.version_info < (3, 10))" >nul 2>nul
if not errorlevel 1 goto run_codex_python

:check_py
where py >nul 2>nul
if errorlevel 1 goto check_python3
py -3 -c "import sys; raise SystemExit(sys.version_info < (3, 10))" >nul 2>nul
if not errorlevel 1 goto run_py

:check_python3
where python3 >nul 2>nul
if errorlevel 1 goto check_python
python3 -c "import sys; raise SystemExit(sys.version_info < (3, 10))" >nul 2>nul
if not errorlevel 1 goto run_python3

:check_python
where python >nul 2>nul
if errorlevel 1 goto missing_python
python -c "import sys; raise SystemExit(sys.version_info < (3, 10))" >nul 2>nul
if not errorlevel 1 goto run_python

:missing_python
echo Python 3.10 or newer is required to update README line counts. 1>&2
exit /b 1

:run_codex_python
"%CODEX_PYTHON%" "%~dp0update_readme_line_counts.py" %*
exit /b %errorlevel%

:run_py
py -3 "%~dp0update_readme_line_counts.py" %*
exit /b %errorlevel%

:run_python3
python3 "%~dp0update_readme_line_counts.py" %*
exit /b %errorlevel%

:run_python
python "%~dp0update_readme_line_counts.py" %*
exit /b %errorlevel%
