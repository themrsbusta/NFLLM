@echo off
setlocal

set "ROOT=%~dp0"
if not defined WATCOM (
  echo ERROR: set WATCOM to an Open Watcom installation.
  exit /b 2
)
if not exist "%WATCOM%\binnt64\wcl386.exe" (
  echo ERROR: set WATCOM to an Open Watcom installation containing binnt64\wcl386.exe.
  exit /b 2
)
set "PATH=%WATCOM%\binnt64;%WATCOM%\binw;%PATH%"
set "INCLUDE=%WATCOM%\h"
set "LIB=%WATCOM%\lib386;%WATCOM%\lib386\dos"

if not exist "%ROOT%build" mkdir "%ROOT%build"
if not exist "%ROOT%build\obj" mkdir "%ROOT%build\obj"
if not exist "%ROOT%build\obj\plugins" mkdir "%ROOT%build\obj\plugins"
if not exist "%ROOT%build\PLUGINS" mkdir "%ROOT%build\PLUGINS"
if not exist "%ROOT%build\maps" mkdir "%ROOT%build\maps"

call :build_plugin RF_X87 "%ROOT%src\nfllm_reference_kernels.c" "%ROOT%plugins\f32_x87\manifest.nfm"
if errorlevel 1 exit /b %errorlevel%
call :build_plugin Q8_0 "%ROOT%plugins\q8_0\q8_0.c" "%ROOT%plugins\q8_0\manifest.nfm"
if errorlevel 1 exit /b %errorlevel%
call :build_plugin Q4_0 "%ROOT%plugins\q4_0\q4_0.c" "%ROOT%plugins\q4_0\manifest.nfm"
if errorlevel 1 exit /b %errorlevel%
call :build_plugin Q2 "%ROOT%plugins\q2\q2.c" "%ROOT%plugins\q2\manifest.nfm"
if errorlevel 1 exit /b %errorlevel%
call :build_plugin IQ3 "%ROOT%plugins\iq3\iq3.c" "%ROOT%plugins\iq3\manifest.nfm"
if errorlevel 1 exit /b %errorlevel%
call :build_plugin MMX "%ROOT%plugins\mmx\mmx.c" "%ROOT%plugins\mmx\manifest.nfm"
if errorlevel 1 exit /b %errorlevel%
call :build_plugin DNOW "%ROOT%plugins\3dnow\3dnow.c" "%ROOT%plugins\3dnow\manifest.nfm"
if errorlevel 1 exit /b %errorlevel%
call :build_plugin SSE2 "%ROOT%plugins\sse2\sse2.c" "%ROOT%plugins\sse2\manifest.nfm"
if errorlevel 1 exit /b %errorlevel%

wcl386 -q -bt=dos -l=CauseWay -3s -fpi87 -ot ^
  "%ROOT%NFLLM.cpp" ^
  "%ROOT%src\nfllm_logic.c" ^
  "%ROOT%src\nfllm_registry.c" ^
  "%ROOT%src\nfllm_plugin_runtime.c" ^
  "%ROOT%src\nfllm_cpu_caps.c" ^
  -fm="%ROOT%build\maps\NFLLM.map" ^
  -fe="%ROOT%build\NFLLM.EXE"
if errorlevel 1 exit /b %errorlevel%

echo Built NFLLM.EXE and independent CauseWay modules with the .NFD plugin-file extension.
echo Install the PLUGINS directory beside NFLLM.EXE.
exit /b 0

:build_plugin
set "PLUGIN_NAME=%~1"
set "PLUGIN_SOURCE=%~2"
set "PLUGIN_MANIFEST=%~3"
"%WATCOM%\binnt64\wcc386.exe" -q -bt=dos -3s -fpi87 -ot -bd -s -dNFLLM_DYNAMIC_PLUGIN_EXPORT "%PLUGIN_SOURCE%" -fo="%ROOT%build\obj\plugins\%PLUGIN_NAME%.obj"
if errorlevel 1 exit /b %errorlevel%
copy /y "%ROOT%build\obj\plugins\%PLUGIN_NAME%.obj" "%ROOT%build\%PLUGIN_NAME%.obj" >nul
if errorlevel 1 exit /b %errorlevel%
pushd "%ROOT%build"
"%WATCOM%\binnt64\wlink.exe" system cwdlls name %PLUGIN_NAME%.NFD file %PLUGIN_NAME%.obj export _NFLLM_PluginQueryV1
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" exit /b %RC%
move /y "%ROOT%build\%PLUGIN_NAME%.NFD" "%ROOT%build\PLUGINS\%PLUGIN_NAME%.NFD" >nul
if errorlevel 1 exit /b %errorlevel%
copy /y "%PLUGIN_MANIFEST%" "%ROOT%build\PLUGINS\%PLUGIN_NAME%.NFM" >nul
if errorlevel 1 exit /b %errorlevel%
del "%ROOT%build\%PLUGIN_NAME%.obj" >nul
exit /b 0
