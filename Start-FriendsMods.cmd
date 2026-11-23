@echo off
setlocal
call "%~dp0Start-NativeP2P.cmd" -FriendsOnly %*
set "BetterMultiplayerExit=%errorlevel%"
if %BetterMultiplayerExit% neq 0 if "%~1"=="" pause
exit /b %BetterMultiplayerExit%
