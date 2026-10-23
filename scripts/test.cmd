@echo off
rem SPDX-License-Identifier: Apache-2.0
rem Copyright 2026 HarriethWiKk
rem
rem Run ctest suites with MSVC and Qt environment.
setlocal

if not defined QTDIR set "QTDIR=D:\tools\Qt\6.8.3\msvc2022_64"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :missing_vswhere

set "VSPATH="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto :missing_vswhere

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :missing_vcvars

set "PATH=%QTDIR%\bin;%PATH%"

ctest --test-dir "%~dp0..\build" --output-on-failure %*
exit /b %errorlevel%

:missing_vswhere
echo error: vswhere.exe not found. 1>&2
exit /b 1

:missing_vcvars
echo error: vcvars64.bat failed. 1>&2
exit /b 1
