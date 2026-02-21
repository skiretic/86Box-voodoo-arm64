# ARM64 Voodoo JIT Optimization Plan

**Date**: 2026-02-20
**Based on**: `perf-optimization-report.md`
**Audited by**: `optimization-plan-audit.md`
**Constraint**: ARMv8.0-A only, bit-identical output, accuracy first

---

## Approach

Slow and methodical. One batch at a time. Each batch follows this cycle:

1. **Implement** — subagent makes changes
2. **Build** — `./scripts/build-and-sign.sh`
3. **Test** — manual testing in specified game/benchmark
4. **Verify** — subagent audit of changes if needed
5. **Commit** — only after test passes
6. **Next batch** — only after commit

If any batch causes visual artifacts or crashes, revert immediately and investigate before proceeding. No batch depends on a prior batch — they are independent and can be skipped or reordered.

---

## Batch 1 — Trivial / Zero Risk (H7 + H8)

**Alpha blend rounding cleanup — ~22 cycles/pixel saved**

**Audit status**: Fully verified — 14 sites confirmed, v8 = alookup[1] confirmed, USHR Rd!=Rn valid

- **H7**: Replace all `LDR d16, [x20, #16]` (loading `alookup[1]` = `{1,1,1,1}`) with direct use of the already-pinned v8 register. The values are identical by construction. Exactly 14 sites across dest_afunc and src_afunc.
- **H8**: In the same 14 rounding sequences, change `MOV v17, v4` + `USHR v17, v17, #8` to just `USHR v17, v4, #8`. USHR supports different Rd/Rn — the MOV is unnecessary. 7 sites use v4 source, 7 use v0 source.

**Files**: `src/include/86box/vid_voodoo_codegen_arm64.h`
**Lines**: ~3700-3880 (dest_afunc and src_afunc blend factor computation)
**Complexity**: Trivial — mechanical replacement, no logic changes
**Test**: Any alpha-blended scene (Quake 3 menu, UT99 in-game)

---

## Batch 2 — Low Complexity / Low Risk (H2 + H3)

**Cache STATE_x and STATE_x2 in callee-saved GPRs — ~20-36 cycles/pixel saved**

**Audit status**: Fully verified — 9 STATE_x loads confirmed, STATE_x2 never written in loop

- **H2**: Load STATE_x once at the top of the loop body into a callee-saved GPR (w28). Compute x_tiled once. Replace all subsequent STATE_x and STATE_x_tiled loads (up to 9 per pixel) with the cached register. STATE_x is only written at the very end of the loop (line 4310).
- **H3**: Load STATE_x2 (loop termination bound) into a callee-saved GPR (w27) in the prologue. Replace the per-iteration `LDR w6, [x0, #STATE_x2]` + `CMP` with just `CMP w28, w27`.

**Files**: `src/include/86box/vid_voodoo_codegen_arm64.h`
**Lines**: Prologue (~1950-2000), loop body (2074, 2128, 2333, 2336, 3642, 3655, 3942, 3978, 4122, 4301, 4313-4314)
**Complexity**: Low — register allocation change, multiple call sites
**Test**: Full scene with depth testing + alpha blend (Quake 3 gameplay, 3DMark99)

---

## Batch 3 — Medium Complexity / Low Risk (H1)

**Hoist loop-invariant params delta loads to prologue — ~20-36 cycles/pixel saved**

**Audit status**: Line numbers verified (~4186-4270), logic confirmed correct. Needs careful register assignment.

- Load `{dBdX, dGdX, dRdX, dAdX}` into callee-saved NEON register v12 in the prologue
- Load TMU0/TMU1 deltas into v15 (or split across available registers)
- Replace all per-pixel delta loads in the increment section (lines ~4186-4270) with the pinned registers
- Extend prologue STP/LDP to save/restore v14/v15 (v10-v13 already saved)

**Files**: `src/include/86box/vid_voodoo_codegen_arm64.h`
**Lines**: Prologue (~1930-2000), per-pixel increments (~4186-4270)
**Complexity**: Medium — prologue/epilogue changes, register pressure analysis needed
**Test**: Textured scene with perspective correction (Quake 3, Turok demo)

---

## Batch 4 — Low Complexity / Low Risk (M2)

**BIC+ASR clamp idiom — ~16 cycles/pixel saved**

**Audit status**: Math verified correct. Requires new `ARM64_BIC_REG_ASR` encoding macro. Line numbers slightly off (~5 lines) but patterns confirmed present.

- Replace the 5-instruction `CMP #0 / CSEL LT / MOVZ #0xFF / CMP / CSEL HI` clamp-to-[0,255] pattern with the 3-instruction ARM idiom:
  ```
  BIC  w14, w14, w14, ASR #31   // zero if negative
  CMP  w14, #255
  CSEL w14, w10, w14, HI        // cap at 255
  ```
- Applies to ~8 sites: alpha select, alpha local, fog alpha, alpha combine result, TCA clamp, etc.
- Pre-load `w10 = 0xFF` once before the clamp sequences (check if already available from prior clamp)
- Need to add encoding macro:
  ```c
  #define ARM64_BIC_REG_ASR(d, n, m, shift) (0x0A200000 | (2 << 22) | (((shift) & 0x3F) << 10) | Rm(m) | Rn(n) | Rd(d))
  ```

**Files**: `src/include/86box/vid_voodoo_codegen_arm64.h`
**Lines**: ~2847, ~3022, ~3057, ~3075, ~3247, ~3286, ~3298, ~3484 (approximate — verify before editing)
**Complexity**: Low — mechanical pattern replacement + one new macro
**Test**: Scenes with alpha testing/blending (Quake 3, UT99 translucent effects)

---

## Batch 5 — Trivial-Low / Zero Risk (M4 + M7)

**Pin rgb565 pointer + batch pixel/texel count — ~8 cycles/pixel saved**

**Audit status**: Not yet audited in detail. Low risk by nature.

- **M4**: Load the `rgb565` lookup table pointer into callee-saved register x26 in the prologue. Replace the per-pixel 4-instruction MOVZ+MOVK sequence in the alpha blend path with the register.
- **M7**: Replace separate LDR/ADD/STR for pixel_count and texel_count with paired LDP/ADD/ADD/STP. Requires adding `ARM64_LDP_OFF_W` and `ARM64_STP_OFF_W` encoding macros. Fields are adjacent at offsets 552 and 556.

**Files**: `src/include/86box/vid_voodoo_codegen_arm64.h`
**Lines**: Prologue, ~3675-3678 (rgb565), ~4277-4291 (counters)
**Complexity**: Trivial (M4), Low (M7 — new encoding macros)
**Test**: Any scene with alpha blending

---

## Batch 6 — Low-Medium / Low Risk (H4 + H6)

**Cache LOD + iterated color BGRA — ~9-16 cycles/pixel saved**

**Audit status**: H4 store-then-reload confirmed. H6 register reassigned to v14 (v10 is in use). Needs further audit before implementation.

- **H4**: After storing LOD to STATE_lod, keep the value in a scratch register. Eliminate the redundant reload 12-60 instructions later in both bilinear and point-sample paths.
- **H6**: Compute the packed iterated BGRA (ib/ig/ir/ia → SSHR → SQXTN → SQXTUN) once at the start of the color combine phase. Cache in callee-saved NEON register v14. Reuse for chroma key, color local select, color other select (up to 4 identical 5-instruction sequences eliminated).

**Files**: `src/include/86box/vid_voodoo_codegen_arm64.h`
**Lines**: ~1352-1474 (H4, texture fetch), ~2949-3165 (H6, color combine)
**Complexity**: Low (H4), Medium (H6 — need to identify earliest use and ensure no clobber)
**Test**: Textured + Gouraud shaded scenes (Quake 3, 3DMark)

---

## Batch 7 — Various Small Wins (M1 + M3 + M5 + M6 + L1 + L2)

**Miscellaneous — ~16-22 cycles/pixel saved**

**Audit status**: PASS — audited, all claims verified (M6 adjusted from 5→6 insns, L1 reduced to 1 site). See `batch7-audit.md`.

- **M1**: Use LDP for adjacent 32-bit loads where both come from the same base register
- **M3**: Hoist `params->fogColor` load+unpack (LDR+FMOV+UXTL) to prologue into v11 (replacing dead neon_minus_254). Constant per triangle.
- **M5**: Eliminate redundant `FMOV w5, s7` + `LSR w5, w5, #24` in dual-TMU TCA path (extract TMU0 alpha once, reuse)
- **M6**: Use BFI for no-dither RGB565 packing (5-6 insns instead of 7)
- **L1**: Replace stray `CMP #0` + `B.EQ/B.NE` with `CBZ/CBNZ` where applicable
- **L2**: Eliminate `MOV w11, w7` before `LSL w5, w5, w11` in texture fetch (use w7 directly)

**Files**: `src/include/86box/vid_voodoo_codegen_arm64.h`
**Complexity**: Low across the board
**Test**: Full regression — Quake 3, UT99, 3DMark99

---

## Bonus — Dead Code Removal (v11 neon_minus_254)

**Remove dead prologue load — saves 4 instructions in prologue**

- v11 (neon_minus_254) is loaded in the prologue but never read in any generated code path
- Remove the 4-instruction MOVZ+MOVK sequence that materializes the neon_minus_254 pointer
- Remove the LDR that loads the constant into v11
- Can be done as part of Batch 7 (M3) since M3 repurposes v11 for fogColor

---

## Deferred — Phase 8+ (H5)

**SDIV → Newton-Raphson reciprocal approximation**

- Replace the 7-13 cycle SDIV in perspective W division with FRECPE+FRECPS+integer conversion
- **NOT SAFE** for implementation now — floating-point reciprocal cannot guarantee bit-identical truncation semantics with integer SDIV
- Requires exhaustive testing across the full W range after all other optimizations are proven correct
- Potential savings: 5-15 cycles/pixel when perspective correction is active

---

## Not Implemented (rejected)

| ID | Reason |
|----|--------|
| NR1 | SMLAL doesn't help — combine needs intermediate shift before accumulate |
| NR2 | SQRDMULH requires ARMv8.1 |
| NR3 | Computed bilinear weights (~8 insns) slower than table lookup (1 LDR) |
| NR4 | ADRP+ADD unreliable for cross-segment JIT references (ASLR, >4GB gap) |
| NR5 | Loop unrolling — too complex with conditional pipeline stages, code size risk |
| L3 | CSINC for reverse blend +1 — unconditional ADD, no savings |
| L4 | MOVI zero before SUB — needed as negation base, not dead |

---

## Register Budget After All Optimizations

### GPR (callee-saved)
| Register | Current Use | After Optimization |
|----------|------------|-------------------|
| x19 | logtable pointer | logtable pointer |
| x20 | alookup pointer | alookup pointer |
| x21 | aminuslookup pointer | aminuslookup pointer |
| x22 | neon_00_ff_w pointer | neon_00_ff_w pointer |
| x23 | i_00_ff_w pointer | i_00_ff_w pointer |
| x24 | reserved | reserved |
| x25 | bilinear_lookup pointer | bilinear_lookup pointer |
| x26 | available | **rgb565 pointer (M4)** |
| x27 | available | **STATE_x2 loop bound (H3)** |
| x28 | available | **STATE_x cache (H2)** |

### NEON (callee-saved, d8-d15)
| Register | Current Use | After Optimization |
|----------|------------|-------------------|
| v8 | neon_01_w (pinned) | neon_01_w (pinned) — **also used as alookup[1] (H7)** |
| v9 | neon_ff_w (pinned) | neon_ff_w (pinned) |
| v10 | neon_ff_b (cc_invert) | neon_ff_b (cc_invert) — **IN USE, do not reassign** |
| v11 | neon_minus_254 (DEAD — loaded but never read) | **fogColor unpacked (M3)** — remove dead load |
| v12 | available | **{dBdX, dGdX, dRdX, dAdX} (H1)** |
| v13 | color-before-fog (ACOLORBEFOREFOG blend) | color-before-fog — **IN USE, do not reassign** |
| v14 | available | **iterated BGRA cache (H6)** |
| v15 | available | **texture deltas or additional H1 deltas** |

### Stack Frame
- Current: 160 bytes (9 register pairs + 16 padding)
- After: ~192 bytes (11 register pairs + 16 padding) — adds v14/v15 saves (v10-v13 already saved)
- Cost: 1 extra STP in prologue, 1 extra LDP in epilogue — amortized over full span
- Bonus: removing dead v11 (neon_minus_254) load saves 4 prologue instructions

---

## Estimated Aggregate Impact

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| Instructions/pixel (textured+blended) | ~250-350 | ~200-280 | -15-25% |
| Wall-clock estimate | baseline | ~10-20% faster | improvement |
| Code size per block | ~same | ~same (fewer loop insns, slightly larger prologue) | neutral |
| Stack frame | 160 bytes | ~192 bytes | +32 bytes |
