@echo off
rem Ecu Fps: cift tikla derle + test et (Release). Ayrinti: scripts\dev.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\dev.ps1" build %*
if errorlevel 1 (echo. & echo DERLEME BASARISIZ & pause & exit /b 1)
echo. & echo Derleme tamam: build\x64-release\src\app\EcuFps.exe
pause
