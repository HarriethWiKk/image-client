@echo off
rem SPDX-License-Identifier: Apache-2.0
rem Copyright 2026 HarriethWiKk
setlocal
if not defined QTDIR set "QTDIR=D:\tools\Qt\6.8.3\msvc2022_64"
set "PATH=%QTDIR%\bin;%PATH%"
start "" "%~dp0..\build\ImageClient.exe" %*
