WIN98 THROUGHPUT VALIDATION KIT
===============================

Files:
- THROUGHCOV.EXE: deterministic throughput validation tool
- SCRIPTS\RUN_SMOKE.BAT
- SCRIPTS\RUN_FULL.BAT
- SCRIPTS\RUN_STRESS.BAT

Usage (inside Win98 guest):
1) Mount this ISO as CD-ROM (assume drive D:).
2) Run one of:
   D:\SCRIPTS\RUN_SMOKE.BAT D:
   D:\SCRIPTS\RUN_FULL.BAT D:
   D:\SCRIPTS\RUN_STRESS.BAT D:

Output format:
- BEGIN MODE=<MODE>
- PHASE Pn ITERS=<N> HASH=<hex>
- TOTAL MODE=<MODE> HASH=<hex>
- RESULT MODE=<MODE> PASS|FAIL
- DONE

Expected totals:
- SMOKE:  747ae302
- FULL:   9113ac58
- STRESS: 487a9954

Validation rule:
- PASS and DONE must both appear.
- TOTAL hash should match expected table for mode.
