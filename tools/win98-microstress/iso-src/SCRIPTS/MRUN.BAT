@echo off
set SRC_DRIVE=%1
if "%SRC_DRIVE%"=="" set SRC_DRIVE=D:

echo MICRO_RUN_START

if not exist %SRC_DRIVE%\MICROSTR.EXE goto MISSING_EXE

if not exist C:\PERF_LOG\NUL md C:\PERF_LOG

echo MICRO_RUN_CMD %SRC_DRIVE%\MICROSTR.EXE > C:\PERF_LOG\wl05-micro-last.txt
%SRC_DRIVE%\MICROSTR.EXE > C:\PERF_LOG\wl05-micro.log
if errorlevel 1 goto RUN_FAIL

find "MICROSTRESS_DONE" C:\PERF_LOG\wl05-micro.log >NUL
if errorlevel 1 goto MISSING_DONE

find "MICROSTRESS_DONE" C:\PERF_LOG\wl05-micro.log
echo MICRO_SUMMARY status=OK mode=normal log=C:\PERF_LOG\wl05-micro.log
echo MICRO_RUN_DONE
goto DONE

:MISSING_EXE
echo MICRO_SUMMARY status=ERROR mode=normal reason=missing_exe
echo MICRO_RUN_ERROR missing %SRC_DRIVE%\MICROSTR.EXE
goto DONE

:RUN_FAIL
find "MICROSTRESS_ERROR" C:\PERF_LOG\wl05-micro.log
echo MICRO_SUMMARY status=ERROR mode=normal reason=nonzero_exit log=C:\PERF_LOG\wl05-micro.log
echo MICRO_RUN_ERROR microstress exited nonzero
goto DONE

:MISSING_DONE
echo MICRO_SUMMARY status=ERROR mode=normal reason=missing_done_marker log=C:\PERF_LOG\wl05-micro.log
echo MICRO_RUN_ERROR missing MICROSTRESS_DONE marker
goto DONE

:DONE
