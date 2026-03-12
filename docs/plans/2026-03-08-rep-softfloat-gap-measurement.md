# REP and Softfloat Gap Measurement Implementation Plan

> Historical implementation-plan note (updated 2026-03-12): this plan captures the observability work used to measure REP and softfloat-disabled coverage pressure. That measurement work is already in tree, the branch has since closed the large measured REP string subset (`REP MOVS`, `REP STOS`, `REP SCASB`, `REP CMPSB`, `REP CMPSW` / `REP CMPSD`), and only a small REP tail now remains. The deeper env-var-driven investigation surfaces discussed below are also no longer part of the default app build; they now require a rebuild with `NEW_DYNAREC_DEVTOOLS=ON`. Read this file as preserved implementation history rather than the current branch-status source of truth. For current status, use [new-dynarec-executive-summary.md](./new-dynarec-executive-summary.md), [new-dynarec-changelog.md](./new-dynarec-changelog.md), and [new-dynarec-optimization-overview.md](./new-dynarec-optimization-overview.md).

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add minimal runtime accounting that tells us whether REP or softfloat-disabled direct coverage is the hotter remaining CPU dynarec gap under real workloads.

**Architecture:** Reuse the existing CPU dynarec observability surface instead of adding a separate framework. Count only the two measurement families we care about now, expose a small shutdown formatter, and classify them at the existing direct-vs-helper decision boundary in `src/codegen_new/codegen.c`.

**Tech Stack:** C11, existing CPU new dynarec observability/logging surface, focused standalone tests, CMake target build.

### Task 1: Add failing observability tests

**Files:**
- Modify: `tests/codegen_new_dynarec_observability_test.c`
- Modify: `src/cpu/codegen_public.h`

**Step 1: Write the failing test**

Add assertions for:
- zeroed REP/softfloat gap summaries after reset
- counting REP-family hits
- counting softfloat-family hits
- formatting only non-zero summaries

**Step 2: Run test to verify it fails**

Run:
```bash
cc -std=c11 -DUSE_NEW_DYNAREC -Isrc/cpu tests/codegen_new_dynarec_observability_test.c src/codegen_new/codegen_observability.c -o /tmp/codegen_new_dynarec_observability_test
```

Expected: fail with missing declarations for the new gap-family APIs.

### Task 2: Implement minimal observability APIs

**Files:**
- Modify: `src/cpu/codegen_public.h`
- Modify: `src/codegen_new/codegen_observability.c`

**Step 1: Add the counters and formatter**

Implement:
- REP-family fallback count
- softfloat-family fallback count
- reset behavior
- summary formatting helpers
- optional logging enable helper

**Step 2: Run test to verify it passes**

Run:
```bash
cc -std=c11 -DUSE_NEW_DYNAREC -Isrc/cpu tests/codegen_new_dynarec_observability_test.c src/codegen_new/codegen_observability.c -o /tmp/codegen_new_dynarec_observability_test && /tmp/codegen_new_dynarec_observability_test
```

Expected: pass.

### Task 3: Wire classification and shutdown logging

**Files:**
- Modify: `src/codegen_new/codegen.c`
- Modify: `src/86box.c`

**Step 1: Classify the existing fallback path**

At the current direct-vs-helper decision boundary, classify only:
- REP-prefixed helper-path traffic
- softfloat-disabled x87 direct-path exclusions

**Step 2: Emit shutdown report**

Add an optional shutdown log gated by a dedicated env var so workload runs can report those counts without changing the default summary.

### Task 4: Verify and launch

**Files:**
- Test: `tests/codegen_new_dynarec_observability_test.c`
- Test: `tests/codegen_new_opcode_coverage_policy_test.c`
- Test: `tests/codegen_new_allocator_pressure_policy_test.c`

**Step 1: Run focused tests**

Run:
```bash
cc -std=c11 -DUSE_NEW_DYNAREC -Isrc/cpu tests/codegen_new_dynarec_observability_test.c src/codegen_new/codegen_observability.c -o /tmp/codegen_new_dynarec_observability_test && /tmp/codegen_new_dynarec_observability_test
cc -std=c11 -DUSE_NEW_DYNAREC -Isrc/include -Isrc/cpu tests/codegen_new_opcode_coverage_policy_test.c src/codegen_new/codegen_observability.c -o /tmp/codegen_new_opcode_coverage_policy_test && /tmp/codegen_new_opcode_coverage_policy_test
cc -std=c11 -DUSE_NEW_DYNAREC -Isrc/include -Isrc/cpu tests/codegen_new_allocator_pressure_policy_test.c src/codegen_new/codegen_observability.c -o /tmp/codegen_new_allocator_pressure_policy_test && /tmp/codegen_new_allocator_pressure_policy_test
```

Expected: all pass.

**Step 2: Run target build**

Run:
```bash
cmake --build build --target 86Box cpu dynarec mem -j4
```

Expected: success, with only the pre-existing macOS/Homebrew deployment-target linker warnings.

**Step 3: Re-sign before runtime use**

Run:
```bash
codesign -s - --force --deep build/src/86Box.app
```

Expected: signature replaced successfully.

## Follow-up validation status

- The original REP vs softfloat question was answered narrowly:
  - the first gap-family pass produced no useful line because shutdown logging returned too early when 3DNow logging was disabled
  - that control-flow bug in `src/86box.c` is now fixed
- The corrected family-level workload result is:
  - `base=48526`
  - `rep=8100`
  - `0f=6763`
  - `x87=2843`
  - `3dnow=0`
- That result changed the next target:
  - REP is real but not primary
  - softfloat is irrelevant for the current configuration
  - the next evidence-backed implementation target is the dominant `base` fallback bucket
- A deeper follow-up measurement now exists too:
  - `86BOX_NEW_DYNAREC_LOG_BASE_FALLBACKS=1` narrows the `base` bucket to exact opcodes
  - the dominant observed cluster is led by `0x9a`, `0xca`, `0xab`, `0xc8`, `0xf7`, `0xad`, `0x9d`, `0xcb`, `0xa5`, `0x6b`, `0xac`, and `0xff`
- Recommended next implementation batch from this measurement:
  - start with the hot base string/far-control cluster, then remeasure
- Follow-up status:
  - the first landed batch took the narrowest string-op slice from that cluster: non-REP `STOS`/`LODS` (`0xaa`, `0xab`, `0xac`, `0xad`)
  - a confirming 3DMark99 rerun with fallback-family and base-opcode logging shows the shutdown family line `base=31127 0f=4880 x87=319 rep=6818 3dnow=0`
  - that same shutdown base-opcode report no longer contains `0xaa`, `0xab`, `0xac`, or `0xad`
  - the next measured base-opcode subset was therefore the far control / frame cluster `0x9a`, `0xca`, `0xc8`, `0x9d`, `0xcb`
  - the first landed slice from that cluster is `ENTER` (`0xc8`), chosen ahead of `0x9a`, `0xca`, `0x9d`, and `0xcb` because it is the only remaining table hole in that cluster without protected-mode far-transfer or `POPF` privilege semantics
  - a guest-visible rerun after `ENTER` confirmed that `0xc8` dropped out of the shutdown base-fallback report, with the family line `base=28738 0f=4886 x87=410 rep=6700 3dnow=0`
  - the next landed slice from that same cluster is `POPF` (`0x9d`), implemented as a legality-first non-V86 direct path while preserving fallback for the V86-sensitive path
  - the first `POPF` guest run crashed due to a mixed-width IR bug in the new upper-EFLAGS merge sequence; the follow-up fix corrected that backend-illegal form
  - the confirming post-fix 3DMark99 rerun shows the shutdown family line `base=25331 0f=4672 x87=451 rep=6754 3dnow=0`
  - that same shutdown base-opcode report no longer contains `0xc8` or `0x9d`
  - the remaining measured base-opcode list at that point was `0x9a`, `0xca`, `0xf7`, `0xcb`, `0xa5`, `0x6b`, and `0xff`
  - the next landed non-protected measured batch then took `MOVS` word/dword (`0xa5`) plus `IMUL r, r/m, imm8` (`0x6b`) together, leaving the protected-mode far-transfer subset untouched
  - the first `0x6b` guest run crashed due to an invalid `W <- W` `MOVZX` in the helper-backed IMUL flag-mask path; the follow-up fix removed that backend-illegal form
  - the confirming post-fix 3DMark99 rerun shows the shutdown family line `base=19237 0f=4063 x87=231 rep=5626 3dnow=0`
  - that same shutdown base-opcode report no longer contains `0xa5` or `0x6b`
  - the next low-risk sibling choice then took `IMUL r, r/m, imm16/32` (`0x69`) ahead of `MOVSB` (`0xa4`) because it reuses the already validated `0x6b` helper/result/flags structure without reopening byte-string move semantics
  - the confirming 3DMark99 rerun shows the shutdown family line `base=21172 0f=4693 x87=478 rep=6794 3dnow=0`
  - that same shutdown base-opcode report no longer contains `0x69`
  - a follow-up low-risk sibling cleanup then took `MOVSB` (`0xa4`) once code inspection confirmed `0x69` was already in tree for this branch state
  - the confirming 3DMark99 rerun for that step shows the shutdown family line `base=32181 0f=7533 x87=717 rep=11137 3dnow=0`
  - that same shutdown base-opcode report no longer contains `0xa4`
  - the next measured protected-mode follow-up then took immediate far call (`0x9a`) as the narrowest remaining true table hole ahead of the far-return pair (`0xca`, `0xcb`) and the mixed-group bailout pair (`0xf7`, `0xff`)
  - a confirming 3DMark99 rerun for that step shows the shutdown family line `base=19132 0f=6897 x87=1671 rep=9029 3dnow=0`
  - that same shutdown base-opcode report no longer contains `0x9a`
  - the measured remaining hotspot list is now `0xca`, `0xcb`, `0xf7`, and `0xff`
  - the next measured branch-point is now resolved in favor of the protected-mode far-return pair (`0xca`, `0xcb`) because it stays inside one far-return semantic family and only needs the protected-mode helper-backed completion path on top of already-direct real/V86 handling, unlike the broader mixed-group bailout pair (`0xf7`, `0xff`)
  - that far-return pair is now implemented in tree with focused coverage-policy assertions, fresh standalone verification, a rebuild, and a re-sign
  - a confirming guest rerun logged to `/tmp/new_dynarec_retf_validation.log` reached shutdown with `base=10076 0f=6801 x87=1595 rep=9191 3dnow=0`
  - that same shutdown base-opcode report no longer contains `0xca` or `0xcb`
  - the first follow-up rerun for that next pair logged to `/tmp/new_dynarec_f7_ff_validation.log` reached shutdown with `base=6475 0f=6805 x87=1421 rep=9092 3dnow=0`
  - that same shutdown base-opcode report no longer contains `0xf7`, while `0xff` still remains
  - the follow-up narrow fix pair then added direct `0xf6` byte-group arithmetic and completed the remaining `0xff` indirect far-call path
  - the confirming rerun logged to `/tmp/new_dynarec_f6_ff_validation.log` reached shutdown with `base=5142 0f=6906 x87=1607 rep=9182 3dnow=0`
  - that same shutdown base-opcode report no longer contains `0xf6` or `0xff`
  - the old mixed-group hotspot sequence on this CPU mix is therefore now closed through `0xf7`, `0xf6`, and `0xff`
  - protected-mode follow-up remains explicitly in scope, while softfloat / x87 remains intentionally out of the near-term batch discussion
  - the next measured base-opcode target should now be chosen only after re-baselining on the planned MMX-only CPU configuration
  - that MMX-only re-baseline is now complete with `/tmp/new_dynarec_mmx_only_validation.log`, which reached shutdown with `base=5004 0f=6455 x87=2124 rep=9343 3dnow=0`
  - this materially changed the prior ordering: REP is now hottest overall, `0F` is second, and plain base opcodes are third instead of first
  - the new exact base-opcode report is led by `0xd1` (719), `0xd3` (605), `0xee` (592), `0xcd` (518), `0xe6` (400), `0xcf` (334), `0xec` (314), `0x8e` (259), `0xd0` (244), and `0x9b` (204)
  - the missing observability for choosing an MMX subset is now explicit: current shutdown logging still does not expose exact `0F` fallback opcodes, only the family total
  - the next measurement pass should add exact `0F` shutdown fallback logging with the same `helper_table_null` / `helper_bailout` split already used for plain base opcodes
  - that `0F` logging should also make MMX-visible ranking possible, either by exact secondary opcode or by an extra MMX/non-MMX derived summary
  - that observability step is now complete: `/tmp/new_dynarec_mmx_only_0f_validation.log` records exact `0F` fallback opcodes for the longer MMX-only run
  - the longer-run family line is `base=7881 0f=11311 x87=4001 rep=15503 3dnow=0`, which preserves the same `rep > 0f > base > x87` ordering under a much larger sample
  - the longest-run exact `0F` ranking is now led by `0xaf` (3416), `0xba` (2108), `0x94` (914), `0x95` (828), `0x02` (585), `0xc8` (500), `0xb3` (368), `0xab` (336), `0xa3` (330), and `0x03` (299)
  - those `0F` hotspots are all `helper_table_null` hits, not `helper_bailout` hits
  - the clearest next generic base-opcode batch from that MMX-only report is the shift/rotate bailout family `0xd0`-`0xd3`
  - the clearest overall next batch after the longer MMX-only run is now a first exact `0F` coverage slice chosen from those ranked opcodes, with `0xaf`, `0xba`, `0x94`, and `0x95` as the best initial candidate group pending instruction-family mapping
