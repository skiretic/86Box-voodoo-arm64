# WL-05 Win98 Microstress

Deterministic Win98-friendly console workload for targeted dynarec validation.
Current build is Win98-safe CRT-free binary (imports `KERNEL32.dll` only).

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

## What this tests (explicit)

Run modes:
- `quick`: same phase coverage, reduced iteration counts. Fast regression gate.
- `normal`: same phase coverage, full iteration counts. Main stability gate.
- `smc`: same as `normal` plus `smc_touch` enabled. Churn/SMC guardrail gate.

Phase intent:
- `imm_store`:
  - stresses immediate write patterns and dependent reload paths.
  - primary signal for `S-02` (`CODEGEN_BACKEND_HAS_MOV_IMM` + direct imm-store hooks).
- `branch_helper`:
  - stresses branch-heavy/control-flow-dense integer behavior.
  - primary signal for `A-013` (call/jump dispatch shape changes).
- `mmx_touch`:
  - forces MMX instruction path activity and entry/exit handling.
  - primary targeted signal for `S-01` (`codegen_MMX_ENTER` patch-site correctness).
- `smc_touch`:
  - mutates executable bytes and re-executes generated code.
  - tests self-modifying-code-adjacent behavior under deterministic loop.
  - primary guardrail signal for `S-03` churn-policy work.

Detection model:
- Exact expected output markers + deterministic final totals.
- Pass means no crash/error and stable totals for locked VM profile.
- Fail means runtime error/crash/missing marker or unexpected total drift.

## Build (macOS host with Homebrew MinGW)

Use the helper script:

```bash
tools/win98-microstress/build-win98-microstress.sh
```

Or compile directly:

```bash
i686-w64-mingw32-gcc -O2 -march=i586 -mmmx -s \
  -fno-stack-protector -ffreestanding -nostdlib \
  -Wl,-e,_start -Wl,--subsystem,console \
  -o tools/win98-microstress/MICROSTR.EXE \
  tools/win98-microstress/microstress_win98.c -lkernel32
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
D:\SCRIPTS\MRUNALL.BAT D:
```

`MRUNALL.BAT` runs quick + normal + smc in one shot and prints compact summary block for screenshot capture.

## Validation notes

- Treat any `MICROSTRESS_ERROR` line or nonzero exit code as failure.
- Capture stdout into workload artifacts using the existing wave-1 naming rules.
- Keep VM profile fixed when comparing checksums across runs.
- Validated canonical baseline totals (`2026-04-21`, K6-2 canonical VM):
  - quick (`MRUNQ`): `45db7b65`
  - normal (`MRUN`): `2520dd5e`
  - smc (`MRUNS`): `b86f22a1`
