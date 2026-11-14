@ECHO OFF
call \kernel\commands\setsys.bat
call \Kernel\Commands\sysver.bat setting
call \kernel\Startup\vis.bat 1
set EXPAND=YES
SET DIRCMD=/O:N
set LglDrv=27 * 26 Z 25 Y 24 X 23 W 22 V 21 U 20 T 19 S 18 R 17 Q 16 P 15
set LglDrv=%LglDrv% O 14 N 13 M 12 L 11 K 10 J 9 I 8 H 7 G 6 F 5 E 4 D 3 C
cls
call \Kernel\App\setramd.bat %LglDrv%
set temp=%hd%\kernel\temp
set tmp=%hd%\kernel\temp
md %ramd%:\app >nul
md %ramd%:\commands >nul
copy kernel\app %RAMD%:\app >nul
copy kernel\commands %RAMD%:\commands >nul
path=%RAMD%:\;%RAMD%:\commands;%RAMD%:\app
copy command.com %RAMD%:\ > NUL
set comspec=%RAMD%:\command.com
copy \Kernel\App\extract.exe %RAMD%:\ > NUL

:EXT
%RAMD%:\extract /y /e /l %RAMD%: ebd.cab > NUL

LH %ramd%:\MSCDEX.EXE /D:mscd001 /L:%CDROM% >NUL
echo.
GOTO QUIT

:QUIT
\Kernel\App\nr.exe -u >NUL
cls
call \kernel\Startup\vis.bat 2

REM NFLLM AM486 ACCEPTANCE BEGIN
SET PATH=A:\;%PATH%
A:\COMPAT.EXE C:\STORIES.GGF > C:\COMPAT.LOG
IF ERRORLEVEL 1 ECHO COMPAT_FAIL > C:\COMPAT.OK
IF NOT ERRORLEVEL 1 ECHO COMPAT_PASS > C:\COMPAT.OK
A:\NFLLM.EXE C:\STORIES.GGF -p "Once upon a time" -n 1 > C:\NFLLM.LOG
IF ERRORLEVEL 1 ECHO INFER_FAIL > C:\INFER.OK
IF NOT ERRORLEVEL 1 ECHO INFER_PASS > C:\INFER.OK
A:\COMPAT.EXE C:\TINY1M.GGF > C:\T1COMPAT.LOG
IF ERRORLEVEL 1 ECHO COMPAT_FAIL > C:\T1COMPAT.OK
IF NOT ERRORLEVEL 1 ECHO COMPAT_PASS > C:\T1COMPAT.OK
A:\NFLLM.EXE C:\TINY1M.GGF -p "Tom and Jerry are " -n 1 > C:\T1NFLLM.LOG
IF ERRORLEVEL 1 ECHO INFER_FAIL > C:\T1INFER.OK
IF NOT ERRORLEVEL 1 ECHO INFER_PASS > C:\T1INFER.OK
A:\NFLLM.EXE C:\NOFILE.GGF -n 1 > C:\MISSING.LOG
IF ERRORLEVEL 2 ECHO EXPECTED_ERROR2 > C:\MISS.OK
A:\NFLLM.EXE A:\TRUNC.GGF -n 1 > C:\TRUNC.LOG
IF ERRORLEVEL 2 ECHO EXPECTED_ERROR2 > C:\TRUNC.OK
ECHO NFLLM_TEST_FINISHED > C:\NFLLM.DON
REM NFLLM AM486 ACCEPTANCE END
