@echo off
set SRC_DRIVE=%1
if "%SRC_DRIVE%"=="" set SRC_DRIVE=D:

echo MICRO_ALL_START

if not exist %SRC_DRIVE%\MICROSTR.EXE goto MISSING_EXE
if not exist C:\PERF_LOG\NUL md C:\PERF_LOG

echo.
echo [1/3] QUICK
echo MICRO_RUN_CMD %SRC_DRIVE%\MICROSTR.EXE --quick > C:\PERF_LOG\wl05-micro-last.txt
%SRC_DRIVE%\MICROSTR.EXE --quick > C:\PERF_LOG\wl05-micro-quick.log
if errorlevel 1 goto QUICK_FAIL
find "MICROSTRESS_DONE" C:\PERF_LOG\wl05-micro-quick.log >NUL
if errorlevel 1 goto QUICK_FAIL

echo [2/3] NORMAL
echo MICRO_RUN_CMD %SRC_DRIVE%\MICROSTR.EXE > C:\PERF_LOG\wl05-micro-last.txt
%SRC_DRIVE%\MICROSTR.EXE > C:\PERF_LOG\wl05-micro-normal.log
if errorlevel 1 goto NORMAL_FAIL
find "MICROSTRESS_DONE" C:\PERF_LOG\wl05-micro-normal.log >NUL
if errorlevel 1 goto NORMAL_FAIL

echo [3/3] SMC
echo MICRO_RUN_CMD %SRC_DRIVE%\MICROSTR.EXE --smc > C:\PERF_LOG\wl05-micro-last.txt
%SRC_DRIVE%\MICROSTR.EXE --smc > C:\PERF_LOG\wl05-micro-smc.log
if errorlevel 1 goto SMC_FAIL
find "MICROSTRESS_DONE" C:\PERF_LOG\wl05-micro-smc.log >NUL
if errorlevel 1 goto SMC_FAIL

echo.
echo MICRO_ALL_RESULTS_BEGIN
echo QUICK:
find "MICROSTRESS_DONE" C:\PERF_LOG\wl05-micro-quick.log
echo MICRO_SUMMARY status=OK mode=quick log=C:\PERF_LOG\wl05-micro-quick.log
echo NORMAL:
find "MICROSTRESS_DONE" C:\PERF_LOG\wl05-micro-normal.log
echo MICRO_SUMMARY status=OK mode=normal log=C:\PERF_LOG\wl05-micro-normal.log
echo SMC:
find "MICROSTRESS_DONE" C:\PERF_LOG\wl05-micro-smc.log
echo MICRO_SUMMARY status=OK mode=smc log=C:\PERF_LOG\wl05-micro-smc.log
echo MICRO_ALL_RESULTS_END
echo MICRO_ALL_DONE
goto DONE

:MISSING_EXE
echo MICRO_SUMMARY status=ERROR mode=all reason=missing_exe
echo MICRO_ALL_ERROR missing %SRC_DRIVE%\MICROSTR.EXE
goto DONE

:QUICK_FAIL
echo MICRO_SUMMARY status=ERROR mode=quick reason=run_failed log=C:\PERF_LOG\wl05-micro-quick.log
echo MICRO_ALL_ERROR quick failed
goto DONE

:NORMAL_FAIL
echo MICRO_SUMMARY status=ERROR mode=normal reason=run_failed log=C:\PERF_LOG\wl05-micro-normal.log
echo MICRO_ALL_ERROR normal failed
goto DONE

:SMC_FAIL
echo MICRO_SUMMARY status=ERROR mode=smc reason=run_failed log=C:\PERF_LOG\wl05-micro-smc.log
echo MICRO_ALL_ERROR smc failed
goto DONE

:DONE
