# WL-05 Win98 Microstress

Deterministic Win98-friendly console workload for targeted dynarec validation.

Source file:
- `microstress_win98.c`

Output markers:
- `MICROSTRESS_START`
- `PHASE_START <name>`
- `PHASE_END <name> checksum=<hex> iters=<n>`
- `PHASE_SKIP smc_touch reason=disabled` (if `--smc` is not used)
- `MICROSTRESS_DONE total=<hex>`
- `MICROSTRESS_ERROR ...` (failure marker)

Batch wrapper summary markers (easy to share):
- `MICRO_SUMMARY status=OK ...`
- `MICRO_SUMMARY status=ERROR ...`

Phases:
- `imm_store` (immediate-heavy memory traffic; supports `S-02`)
- `branch_helper` (branch/control-flow density; supports `A-013`)
- `mmx_touch` (MMX touchpoint; supports `S-01` targeted checks)
- `smc_touch` (optional self-modifying phase; supports `S-03` targeted checks)

## Build (macOS host with Homebrew MinGW)

Use the helper script:

```bash
tools/win98-microstress/build-win98-microstress.sh
```

Or compile directly:

```bash
i686-w64-mingw32-gcc -O2 -march=i586 -mmmx -s \
  -o tools/win98-microstress/MICROSTR.EXE \
  tools/win98-microstress/microstress_win98.c
```

## Build ISO Kit (recommended)

This packages `MICROSTR.EXE` plus Win98 batch wrappers into one mountable ISO.

```bash
tools/win98-microstress/create-win98-microstress-iso.sh
```

Output:
- `tools/win98-microstress/win98-microstress-kit.iso`

## Guest usage (Windows 98)

Copy `MICROSTR.EXE` into the guest and run:

```bat
MICROSTR.EXE
```

Optional modes:

```bat
MICROSTR.EXE --quick
MICROSTR.EXE --smc
MICROSTR.EXE --quick --smc
```

Or use ISO scripts (writes logs to `C:\PERF_LOG\`):

```bat
D:\SCRIPTS\MICRO_RUN.BAT D:
D:\SCRIPTS\MICRO_RUN_QUICK.BAT D:
D:\SCRIPTS\MICRO_RUN_SMC.BAT D:
D:\SCRIPTS\MRUN.BAT D:
```

## Validation notes

- Treat any `MICROSTRESS_ERROR` line or nonzero exit code as failure.
- Capture stdout into workload artifacts using the existing wave-1 naming rules.
- Keep VM profile fixed when comparing checksums across runs.
