@echo off
setlocal
cd /d "%~dp0"
py -3 tools\build_helper.py run --config Debug
if errorlevel 1 pause
