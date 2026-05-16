@echo off
setlocal
set SCRIPT=%~dp0New-FmsHarnessScenario.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" -OpenTemplate
echo.
pause
