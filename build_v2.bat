@echo off
cd /d C:\lobster\hermes_bridge\Release
if exist CMakeFiles rmdir /s /q CMakeFiles
if exist CMakeCache.txt del /q CMakeCache.txt
cmake .. -G "Ninja" -A x64