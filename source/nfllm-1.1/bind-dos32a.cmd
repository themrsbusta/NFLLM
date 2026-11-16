@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "BUILD=%ROOT%build"
set "WORK=%BUILD%\dos32a-bind.tempd"
set "BIN=%ROOT%third_party\dos32a\bin"
set "DOSBOX_X=%DOSBOX_X%"
if not defined DOSBOX_X set "DOSBOX_X=C:\DOSBox-X\dosbox-x.exe"
set "MODE=%~1"
set "LOG=%BUILD%\logs\dos32a-bind\%MODE%"

if /i "%MODE%"=="all" (
  set "TARGETS=NFLLM NFTEST NF3DNOW NFMMX NFSSE2 COMPAT"
  set "OUTPUTS=NFLLM NFTEST NF3DNOW NFMMX NFSSE2 COMPAT"
) else if /i "%MODE%"=="isa" (
  set "TARGETS=ISAPROB"
  set "OUTPUTS=ISAPROB"
) else (
  echo Usage: bind-dos32a.cmd all^|isa
  exit /b 2
)

if not exist "%DOSBOX_X%" (
  echo ERROR: DOSBox-X not found: "%DOSBOX_X%"
  exit /b 3
)
if not exist "%BIN%\DOS32A.EXE" goto missing_vendor_tool
if not exist "%BIN%\SB.EXE" goto missing_vendor_tool
if not exist "%BIN%\SVER.EXE" goto missing_vendor_tool
if not exist "%WORK%" mkdir "%WORK%"
if not exist "%LOG%" mkdir "%LOG%"
if not exist "%WORK%\DOS32A\BINW" mkdir "%WORK%\DOS32A\BINW"

for %%T in (%TARGETS%) do if not exist "%WORK%\%%T.RAW" (
  echo ERROR: Missing linker output "%WORK%\%%T.RAW".
  exit /b 4
)

del /q "%WORK%\*.EXE" "%WORK%\*.LE" "%WORK%\*.TXT" "%WORK%\*.BAT" >nul 2>nul
copy /y "%BIN%\DOS32A.EXE" "%WORK%\DOS32A\BINW\DOS32A.EXE" >nul
if errorlevel 1 exit /b %errorlevel%
copy /y "%BIN%\SB.EXE" "%WORK%\SB.EXE" >nul
if errorlevel 1 exit /b %errorlevel%
copy /y "%BIN%\SVER.EXE" "%WORK%\SVER.EXE" >nul
if errorlevel 1 exit /b %errorlevel%

>"%WORK%\RUNBIND.BAT" echo @echo off
>>"%WORK%\RUNBIND.BAT" echo SET DOS32A=C:\DOS32A
set /a INDEX=0
>"%LOG%\targets.txt" echo index=target
for %%T in (%TARGETS%) do call :append_target %%T
>>"%WORK%\RUNBIND.BAT" echo ECHO PASS ^> RESULT.TXT
>>"%WORK%\RUNBIND.BAT" echo GOTO DONE
>>"%WORK%\RUNBIND.BAT" echo :FAIL
>>"%WORK%\RUNBIND.BAT" echo ECHO FAIL ^> RESULT.TXT
>>"%WORK%\RUNBIND.BAT" echo :DONE
>>"%WORK%\RUNBIND.BAT" echo EXIT

where python.exe >nul 2>nul
if errorlevel 1 (
  echo ERROR: Python is required to register DOSBox-X in the shared process supervisor.
  exit /b 10
)
python.exe "%ROOT%scripts\run_dosbox_managed.py" ^
  --work-dir "%WORK%" --batch RUNBIND.BAT ^
  --task-id nfllm11-bind-%RANDOM%-%RANDOM% --timeout 180
set "DOSBOX_RC=%ERRORLEVEL%"
if not "%DOSBOX_RC%"=="0" (
  copy /y "%WORK%\*.TXT" "%LOG%\" >nul 2>nul
  echo ERROR: Managed DOSBox-X returned %DOSBOX_RC%. See "%LOG%" and "%WORK%".
  exit /b %DOSBOX_RC%
)

copy /y "%WORK%\*.TXT" "%LOG%\" >nul 2>nul
if not exist "%WORK%\RESULT.TXT" (
  echo ERROR: DOSBox-X exited with code %DOSBOX_RC% without a bind result. See "%LOG%".
  exit /b 5
)
findstr /x /c:"PASS" "%WORK%\RESULT.TXT" >nul
if errorlevel 1 (
  echo ERROR: DOS/32A bind or SVER validation failed. See "%LOG%" and "%WORK%".
  exit /b 6
)

set /a INDEX=0
for %%T in (%TARGETS%) do call :verify_target %%T || exit /b 7

if /i "%MODE%"=="all" (
  call :copy_output NFLLM NFLLM.exe || exit /b 8
  call :copy_output NFTEST NFTEST.exe || exit /b 8
  call :copy_output NF3DNOW NF3DNOW.exe || exit /b 8
  call :copy_output NFMMX NFMMX.exe || exit /b 8
  call :copy_output NFSSE2 NFSSE2.exe || exit /b 8
  call :copy_output COMPAT compat_probe.exe || exit /b 8
) else (
  call :copy_output ISAPROB isa_probe.exe || exit /b 8
)

del /q "%WORK%\*.RAW" "%WORK%\*.EXE" "%WORK%\*.LE" "%WORK%\*.BAT" "%WORK%\*.TXT" >nul 2>nul
del /q "%WORK%\DOS32A\BINW\DOS32A.EXE" >nul 2>nul
rmdir "%WORK%\DOS32A\BINW" >nul 2>nul
rmdir "%WORK%\DOS32A" >nul 2>nul
echo DOS/32A binding and SVER checks passed. Logs: "%LOG%"
exit /b 0

:append_target
set /a INDEX+=1
>>"%LOG%\targets.txt" echo %INDEX%=%~1
>>"%WORK%\RUNBIND.BAT" echo SB.EXE /B /BN%~1.EXE %~1.RAW ^> B%INDEX%.TXT
>>"%WORK%\RUNBIND.BAT" echo IF ERRORLEVEL 1 GOTO FAIL
>>"%WORK%\RUNBIND.BAT" echo SVER.EXE %~1.EXE ^> V%INDEX%.TXT
>>"%WORK%\RUNBIND.BAT" echo IF ERRORLEVEL 1 GOTO FAIL
exit /b 0

:verify_target
set /a INDEX+=1
if not exist "%WORK%\%~1.EXE" (
  echo ERROR: Bound output missing: %~1.EXE
  exit /b 1
)
findstr /i /c:"Binding file:" "%WORK%\B%INDEX%.TXT" >nul
if errorlevel 1 (
  echo ERROR: SB did not confirm binding for %~1. See "%LOG%\B%INDEX%.TXT".
  exit /b 1
)
findstr /i /c:"Stub file used:" "%WORK%\B%INDEX%.TXT" >nul
if errorlevel 1 exit /b 1
findstr /i /c:"DOS/32 Advanced DOS Extender" "%WORK%\V%INDEX%.TXT" >nul
if errorlevel 1 exit /b 1
findstr /i /c:"Version:" "%WORK%\V%INDEX%.TXT" >nul
if errorlevel 1 exit /b 1
exit /b 0

:copy_output
copy /b /y "%WORK%\%~1.EXE" "%BUILD%\%~2" >nul
if errorlevel 1 exit /b %errorlevel%
exit /b 0

:missing_vendor_tool
echo ERROR: DOS/32A DOS32A.EXE, SB.EXE, or SVER.EXE is missing under "%BIN%".
exit /b 9
