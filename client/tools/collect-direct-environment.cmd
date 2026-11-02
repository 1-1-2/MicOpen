@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0collect-direct-environment.ps1" -Reveal
if errorlevel 1 (
    echo Environment collection failed. Please send the error shown above.
    pause
    exit /b 1
)
endlocal
