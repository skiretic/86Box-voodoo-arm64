Win98 Microstress Kit

Contents:
- MICROSTR.EXE                 deterministic microstress workload
- SCRIPTS\MICRO_RUN.BAT        normal run
- SCRIPTS\MICRO_RUN_QUICK.BAT  fast sanity run
- SCRIPTS\MICRO_RUN_SMC.BAT    run with optional SMC phase enabled
- SCRIPTS\MRUN.BAT             short-name alias for MICRO_RUN.BAT
- SCRIPTS\MRUNQ.BAT            short-name alias for MICRO_RUN_QUICK.BAT
- SCRIPTS\MRUNS.BAT            short-name alias for MICRO_RUN_SMC.BAT

Recommended guest usage:
1) Mount this ISO in Win98 CD-ROM (assume D:)
2) Open MS-DOS Prompt and run one of:
   D:\SCRIPTS\MICRO_RUN.BAT D:
   D:\SCRIPTS\MICRO_RUN_QUICK.BAT D:
   D:\SCRIPTS\MICRO_RUN_SMC.BAT D:
   D:\SCRIPTS\MRUN.BAT D:

Expected output markers:
- MICRO_RUN_START
- MICRO_RUN_CMD ...
- MICRO_RUN_DONE

Failure marker:
- MICRO_RUN_ERROR ...

Guest logs are always written to:
- C:\PERF_LOG\wl05-micro.log
- C:\PERF_LOG\wl05-micro-last.txt

If your CD drive letter is not D:, pass the correct one (for example E:).
