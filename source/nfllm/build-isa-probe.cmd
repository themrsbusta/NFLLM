@echo off
setlocal

set "ROOT=%~dp0"
if not defined WATCOM (
  if exist "%ROOT%..\..\..\NF-DOS-6.0\tools\watcom2026\binnt64\wcl386.exe" (
    set "WATCOM=%ROOT%..\..\..\NF-DOS-6.0\tools\watcom2026"
  ) else (
    echo ERROR: Set WATCOM to the root of an Open Watcom installation.
    exit /b 2
  )
)
set "PATH=%WATCOM%\binnt64;%PATH%"
set "INCLUDE=%WATCOM%\h"
set "LIB=%WATCOM%\lib386;%WATCOM%\lib386\dos"

if not exist "%ROOT%build" mkdir "%ROOT%build"
wcl386 -q -bt=dos -l=dos4g -6 -fpi87 -ot "%ROOT%src\isa_probe.cpp" -fe="%ROOT%build\isa_probe.exe" -fm="%ROOT%build\isa_probe.map"
if errorlevel 1 exit /b %errorlevel%

echo Built isolated ISA probe for the QEMU proxy labs.
echo   %ROOT%build\isa_probe.exe
exit /b 0
