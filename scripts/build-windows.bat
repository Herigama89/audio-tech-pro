@echo off
rem Builds Audio Tech Pro for Windows (64-bit) with Visual Studio 2022.
rem Needs: Visual Studio 2022 with "Desktop development with C++", CMake, and Git.
rem Run from a "x64 Native Tools Command Prompt for VS 2022":   scripts\build-windows.bat
cd /d "%~dp0\.."
cmake -B build-windows -G "Visual Studio 17 2022" -A x64 || exit /b 1
cmake --build build-windows --config Release --parallel || exit /b 1
build-windows\AudioTechProTests_artefacts\Release\AudioTechProTests.exe || exit /b 1
echo.
echo The app: build-windows\AudioTechPro_artefacts\Release\Audio Tech Pro.exe
