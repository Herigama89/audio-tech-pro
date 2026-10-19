@echo off
rem Builds Audio Tech Pro for Windows and makes "Audio Tech Pro <version> Setup.exe" next to the riff folder.
rem Needs: Visual Studio 2022 ("Desktop development with C++"), Git, and Inno Setup 6 (jrsoftware.org).
rem Run from a "x64 Native Tools Command Prompt for VS 2022":   scripts\make-windows-installer.bat
cd /d "%~dp0\.."
call scripts\build-windows.bat || exit /b 1
for /f "tokens=3" %%v in ('findstr /b /c:"project(AudioTechPro VERSION" CMakeLists.txt') do set VERSION=%%v
set ISCC=iscc
where iscc >nul 2>nul || set ISCC="%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
%ISCC% /DAppVersion=%VERSION% /DBuildDir=..\..\build-windows\AudioTechPro_artefacts\Release /DOutputDir=..\..\.. installer\windows\AudioTechPro.iss || exit /b 1
echo.
echo Made: ..\Audio Tech Pro %VERSION% Setup.exe
