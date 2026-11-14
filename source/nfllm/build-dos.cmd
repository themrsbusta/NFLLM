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
if not exist "%ROOT%build\obj" mkdir "%ROOT%build\obj"
if not exist "%ROOT%build\obj\plugins" mkdir "%ROOT%build\obj\plugins"
if not exist "%ROOT%build\plugins" mkdir "%ROOT%build\plugins"
if not exist "%ROOT%build-modules.cmd" goto no_module_config
call "%ROOT%build-modules.cmd"
if errorlevel 1 exit /b %errorlevel%

wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -l=dos4g -3 -fpi87 -ot "%ROOT%src\compat_probe.cpp" -fe="%ROOT%build\compat_probe.exe"
if errorlevel 1 exit /b %errorlevel%

wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%NFLLM.cpp" -fo="%ROOT%build\obj\NFLLM.obj"
if errorlevel 1 exit /b %errorlevel%
wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%src\nfllm_logic.c" -fo="%ROOT%build\obj\nfllm_logic.obj"
if errorlevel 1 exit /b %errorlevel%
wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%src\nfllm_registry.c" -fo="%ROOT%build\obj\nfllm_registry.obj"
if errorlevel 1 exit /b %errorlevel%
wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%src\nfllm_reference_kernels.c" -fo="%ROOT%build\obj\nfllm_reference_kernels.obj"
if errorlevel 1 exit /b %errorlevel%
wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%plugins\3dnow\3dnow.c" -fo="%ROOT%build\obj\plugins\3dnow.obj"
if errorlevel 1 exit /b %errorlevel%
wlib -q -n -fo "%ROOT%build\plugins\3dnow.lib" +"%ROOT%build\obj\plugins\3dnow.obj"
if errorlevel 1 exit /b %errorlevel%
copy /y "%ROOT%plugins\3dnow\manifest.yaml" "%ROOT%build\plugins\3dnow-manifest.yaml" >nul
if errorlevel 1 exit /b %errorlevel%
wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%plugins\mmx\mmx.c" -fo="%ROOT%build\obj\plugins\mmx.obj"
if errorlevel 1 exit /b %errorlevel%
wlib -q -n -fo "%ROOT%build\plugins\mmx.lib" +"%ROOT%build\obj\plugins\mmx.obj"
if errorlevel 1 exit /b %errorlevel%
copy /y "%ROOT%plugins\mmx\manifest.yaml" "%ROOT%build\plugins\mmx-manifest.yaml" >nul
if errorlevel 1 exit /b %errorlevel%
wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%plugins\sse2\sse2.c" -fo="%ROOT%build\obj\plugins\sse2.obj"
if errorlevel 1 exit /b %errorlevel%
wlib -q -n -fo "%ROOT%build\plugins\sse2.lib" +"%ROOT%build\obj\plugins\sse2.obj"
if errorlevel 1 exit /b %errorlevel%
copy /y "%ROOT%plugins\sse2\manifest.yaml" "%ROOT%build\plugins\sse2-manifest.yaml" >nul
if errorlevel 1 exit /b %errorlevel%
wlib -q -n -fo "%ROOT%build\plugins\f32_x87.lib" +"%ROOT%build\obj\nfllm_reference_kernels.obj"
if errorlevel 1 exit /b %errorlevel%
copy /y "%ROOT%plugins\f32_x87\manifest.yaml" "%ROOT%build\plugins\f32_x87-manifest.yaml" >nul
if errorlevel 1 exit /b %errorlevel%
wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%plugins\q8_0\q8_0.c" -fo="%ROOT%build\obj\plugins\q8_0.obj"
if errorlevel 1 exit /b %errorlevel%
wlib -q -n -fo "%ROOT%build\plugins\q8_0_x87.lib" +"%ROOT%build\obj\plugins\q8_0.obj"
if errorlevel 1 exit /b %errorlevel%
copy /y "%ROOT%plugins\q8_0\manifest.yaml" "%ROOT%build\plugins\q8_0-manifest.yaml" >nul
if errorlevel 1 exit /b %errorlevel%
wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%plugins\q4_0\q4_0.c" -fo="%ROOT%build\obj\plugins\q4_0.obj"
if errorlevel 1 exit /b %errorlevel%
wlib -q -n -fo "%ROOT%build\plugins\q4_0_x87.lib" +"%ROOT%build\obj\plugins\q4_0.obj"
if errorlevel 1 exit /b %errorlevel%
copy /y "%ROOT%plugins\q4_0\manifest.yaml" "%ROOT%build\plugins\q4_0-manifest.yaml" >nul
if errorlevel 1 exit /b %errorlevel%
wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%plugins\q2\q2.c" -fo="%ROOT%build\obj\plugins\q2.obj"
if errorlevel 1 exit /b %errorlevel%
wlib -q -n -fo "%ROOT%build\plugins\q2_x87.lib" +"%ROOT%build\obj\plugins\q2.obj"
if errorlevel 1 exit /b %errorlevel%
copy /y "%ROOT%plugins\q2\manifest.yaml" "%ROOT%build\plugins\q2-manifest.yaml" >nul
if errorlevel 1 exit /b %errorlevel%
wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%plugins\iq3\iq3.c" -fo="%ROOT%build\obj\plugins\iq3.obj"
if errorlevel 1 exit /b %errorlevel%
wlib -q -n -fo "%ROOT%build\plugins\iq3_x87.lib" +"%ROOT%build\obj\plugins\iq3.obj"
if errorlevel 1 exit /b %errorlevel%
copy /y "%ROOT%plugins\iq3\manifest.yaml" "%ROOT%build\plugins\iq3-manifest.yaml" >nul
if errorlevel 1 exit /b %errorlevel%
wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%src\nfllm_builtin_plugins.c" -fo="%ROOT%build\obj\nfllm_builtin_plugins.obj"
if errorlevel 1 exit /b %errorlevel%
wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%tests\nfllm_selftest.c" -fo="%ROOT%build\obj\nfllm_selftest.obj"
if errorlevel 1 exit /b %errorlevel%
wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%tests\nfllm_3dnow_test.c" -fo="%ROOT%build\obj\nfllm_3dnow_test.obj"
if errorlevel 1 exit /b %errorlevel%
wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%tests\nfllm_mmx_test.c" -fo="%ROOT%build\obj\nfllm_mmx_test.obj"
if errorlevel 1 exit /b %errorlevel%
wcl386 -q %NFLLM_CONFIG_FLAGS% -bt=dos -3 -fpi87 -ot -c "%ROOT%tests\nfllm_sse2_test.c" -fo="%ROOT%build\obj\nfllm_sse2_test.obj"
if errorlevel 1 exit /b %errorlevel%

wcl386 -q -bt=dos -l=dos4g -3 -fpi87 -ot ^
  "%ROOT%build\obj\nfllm_3dnow_test.obj" ^
  "%ROOT%build\obj\nfllm_registry.obj" ^
  "%ROOT%build\plugins\3dnow.lib" ^
  "%ROOT%build\plugins\f32_x87.lib" ^
  -fm="%ROOT%build\NF3DNOW.map" ^
  -fe="%ROOT%build\NF3DNOW.exe"
if errorlevel 1 exit /b %errorlevel%

wcl386 -q -bt=dos -l=dos4g -3 -fpi87 -ot ^
  "%ROOT%build\obj\nfllm_mmx_test.obj" ^
  "%ROOT%build\obj\nfllm_registry.obj" ^
  "%ROOT%build\plugins\mmx.lib" ^
  "%ROOT%build\plugins\f32_x87.lib" ^
  -fm="%ROOT%build\NFMMX.map" ^
  -fe="%ROOT%build\NFMMX.exe"
if errorlevel 1 exit /b %errorlevel%

wcl386 -q -bt=dos -l=dos4g -3 -fpi87 -ot ^
  "%ROOT%build\obj\nfllm_sse2_test.obj" ^
  "%ROOT%build\obj\nfllm_registry.obj" ^
  "%ROOT%build\plugins\sse2.lib" ^
  "%ROOT%build\plugins\f32_x87.lib" ^
  -fm="%ROOT%build\NFSSE2.map" ^
  -fe="%ROOT%build\NFSSE2.exe"
if errorlevel 1 exit /b %errorlevel%

wcl386 -q -bt=dos -l=dos4g -3 -fpi87 -ot ^
  "%ROOT%build\obj\NFLLM.obj" ^
  "%ROOT%build\obj\nfllm_logic.obj" ^
  "%ROOT%build\obj\nfllm_registry.obj" ^
  "%ROOT%build\obj\nfllm_builtin_plugins.obj" ^
  %NFLLM_EXTRA_OBJECTS% "%ROOT%build\plugins\3dnow.lib" "%ROOT%build\plugins\mmx.lib" "%ROOT%build\plugins\sse2.lib" "%ROOT%build\plugins\f32_x87.lib" "%ROOT%build\plugins\q8_0_x87.lib" "%ROOT%build\plugins\q4_0_x87.lib" "%ROOT%build\plugins\q2_x87.lib" "%ROOT%build\plugins\iq3_x87.lib" %NFLLM_EXTRA_LIBRARIES% ^
  -fm="%ROOT%build\NFLLM.map" ^
  -fe="%ROOT%build\NFLLM.exe"
if errorlevel 1 exit /b %errorlevel%

wcl386 -q -bt=dos -l=dos4g -3 -fpi87 -ot ^
  "%ROOT%build\obj\nfllm_selftest.obj" ^
  "%ROOT%build\obj\nfllm_logic.obj" ^
  "%ROOT%build\obj\nfllm_registry.obj" ^
  "%ROOT%build\plugins\3dnow.lib" ^
  "%ROOT%build\plugins\mmx.lib" ^
  "%ROOT%build\plugins\sse2.lib" ^
  "%ROOT%build\plugins\f32_x87.lib" ^
  "%ROOT%build\plugins\q8_0_x87.lib" ^
  "%ROOT%build\plugins\q4_0_x87.lib" ^
  "%ROOT%build\plugins\q2_x87.lib" ^
  "%ROOT%build\plugins\iq3_x87.lib" ^
  -fm="%ROOT%build\NFTEST.map" ^
  -fe="%ROOT%build\NFTEST.exe"
if errorlevel 1 exit /b %errorlevel%

copy /y "%WATCOM%\binw\dos4gw.exe" "%ROOT%build\dos4gw.exe" >nul
if errorlevel 1 exit /b %errorlevel%
echo Built modular DOS targets with mmx.lib, 3dnow.lib, sse2.lib, f32_x87.lib, Q8_0, Q4_0, Q2_0, Q2_K, IQ3_XXS and IQ3_S libraries
echo   %ROOT%build\NFLLM.exe
echo   %ROOT%build\NFTEST.exe
echo   %ROOT%build\NF3DNOW.exe (Geode LX/Athlon 3DNow! profile only)
echo   %ROOT%build\NFMMX.exe (MMX profile only)
echo   %ROOT%build\NFSSE2.exe (experimental Atom profile only)
exit /b 0

:no_module_config
echo ERROR: build-modules.cmd is missing.
exit /b 2
