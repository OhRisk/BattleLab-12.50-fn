@echo off
rem Builds the Battle Lab 12.50 release (Patcher\dist\BattleLab-12.50.zip). Pass -SkipBuild to reuse Native\bin.
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0BuildPatcher.ps1" %*
if errorlevel 1 (
    echo.
    echo Patcher build failed.
    pause
    exit /b 1
)
