# ARM64 Voodoo JIT Optimization — Executive Summary

**Started**: 2026-02-20
**Goal**: Reduce per-pixel CPU load in the ARM64 JIT codegen without impacting rendering accuracy
**Constraint**: ARMv8.0-A baseline only, bit-identical output required

---

## Progress Tracker

| Batch | Description | Optimizations | Est. Savings | Audit | Impl | Build | Test | Commit | Status |
|-------|-------------|---------------|-------------|-------|------|-------|------|--------|--------|
| 1 | Alpha blend rounding cleanup | H7, H8 | ~22 cyc/px | PASS | [x] | [x] | [x] | [x] | DONE |
| 2 | Cache STATE_x / STATE_x2 | H2, H3 | ~20-36 cyc/px | PASS | [x] | [x] | [x] | [x] | DONE |
| 3 | Hoist params delta loads | H1 | ~20-36 cyc/px | PARTIAL | [x] | [x] | [x] | [x] | DONE |
| 4 | BIC+ASR clamp idiom | M2 | ~16 cyc/px | PASS | [x] | [x] | [x] | [x] | DONE |
| 5 | Pin rgb565 ptr + batch counters | M4, M7 | ~8 cyc/px | PASS | [x] | [x] | [x] | [x] | DONE |
| 6 | Cache LOD + iterated BGRA | H4, H6 | ~9-16 cyc/px | PASS | [x] | [x] | [x] | [x] | DONE |
| 7 | Misc small wins + dead code | M1,M3,M5,M6,L1,L2 | ~16-22 cyc/px | PASS | [x] | [x] | [x] | [x] | DONE |
| 7-fix | Batch 7/M5 alpha extraction bug | — | — | PASS | [x] | [x] | [x] | [x] | DONE |
| 8 | Loop + CC + stipple peepholes (R2) | R2-24,R2-25,R2-13,R2-27 | ~4 insn/px | PASS | [x] | [x] | [x] | [x] | DONE |
| D | SDIV → reciprocal (deferred) | H5 | ~5-15 cyc/px | N/A | — | — | — | — | DEFERRED |

**Legend**: PASS = audit verified, PARTIAL = partially audited, PENDING = not yet audited

**Completed**: 7 / 7 Round 1 batches + 1 Round 2 batch (Batch 8 pending commit)
**Estimated total savings**: 15-25% fewer instructions per pixel (~80-100 insns removed, Round 1) + ~4 insns/pixel (Round 2 Batch 8)

---

## Round 2 Optimization Audit

A second optimization audit was performed on 2026-02-21, generating 33 findings across 10 categories
(report: `optimization-audit-round2.md`). 15 findings were actionable. Batch 8 implements the 4 safest.

### Remaining Work

| Item | Type | Description | Est. Savings | Risk |
|------|------|-------------|-------------|------|
| **Batch 9** | Per-pixel opt | R2-07 (ebp_store elimination via x17), R2-12 (DUP_V4H_GPR), R2-08 (LOD cache for point-sample) | ~3-5 insns/pixel | Moderate |
| **Batch 10** | Prologue opt | R2-23 (skip zero halfwords in EMIT_MOV_IMM64), R2-09 (MOVZ with hw for 1<<48) | ~5-10 insns/block (negligible wall-clock) | Zero |
| **Batch D** | Per-pixel opt | SDIV → reciprocal approximation for perspective W division | ~5-15 cyc/pixel | High (accuracy) |
| **JIT cache expansion** | Infrastructure | Increase cache from 8 to 16-32 slots per thread to reduce recompilation thrashing (258K recompiles for 351 configs). Smoothness/latency improvement, not throughput. | Reduced micro-stutters | Low |
| **Performance benchmarking** | Validation | A/B comparison of pre-optimization vs post-optimization builds with fixed workload. Actually measure the estimated 10-20% wall-clock improvement. | N/A | N/A |
| **Debug logging levels** | Infrastructure | Add verbosity tiers (level 1 = GENERATE only, level 2 = +EXECUTE/POST, level 3 = +PIXELS). Current uncapped logging produces 38GB+ logs. | N/A | Zero |

**Recommended order**: Batch 9 → JIT cache expansion → Batch 10 → Debug logging levels → Performance benchmarking → Batch D

---

## Commit Log

| Batch | Commit | Date | Description |
|-------|--------|------|-------------|
| — | `812bf0ab9` | 2026-02-20 | Dead code removal (MINOR-1 + MINOR-3 from correctness audit) |
| 1 | `ea8443b0f` | 2026-02-20 | Alpha blend rounding cleanup (H7: v8 replaces LDR alookup[1], H8: USHR Rd!=Rn eliminates MOV) |
| 2 | `89dbbe6de` | 2026-02-20 | Cache STATE_x in w28 (H2: 9 loads eliminated), STATE_x2 in w27 (H3: 1 load eliminated) |
| 3 | `e37e3cc0c` | 2026-02-20 | Hoist RGBA deltas→v12, TMU0 ST→v15, TMU1 ST→v14; frame 160→176 bytes |
| 4 | `269af2375` | 2026-02-20 | BIC+ASR clamp idiom: 9 sites updated, new ARM64_BIC_REG_ASR macro added |
| 5 | `63d60c3db` | 2026-02-20 | Pin rgb565 ptr in x26 (M4), LDP/STP counter pairing (M7) |
| 6 | `4ba01f4b4` | 2026-02-20 | Cache LOD in w6 (H4: 3 reloads eliminated), iterated BGRA in v6 (H6: 4 pack sequences replaced) |
| 7 | `877bf0e6a` | 2026-02-20 | Misc small wins: LDP pairing (M1), fogColor hoist to v11 (M3), TCA alpha extract-once (M5), BFI RGB565 (M6), CBZ guard (L1), eliminate MOV w11 (L2), dead neon_minus_254 removal |
| 7-fix | `b48b0d763` | 2026-02-21 | Batch 7/M5 bugfix: moved TMU0 alpha extraction before SMULL (was after tca_sub_clocal read of w13) |
| 8 | `9dddfba23` | 2026-02-21 | Round 2 peepholes: STATE_x LDR before loop (R2-24), eliminate MOV w4,w28 (R2-25), remove MOV v16,v0 in cc multiply (R2-13), MVN directly from w28 in stipple (R2-27) |

---

## Total Savings Breakdown

### Per-Pixel Instruction Savings by Batch

| Batch | What | Insns Removed |
|-------|------|---------------|
| Dead code | Unused MOV v5,v1 + MOVI v2,#0 | 2/block |
| 1 (H7+H8) | alookup[1] LDR → v8, MOV before USHR eliminated | ~28 (alpha blend path) |
| 2 (H2+H3) | Cache STATE_x in w28, STATE_x2 in w27 | ~11 memory loads |
| 3 (H1) | Hoist RGBA/TMU deltas to v12/v15/v14 | 3-4 |
| 4 (M2) | BIC+ASR clamp at 9 sites (5→3 insns each) | ~18 (across clamp sites) |
| 5 (M4+M7) | Pin rgb565 in x26 + LDP/STP counters | ~4 |
| 6 (H4+H6) | Cache LOD in w6 + iterated BGRA in v6 | 12-16 |
| 7 (M1+M3+M5+M6+L1+L2) | LDP, fogColor hoist, TCA extract-once, BFI, CBZ, MOV elim | 16-22 |
| 8 (R2-24+R2-25+R2-13+R2-27) | Loop LDR hoist, loop MOV elim, cc multiply MOV, stipple MVN | ~4 |

### By Category

| Category | Insns Removed |
|----------|---------------|
| Redundant memory loads (STATE_x, LOD, deltas, constants) | 30-45 |
| Unnecessary register copies (MOV before USHR/SMULL/etc.) | ~18 |
| Oversized clamp sequences (5-insn → 3-insn × 9 sites) | ~18 |
| Constant rematerialization (rgb565 ptr, alookup[1]) | 8-10 |
| Misc peepholes (LDP, BFI, CBZ, fog hoist, loop opts) | 14-18 |

### Total

**~84-105 instructions removed per pixel** (path-dependent — alpha blend and dual-TMU paths save more).
Estimated **15-25% fewer instructions per pixel**, roughly **10-20% wall-clock improvement** on the pixel pipeline.

Only remaining candidate: Batch D (SDIV → reciprocal, est. 5-15 cyc/px) — deferred due to accuracy risk.

---

## What This Is

The ARM64 Voodoo JIT generates native ARM64 machine code at runtime to accelerate the Voodoo GPU pixel pipeline. The current implementation is a correct, bit-accurate port of the x86-64 JIT — it was built for correctness first, not performance. This optimization pass tightens the generated code without changing behavior.

## What We're NOT Doing

- No accuracy tradeoffs — every optimization must produce bit-identical pixels
- No ARMv8.1+ features — must run on all ARM64 hardware
- No architectural changes — same JIT structure, same pipeline stages
- No SDIV replacement (yet) — deferred until all other optimizations are proven safe

## Key Findings

### Where the cycles go (per pixel, typical textured + alpha-blended)

| Category | Current | After All Batches | Savings |
|----------|---------|-------------------|---------|
| Redundant memory loads (STATE_x, LOD, deltas, constants) | ~40-60 | ~10-15 | 30-45 |
| Unnecessary register copies (MOV before USHR) | ~14 | 0 | 14 |
| Oversized clamp sequences (5-insn → 3-insn) | ~40 | ~24 | 16 |
| Constant rematerialization (rgb565 ptr, alookup[1]) | ~10-12 | ~2 | 8-10 |
| Other (LDP pairing, fogColor hoist, etc.) | ~20 | ~4-8 | 12-16 |
| **Total overhead removed** | | | **~80-100** |

### Risk Assessment

| Risk Level | Batches | Notes |
|------------|---------|-------|
| Zero risk | 1, 5 | Mechanical replacement, no logic changes |
| Low risk | 2, 3, 4, 6 | Register caching of proven-invariant values |
| Medium risk | 7 | Multiple small changes, need full regression |
| High risk | Deferred (H5) | Accuracy implications, not scheduled |

### Audit Status

The optimization plan was independently audited by the voodoo-debug agent. Two register assignment errors were found and corrected:

- **v10**: Claimed available but used for `neon_ff_b` (cc_invert path) — reassigned H6 to v14
- **v13**: Claimed available but used for color-before-fog (ACOLORBEFOREFOG blend) — reassigned H1 deltas to v15

One bonus finding: **v11** (neon_minus_254) is loaded in the prologue but never used — dead code that was removed in Batch 7 when repurposing v11 for fogColor (M3).

All optimization logic, encoding claims, and safety analysis verified correct.

### Round 1 Final Verification (2026-02-20)

After all 7 batches, a comprehensive feature parity audit confirmed the ARM64 codegen matches the x86-64 reference across all 17 pipeline stages. No features were removed or degraded by any optimization.

Final regression test (Q3, Turok, 3DMark99, 3DMark2000, UT99):
- 68,426 blocks compiled, 0 rejects, 0 errors
- 4.4 billion pixels rendered
- 274 unique pipeline configurations exercised
- 25 texture modes, 30 color configs, 25 alpha modes, 4 fog modes
- Verdict: **HEALTHY**

### Round 2 Batch 8 Verification (2026-02-21)

Extensive regression test (Q3, 3DMark, multiple games on Voodoo 3 dual-TMU):
- 138K blocks compiled, 0 rejects, 0 errors
- 5.5 billion pixels rendered (174M JIT executions, 0 interpreter fallbacks)
- 351 unique pipeline configurations exercised
- 30 texture modes, 30 color configs, 28 alpha modes, 4 fog modes
- 37M unique Z values, 32,550 unique RGB565 colors
- Verdict: **HEALTHY**

## Documents

| File | Contents |
|------|----------|
| `optimization-plan.md` | Detailed implementation plan with 7 batches |
| `perf-optimization-report.md` | Full analysis of all 20 optimization opportunities |
| `optimization-plan-audit.md` | Independent audit of the plan's accuracy |
| `codegen-audit-report.md` | Prior correctness audit (0 bugs found) |
| `batch7-audit.md` | Batch 7 pre-implementation audit |
| `feature-parity-audit.md` | Final feature parity audit (ARM64 vs x86-64) |
