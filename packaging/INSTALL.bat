@echo off
title VERDIGRIS installer
net session >nul 2>&1
if %errorlevel% neq 0 (
  echo Windows will ask for permission to install VERDIGRIS. Click Yes.
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)
set "SRC=%~dp0VERDIGRIS.vst3"
set "DEST=%CommonProgramFiles%\VST3"
if not exist "%SRC%" (
  echo Could not find VERDIGRIS.vst3 next to this file. Unzip the whole download first, then run this again.
  pause
  exit /b 1
)
if not exist "%DEST%" mkdir "%DEST%"
if exist "%DEST%\VERDIGRIS.vst3" rmdir /s /q "%DEST%\VERDIGRIS.vst3"
xcopy "%SRC%" "%DEST%\VERDIGRIS.vst3\" /E /I /Y /Q >nul
if errorlevel 1 (
  echo Install failed. Close FL Studio and try again.
  pause
  exit /b 1
)
echo.
echo VERDIGRIS is installed in:  %DEST%\VERDIGRIS.vst3
echo.
echo Now open FL Studio:
echo   Options ^> Manage plugins ^> Find installed plugins
echo   (tick "Verify plugins" if you see it, then click the scan button)
echo Then in the plugin picker look for VERDIGRIS under Installed ^> Effects (it may also appear under "VST3").
echo.
pause
