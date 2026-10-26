@echo off
chcp 65001 >nul
title SAF8100 Modbus Tool
cd /d "%~dp0"

where py >nul 2>nul
if not errorlevel 1 (
    py server.py
    goto :done
)

where python >nul 2>nul
if not errorlevel 1 (
    python server.py
    goto :done
)

echo.
echo [!] Python not found.
echo     Install Python 3 or run this folder on a machine that has it.
echo.
pause

:done
