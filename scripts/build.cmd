@echo off
rem SPDX-License-Identifier: Apache-2.0
rem Copyright 2026 HarriethWiKk
rem
rem Configure + build with MSVC and Qt. Nothing here is machine-specific: the
rem compiler is located with vswhere, and everything else comes from the
rem environment.
rem
rem   QTDIR      (required) Qt install prefix, e.g. C:\Qt\6.8.3\msvc2022_64
rem   CMAKE_DIR  (optional) directory holding cmake.exe and ninja.exe, for when
rem              they are installed somewhere that is not on PATH
setlocal

if not "%CMAKE_DIR%"=="" set "PATH=%CMAKE_DIR%;%PATH%"

if not defined QTDIR goto :missing_qtdir

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :missing_vswhere

set "VSPATH="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto :missing_vswhere

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :missing_vcvars

where cmake >nul 2>&1
if errorlevel 1 goto :missing_cmake
where ninja >nul 2>&1
if errorlevel 1 goto :missing_cmake

set "PATH=%QTDIR%\bin;%PATH%"

cmake -S "%~dp0.." -B "%~dp0..\build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QTDIR%"
if errorlevel 1 exit /b 1
cmake --build "%~dp0..\build" %*
exit /b %errorlevel%

:missing_qtdir
echo error: QTDIR is not set. 1>&2
echo        set QTDIR to your Qt prefix, e.g. C:\Qt\6.8.3\msvc2022_64 1>&2
exit /b 1

:missing_vswhere
echo error: vswhere.exe not found under "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer". 1>&2
echo        Install Visual Studio with the "Desktop development with C++" workload. 1>&2
exit /b 1

:missing_vcvars
echo error: "%VSPATH%" has no working VC\Auxiliary\Build\vcvars64.bat. 1>&2
echo        Add the "Desktop development with C++" workload to that installation. 1>&2
exit /b 1

:missing_cmake
echo error: cmake.exe or ninja.exe not found on PATH. 1>&2
echo        Either put them on PATH or set CMAKE_DIR to the directory holding them. 1>&2
exit /b 1
