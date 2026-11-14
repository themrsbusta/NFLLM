@echo off
setlocal

set "ROOT=%~dp0"
if not defined WATCOM (
    echo Set WATCOM to the root of an Open Watcom installation.
    exit /b 2
)
if not exist "%WATCOM%\binnt64\wcl386.exe" (
    echo Could not find wcl386.exe under "%WATCOM%\binnt64".
    exit /b 2
)
set "PATH=%WATCOM%\binnt64;%PATH%"
set "INCLUDE=%WATCOM%\h"
set "LIB=%WATCOM%\lib386;%WATCOM%\lib386\dos"

if not exist "%ROOT%build" mkdir "%ROOT%build"
wcl386 -q -bt=dos -l=dos4g -3 -fpi87 -ot "%ROOT%src\compat_probe.cpp" -fe="%ROOT%build\compat_probe.exe"
if errorlevel 1 exit /b %errorlevel%
wcl386 -q -bt=dos -l=dos4g -3 -fpi87 -ot "%ROOT%NFLLM.cpp" -fe="%ROOT%build\NFLLM.exe"
if errorlevel 1 exit /b %errorlevel%
echo Built %ROOT%build\compat_probe.exe and %ROOT%build\NFLLM.exe
endlocal
