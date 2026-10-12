@echo off
rem Exercise SDL3's missing-device failure path in this process only.
rem Does not disable or alter any Windows audio device.
setlocal
set "SDL_AUDIO_DRIVER=fable-test-unavailable-backend"
cd /d "%~dp0"
if exist "%~dp0Fable2Launcher.exe" (
  start "" "%~dp0Fable2Launcher.exe"
) else (
  start "" "%~dp0fable_2.exe" %*
)
