@echo off
setlocal

echo === Hermes Bridge - NSSM Service Registration ===
echo.

REM Check if running as Administrator
net session >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: This script must be run as Administrator.
    echo Right-click and select "Run as administrator"
    exit /b 1
)

REM Check for NSSM
where nssm >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo NSSM not found in PATH. Attempting to install via Chocolatey...
    choco install nssm -y
    if %ERRORLEVEL% neq 0 (
        echo ERROR: Failed to install NSSM. Please install manually:
        echo   choco install nssm -y
        echo Or download from https://nssm.cc/
        exit /b 1
    )
)

set SERVICE_NAME=HermesBridge
set EXE_PATH=%~dp0hermes_bridge.exe
set WORK_DIR=%~dp0

echo Service Name: %SERVICE_NAME%
echo Executable: %EXE_PATH%
echo Working Dir: %WORK_DIR%
echo.

REM Stop existing service if present
echo Stopping existing service (if any)...
net stop %SERVICE_NAME% >nul 2>&1

REM Remove existing service
echo Removing existing service (if any)...
nssm remove %SERVICE_NAME% confirm >nul 2>&1

REM Install service
echo.
echo Installing service...
nssm install %SERVICE_NAME% "%EXE_PATH%" ""
nssm set %SERVICE_NAME% AppDirectory "%WORK_DIR%"
nssm set %SERVICE_NAME% Description "Hermes Bridge - Hermes Agent Windows Resource Bridge"
nssm set %SERVICE_NAME% Start SERVICE_AUTO_START
nssm set %SERVICE_NAME% AppRestartDelay 5000
nssm set %SERVICE_NAME% AppStdout "%WORK_DIR%logs\stdout.txt"
nssm set %SERVICE_NAME% AppStderr "%WORK_DIR%logs\stderr.txt"

REM Create logs directory
if not exist "%WORK_DIR%logs" mkdir "%WORK_DIR%logs"

REM Start service
echo.
echo Starting service...
net start %SERVICE_NAME%

echo.
echo === Service Registration Complete ===
echo.
echo Check status with: sc query %SERVICE_NAME%
echo View logs at: %WORK_DIR%logs\
echo.
echo To uninstall:
echo   net stop %SERVICE_NAME%
echo   nssm remove %SERVICE_NAME% confirm
