@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"
where py >nul 2>nul
if errorlevel 1 goto python
py -3 "%~dp0Genshin-Bilibili-iOS\one_click.py" --mode rebuild
set "RUN_CODE=%ERRORLEVEL%"
goto done
:python
where python >nul 2>nul
if errorlevel 1 goto missing
python "%~dp0Genshin-Bilibili-iOS\one_click.py" --mode rebuild
set "RUN_CODE=%ERRORLEVEL%"
goto done
:missing
echo Python 3.11+ is required. Install Python and run this shortcut again.
set "RUN_CODE=1"
:done
echo.
pause
exit /b %RUN_CODE%
