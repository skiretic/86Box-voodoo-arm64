3DNOWCOV deterministic Win98 guest stress/validation tool.

Files:
- 3DNOWCOV.EXE        : 3DNow opcode stress and validation runner
- SCRIPTS\COV3D_RUN.BAT   : full run
- SCRIPTS\COV3D_QUICK.BAT : shorter smoke run

Run from guest:
  D:\SCRIPTS\COV3D_RUN.BAT D:

Expected on-screen markers:
- 3DNOWCOV_START
- 3DNOWCOV_PREP_START / 3DNOWCOV_PREP_END
- 3DNOWCOV_LOOP_START 1 / 3DNOWCOV_LOOP_END 1
- per-op lines: 3DNOW_OP <name> PASS|FAIL|SKIP_NO_3DNOWEXT hash=<...>
- 3DNOWCOV_LOOP_START 2 / 3DNOWCOV_LOOP_END 2
- 3DNOWCOV_COUNTS ...
- 3DNOWCOV_TOTAL hash=...
- 3DNOWCOV_DONE or 3DNOWCOV_ERROR ...

