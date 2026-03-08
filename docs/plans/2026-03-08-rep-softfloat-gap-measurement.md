# REP and Softfloat Gap Measurement Implementation Plan

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
  - the remaining measured base-opcode list is now `0x9a`, `0xca`, `0xf7`, `0xcb`, `0xa5`, `0x6b`, and `0xff`
  - the next measured choice is now between the remaining protected-mode far-transfer subset (`0x9a`, `0xca`, `0xcb`) and the safer non-far `MOVS` (`0xa5`) follow-up
