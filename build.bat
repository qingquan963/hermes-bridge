@echo off
setlocal enabledelayedexpansion

echo === Hermes Bridge Build Script ===
echo.

REM Find vcvarsall.bat
set VCVARS=""
if defined VS170COMNTOOLS (
    set candidate=%VS170COMNTOOLS%..\..\VC\Auxiliary\Build\vcvarsall.bat
    if exist "!candidate!" set VCVARS=!candidate!
)

REM Search in common VS2022 installation locations
if not defined VCVARS (
    for /r "C:\Program Files\Microsoft Visual Studio\2022" %%D in (vcvarsall.bat) do (
        if exist "%%D" set VCVARS=%%D
        goto :found_vs
    )
)
:found_vs

if not defined VCVARS (
    for /r "C:\Program Files (x86)\Microsoft Visual Studio\2022" %%D in (vcvarsall.bat) do (
        if exist "%%D" set VCVARS=%%D
        goto :found_vs2
    )
)
:found_vs2

if not defined VCVARS (
    echo ERROR: Visual Studio 2022 not found.
    echo Please install "Visual Studio Build Tools 2022" with:
    echo   - MSVC v143 - VS 2022 C++ x64/x86 build tools
    echo   - Windows 11 SDK
    exit /b 1
)

echo Using Visual Studio at: !VCVARS!

REM Find vcpkg toolchain
set VCPKG_CMAKE=""
where vcpkg >nul 2>&1
if %ERRORLEVEL% equ 0 (
    for /f "delims=" %%v in ('where vcpkg') do set VCPKG_DIR=%%~dpv
    if exist "!VCPKG_DIR!scripts\buildsystems\vcpkg.cmake" (
        set VCPKG_CMAKE=-DCMAKE_TOOLCHAIN_FILE=!VCPKG_DIR!scripts\buildsystems\vcpkg.cmake
        echo Found vcpkg at: !VCPKG_DIR!
    )
)

if not defined VCPKG_CMAKE (
    REM Try common vcpkg locations
    for /d %%D in (C:\vcpkg D:\vcpkg E:\vcpkg) do (
        if exist "%%D\scripts\buildsystems\vcpkg.cmake" (
            set VCPKG_CMAKE=-DCMAKE_TOOLCHAIN_FILE=%%D\scripts\buildsystems\vcpkg.cmake
            echo Found vcpkg at: %%D
        )
    )
)

if not defined VCPKG_CMAKE (
    echo WARNING: vcpkg not found. Build may fail without dependencies.
    echo Install with: git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
    echo Then run: C:\vcpkg\bootstrap-vcpkg.bat
    echo And install: vcpkg install curl:x64-windows-static spdlog:x64-windows nlohmann-json:x64-windows
    echo.
)

REM Check for cmake
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: cmake not found. Install CMake from https://cmake.org/download/
    exit /b 1
)

REM Create build directory
cd /d "%~dp0"
if not exist build mkdir build
cd build

REM Configure
echo.
echo === Running CMake Configure ===
call "!VCVARS!" x64 >nul 2>&1
cmake .. -G "Visual Studio 17 2022" -A x64 !VCPKG_CMAKE!
if %ERRORLEVEL% neq 0 (
    echo CMake configure failed!
    exit /b 1
)

REM Build
echo.
echo === Building Hermes Bridge ===
cmake --build . --config Release -j%NUMBER_OF_PROCESSORS%
if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b 1
)

echo.
echo === Build successful ===
echo Output: %~dp0hermes_bridge.exe
echo.
echo To install as Windows Service (run as Admin):
echo   install_service.bat
echo.
echo Done.
