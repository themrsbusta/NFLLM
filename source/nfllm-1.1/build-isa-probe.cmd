@echo off
setlocal

set "ROOT=%~dp0"
if not defined WATCOM set "WATCOM=%ROOT%..\NF-DOS-6.0\tools\watcom2026"
if not exist "%WATCOM%\binnt64\wcl386.exe" (
  echo ERROR: set WATCOM to an Open Watcom installation containing binnt64\wcl386.exe.
  exit /b 2
)
set "PATH=%WATCOM%\binnt64;%PATH%"
set "INCLUDE=%WATCOM%\h"
set "LIB=%WATCOM%\lib386;%WATCOM%\lib386\dos"

if not exist "%ROOT%build" mkdir "%ROOT%build"
if not exist "%ROOT%build\dos32a-bind.tempd" mkdir "%ROOT%build\dos32a-bind.tempd"
wcl386 -q -bt=dos -l=dos4g -6 -fpi87 -ot "%ROOT%src\isa_probe.cpp" -fe="%ROOT%build\dos32a-bind.tempd\ISAPROB.RAW" -fm="%ROOT%build\isa_probe.map"
if errorlevel 1 exit /b %errorlevel%

call "%ROOT%bind-dos32a.cmd" isa
if errorlevel 1 exit /b %errorlevel%

echo Built isolated ISA probe with DOS/32A bound into the executable.
echo   %ROOT%build\isa_probe.exe
exit /b 0
