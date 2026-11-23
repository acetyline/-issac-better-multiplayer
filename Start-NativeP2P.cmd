@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0transport\Start-NativeP2P.ps1" %*
set "BetterMultiplayerExit=%errorlevel%"
if not "%BetterMultiplayerExit%"=="0" if "%~1"=="" pause
exit /b %BetterMultiplayerExit%
