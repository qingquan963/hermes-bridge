@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\lobster\hermes_bridge\install_service.ps1"
echo Exit code: %ERRORLEVEL%
pause
