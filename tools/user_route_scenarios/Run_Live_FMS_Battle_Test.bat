@echo off
setlocal

set SCRIPT=%~dp0Invoke-LiveFmsBattleTest.ps1

echo XVatsim Live FMS Battle Test
echo.
echo This creates a disposable live probe scenario, downloads current VATSIM
echo controller/source data, runs the regression harness, and prints a summary.
echo.

set /p FMS_PATH=Paste full .fms path:
if "%FMS_PATH%"=="" (
  echo No FMS path entered.
  echo.
  pause
  exit /b 1
)

set /p CALLSIGN=Callsign [TEST123]:
if "%CALLSIGN%"=="" set CALLSIGN=TEST123

echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" -FmsPath "%FMS_PATH%" -Callsign "%CALLSIGN%"

echo.
pause
