@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0LaunchBattleLab.ps1" %*
if errorlevel 1 (
    echo.
    echo Launch failed. The error above explains what needs attention.
    pause
    exit /b 1
)
