@echo off
setlocal
set "ROOT=%~dp0..\.."
if not defined WATCOM (
  echo ERROR: set WATCOM to an Open Watcom installation with CauseWay support.
  exit /b 2
)
if not exist "%WATCOM%\binnt64\wcc386.exe" (
  echo ERROR: set WATCOM to an Open Watcom installation with CauseWay support.
  exit /b 2
)
set "PATH=%WATCOM%\binnt64;%WATCOM%\binw;%PATH%"
set "INCLUDE=%WATCOM%\h"
set "LIB=%WATCOM%\lib386;%WATCOM%\lib386\dos"
if not exist "%ROOT%\build" mkdir "%ROOT%\build"
if not exist "%ROOT%\build\PLUGINS" mkdir "%ROOT%\build\PLUGINS"
"%WATCOM%\binnt64\wcc386.exe" -q -bt=dos -3s -fpi87 -ot -bd -s -dNFLLM_DYNAMIC_PLUGIN_EXPORT "%~dp0nfdense.c" -fo="%ROOT%\build\NFDENSE.obj"
if errorlevel 1 exit /b %errorlevel%
pushd "%ROOT%\build"
"%WATCOM%\binnt64\wlink.exe" system cwdlls name NFDENSE.NFD file NFDENSE.obj export _NFLLM_PluginQueryV1
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" exit /b %RC%
move /y "%ROOT%\build\NFDENSE.NFD" "%ROOT%\build\PLUGINS\NFDENSE.NFD" >nul
if errorlevel 1 exit /b %errorlevel%
copy /y "%~dp0manifest.nfm" "%ROOT%\build\PLUGINS\NFDENSE.NFM" >nul
if errorlevel 1 exit /b %errorlevel%
echo Built the independent NFDENSE.NFD package in build\PLUGINS.
echo The host must already use the .NFD loader built by build-dos.cmd.
exit /b 0
