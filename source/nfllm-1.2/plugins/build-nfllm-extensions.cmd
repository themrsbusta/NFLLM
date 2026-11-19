@echo off
setlocal
set "ROOT=%~dp0.."
if not defined WATCOM (
  echo ERROR: set WATCOM to Open Watcom 2.0 with CauseWay support.
  exit /b 2
)
if not exist "%WATCOM%\binnt64\wcc386.exe" (
  echo ERROR: set WATCOM to Open Watcom 2.0 with CauseWay support.
  exit /b 2
)
set "PATH=%WATCOM%\binnt64;%WATCOM%\binw;%PATH%"
set "INCLUDE=%WATCOM%\h"
set "LIB=%WATCOM%\lib386;%WATCOM%\lib386\dos"
if not exist "%ROOT%\build\obj\nfdplugins" mkdir "%ROOT%\build\obj\nfdplugins"
if not exist "%ROOT%\build\PLUGINS" mkdir "%ROOT%\build\PLUGINS"

call :plugin NFMODEL "%~dp0nf_modelio\nf_modelio_tflite_adapter.c" "%~dp0nf_modelio\manifest.nfm"
if errorlevel 1 exit /b %errorlevel%
call :plugin NFGRAPH "%~dp0nf_graphvm\nf_graphvm_runtime.c" "%~dp0nf_graphvm\manifest.nfm"
if errorlevel 1 exit /b %errorlevel%
call :plugin NFINT8OP "%~dp0nf_int8ops\nf_int8ops_kernels.c" "%~dp0nf_int8ops\manifest.nfm"
if errorlevel 1 exit /b %errorlevel%

echo Built independently installable NFMODEL.NFD, NFGRAPH.NFD and NFINT8OP.NFD packages.
echo This script builds plugin modules only. Build the host once with build-dos.cmd for .NFD support.
exit /b 0

:plugin
set "PLUGIN_NAME=%~1"
set "SOURCE=%~2"
set "MANIFEST=%~3"
"%WATCOM%\binnt64\wcc386.exe" -q -bt=dos -3s -fpi87 -ot -bd -s -dNFLLM_DYNAMIC_PLUGIN_EXPORT "%SOURCE%" -fo="%ROOT%\build\obj\nfdplugins\%PLUGIN_NAME%.obj"
if errorlevel 1 exit /b %errorlevel%
copy /y "%ROOT%\build\obj\nfdplugins\%PLUGIN_NAME%.obj" "%ROOT%\build\%PLUGIN_NAME%.obj" >nul
if errorlevel 1 exit /b %errorlevel%
pushd "%ROOT%\build"
"%WATCOM%\binnt64\wlink.exe" system cwdlls name %PLUGIN_NAME%.NFD file %PLUGIN_NAME%.obj export _NFLLM_PluginQueryV1
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" exit /b %RC%
move /y "%ROOT%\build\%PLUGIN_NAME%.NFD" "%ROOT%\build\PLUGINS\%PLUGIN_NAME%.NFD" >nul
if errorlevel 1 exit /b %errorlevel%
copy /y "%MANIFEST%" "%ROOT%\build\PLUGINS\%PLUGIN_NAME%.NFM" >nul
if errorlevel 1 exit /b %errorlevel%
del "%ROOT%\build\%PLUGIN_NAME%.obj" >nul
exit /b 0
