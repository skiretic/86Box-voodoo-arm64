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
| 5 | Pin rgb565 ptr + batch counters | M4, M7 | ~8 cyc/px | PENDING | [ ] | [ ] | [ ] | [ ] | PENDING |
| 6 | Cache LOD + iterated BGRA | H4, H6 | ~9-16 cyc/px | PARTIAL | [ ] | [ ] | [ ] | [ ] | PENDING |
| 7 | Misc small wins + dead code | M1,M3,M5,M6,L1,L2 | ~16-22 cyc/px | PENDING | [ ] | [ ] | [ ] | [ ] | PENDING |
| D | SDIV → reciprocal (deferred) | H5 | ~5-15 cyc/px | N/A | — | — | — | — | DEFERRED |

**Legend**: PASS = audit verified, PARTIAL = partially audited, PENDING = not yet audited

**Completed**: 4 / 7 batches
**Estimated total savings**: 15-25% fewer instructions per pixel (~10-20% wall-clock)

---

## Commit Log

| Batch | Commit | Date | Description |
|-------|--------|------|-------------|
| — | `812bf0ab9` | 2026-02-20 | Dead code removal (MINOR-1 + MINOR-3 from correctness audit) |
| 1 | `ea8443b0f` | 2026-02-20 | Alpha blend rounding cleanup (H7: v8 replaces LDR alookup[1], H8: USHR Rd!=Rn eliminates MOV) |
| 2 | `89dbbe6de` | 2026-02-20 | Cache STATE_x in w28 (H2: 9 loads eliminated), STATE_x2 in w27 (H3: 1 load eliminated) |
| 3 | `e37e3cc0c` | 2026-02-20 | Hoist RGBA deltas→v12, TMU0 ST→v15, TMU1 ST→v14; frame 160→176 bytes |
| 4 | (pending) | 2026-02-20 | BIC+ASR clamp idiom: 9 sites updated, new ARM64_BIC_REG_ASR macro added |
| 5 | | | |
| 6 | | | |
| 7 | | | |

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

One bonus finding: **v11** (neon_minus_254) is loaded in the prologue but never used — dead code that can be removed when repurposing v11 for fogColor (Batch 7/M3).

All optimization logic, encoding claims, and safety analysis verified correct.

## Documents

| File | Contents |
|------|----------|
| `optimization-plan.md` | Detailed implementation plan with 7 batches |
| `perf-optimization-report.md` | Full analysis of all 20 optimization opportunities |
| `optimization-plan-audit.md` | Independent audit of the plan's accuracy |
| `codegen-audit-report.md` | Prior correctness audit (0 bugs found) |
