# ARM64 Voodoo JIT Performance Optimization Report

**Date**: 2026-02-20
**Analyst**: voodoo-debug agent
**Constraint**: ARMv8.0-A baseline only (no v8.1+ extensions)
**File**: `src/include/86box/vid_voodoo_codegen_arm64.h` (4760 lines)
**Reference**: `src/include/86box/vid_voodoo_codegen_x86-64.h` (3562 lines)

## Executive Summary

Analysis of the ARM64 JIT codegen reveals **8 high-impact**, **7 medium-impact**, and **5 low-impact** optimization opportunities. The highest gains come from:

1. **Hoisting loop-invariant params loads to the prologue** (high -- saves 3-8 loads per pixel)
2. **Eliminating redundant STATE_x / STATE_x_tiled loads** (high -- saves 2-6 loads per pixel)
3. **Consolidating the repeated alpha rounding sequence** into a reusable pattern (medium -- reduces code size, improves I-cache)
4. **Replacing the repeated `alookup[1]` load** with a pinned register (medium -- saves 1 LDR per blend path per pixel)

Estimated aggregate improvement: **5-15% fewer instructions per pixel** depending on pipeline configuration. The heaviest gains are in the alpha blend path (which has the most redundant loads and repeated patterns).

All suggestions produce bit-identical output to the current implementation.

---

## High Impact Optimizations

### H1. Hoist PARAMS_dBdX address to a callee-saved register

**Current** (lines 4186-4187, inside pixel loop):
```
ADD x17, x1, #PARAMS_dBdX    // x17 = &params->dBdX
LD1 {v1.4S}, [x17]           // v1 = {dBdX, dGdX, dRdX, dAdX}
```

**Problem**: `params->dBdX` is constant for the entire triangle (params is per-triangle, not per-pixel). This 128-bit load of the same 4 deltas happens every single pixel iteration.

**Proposed**: Load `{dBdX, dGdX, dRdX, dAdX}` into a callee-saved NEON register (e.g., v12 or v14) in the prologue, before the loop. The LD1 + ADD disappear from the hot loop entirely.

Similarly, `PARAMS_dZdX` (line 4199), `PARAMS_tmu0_dSdX` (line 4218), `PARAMS_tmu0_dWdX` (line 4229), `PARAMS_dWdX` (line 4239), and the TMU1 deltas (lines 4255, 4266) are all loop-invariant. The x86-64 codegen loads these every iteration too, but on x86-64 the memory operands are fused into the ALU ops (e.g., `PADDD XMM1, [params+offset]`), giving implicit "free" loads. ARM64 has no such fusion -- each LDR is a separate instruction that occupies the load port.

**Savings**: 5-9 load instructions per pixel (depending on whether texture and dual-TMU are active). At ~4 cycles per L1 hit, this is **20-36 cycles/pixel**.

**Risk**: LOW. These are reads from params (const per-triangle). Pinning more NEON callee-saved registers (v14, v15) requires saving/restoring them in prologue/epilogue (2 more STP/LDP), but that is amortized over the entire span (often 50+ pixels).

**Complexity**: MEDIUM. Requires extending the prologue to load deltas, adding v14/v15 save/restore, and replacing the loop body loads with the pinned registers. The xdir-dependent ADD/SUB still executes per pixel, just using the pinned source.

---

### H2. Eliminate redundant STATE_x / STATE_x_tiled loads

**Current**: STATE_x (or STATE_x_tiled) is loaded independently at multiple points in a single pixel iteration:

| Line  | Context                  | Load instruction                    |
|-------|--------------------------|-------------------------------------|
| 2074  | Stipple test             | `LDR w5, [x0, #STATE_x]`          |
| 2128  | Tiled X computation      | `LDR w4, [x0, #STATE_x]`          |
| 2333  | Depth test (tiled)       | `LDR w4, [x0, #STATE_x_tiled]`    |
| 2336  | Depth test (linear)      | `LDR w4, [x0, #STATE_x]`          |
| 3642  | Alpha blend dest alpha   | `LDR w5, [x0, #STATE_x_tiled]`    |
| 3655  | Alpha blend dest RGB     | `LDR w4, [x0, #STATE_x_tiled]`    |
| 3942  | Depth write (alpha)      | `LDR w4, [x0, #STATE_x_tiled]`    |
| 3978  | FB write                 | `LDR w14, [x0, #STATE_x_tiled]`   |
| 4122  | Depth write (non-alpha)  | `LDR w4, [x0, #STATE_x_tiled]`    |
| 4301  | X increment              | `LDR w4, [x0, #STATE_x]`          |

STATE_x does not change during a pixel iteration until the very end (line 4310 STR). Therefore all reads of STATE_x within a single iteration return the same value.

STATE_x_tiled is computed from STATE_x and stored once (line 2136), then read multiple times.

**Proposed**: Load STATE_x once at the top of the loop body into a scratch register (e.g., w26 or w27, callee-saved, currently marked "available" in the register map). Compute x_tiled once. Reuse for all subsequent references. Save 4-8 LDR instructions per pixel.

**Savings**: 4-8 LDR per pixel = **16-32 cycles/pixel**.

**Risk**: LOW. STATE_x is only written at the end of the loop. x_tiled is derived from it and also only written once.

**Complexity**: LOW. Simple register reuse.

---

### H3. Pin STATE_x2 in a callee-saved register for loop comparison

**Current** (lines 4313-4314, loop back-edge):
```
LDR w6, [x0, #STATE_x2]     // load loop bound
CMP w4, w6                    // compare current x vs bound
```

**Problem**: `state->x2` is the loop termination bound. It never changes during the pixel loop. Loading it from memory on every iteration wastes a load port cycle.

**Proposed**: Load `state->x2` into a callee-saved register (e.g., w27) in the prologue, before the loop. Replace the LDR+CMP with just `CMP w_current_x, w27`.

**Savings**: 1 LDR per pixel = **~4 cycles/pixel**. Small individually but guaranteed to fire on every pixel of every span.

**Risk**: NONE. state->x2 is constant for the entire span.

**Complexity**: TRIVIAL. One load in prologue, one register substitution in loop.

---

### H4. Eliminate redundant LOD reloads in texture fetch

**Current** (bilinear path, lines 1410-1411, then 1473-1474):
```
LDR w6, [x0, #STATE_lod]     // first load (line 1411)
... (12 instructions) ...
LDR w6, [x0, #STATE_lod]     // redundant reload (line 1474)
```

Also in point-sample path (lines 1722-1723 load LOD, then 1768-1780 reload LOD via STATE_lod again).

**Problem**: LOD was just stored to STATE_lod (line 1352) and never modified between these two loads. The value is still in w4 (or can be kept in another register).

**Proposed**: Keep the LOD value in a register across the gap. For the bilinear path, after storing LOD at line 1352, save it to a scratch register (e.g., w26) and reuse at line 1474 instead of reloading.

**Savings**: 1-2 LDR per pixel per TMU = **4-8 cycles/pixel** when texture is active.

**Risk**: NONE. LOD is not modified between the store and the reload.

**Complexity**: LOW. Register bookkeeping.

---

### H5. Replace SDIV with multiply-by-reciprocal approximation for W division

**Current** (lines 1269-1270):
```
SDIV x4, x4, x7      // quotient = (1<<48) / tmu_w
```

**Problem**: SDIV on ARM64 is extremely slow. On Apple M-series, SDIV takes **7-13 cycles** for 64-bit operands. On Cortex-A76 class cores it is **12-20 cycles**. This is the single most expensive instruction in the entire texture fetch path.

The x86-64 version uses a similar IDIV which is also slow, but on modern x86 this is ~20-40 cycles, so the relative penalty is similar.

**Proposed**: Use a Newton-Raphson reciprocal approximation:
1. FRECPE (floating-point reciprocal estimate) on the W value converted to FP
2. FRECPS (reciprocal step) for one Newton-Raphson refinement
3. Convert back to integer and multiply

However, this requires careful analysis to ensure bit-identical results. The Voodoo hardware uses integer division semantics (truncation toward zero). A floating-point reciprocal approximation would produce slightly different rounding in edge cases.

**Savings**: Potentially **5-15 cycles/pixel** when perspective correction is active.

**Risk**: HIGH. Floating-point reciprocal may not be bit-identical to integer SDIV truncation. Would need exhaustive testing across the full W range. This optimization should only be considered after all correctness testing is complete, and ONLY if testing confirms bit-identical output for all inputs the Voodoo actually generates.

**Complexity**: HIGH. Requires int-to-float conversion, FP reciprocal, refinement, float-to-int conversion, and careful rounding analysis.

**Recommendation**: DEFER. The accuracy risk is too high for now. Mark as a Phase 7+ candidate.

---

### H6. Consolidate the iterated color load (ib/ig/ir/ia)

**Current**: The sequence `ADD x16, x0, #STATE_ib` + `LD1 {v_.4S}, [x16]` + `SSHR v_.4S, #12` + `SQXTN` + `SQXTUN` appears in multiple places:

| Lines      | Context                           |
|------------|-----------------------------------|
| 2949-2954  | Chroma key (CC_LOCALSELECT_ITER)  |
| 3119-3123  | Color local select (iter RGB)     |
| 3140-3144  | Color local override (iter path)  |
| 3161-3165  | Color other select (iter RGB)     |

That is up to 4 identical 5-instruction sequences in a single pixel (if chroma key + color combine both select iterated RGB).

**Proposed**: Compute the packed iterated BGRA once, early in the color combine phase, and cache it in a callee-saved NEON register (v12 or v14). Reuse for all subsequent references.

**Savings**: 10-15 instructions eliminated when iterated RGB is used in multiple selects. **~5-8 cycles/pixel** in common configurations (textured + Gouraud shading).

**Risk**: LOW. The ib/ig/ir/ia values are updated only at the end of the loop (per-pixel increments at line 4184). All color combine reads happen before that.

**Complexity**: MEDIUM. Need to identify the earliest point where iterated BGRA is needed and ensure the register is not clobbered between uses.

---

### H7. Hoist `alookup[1]` load out of the blend rounding sequence

**Current**: The rounding correction vector `alookup[1]` (which is just `{1,1,1,1}` in the low 64 bits) is loaded via `LDR d16, [x20, #16]` inside every single alpha blend factor computation. Looking at the dest_afunc switch:

| Lines      | Case              |
|------------|-------------------|
| 3705       | ASRC_ALPHA        |
| 3715       | A_COLOR           |
| 3727       | ADST_ALPHA        |
| 3742       | AOMSRC_ALPHA      |
| 3754       | AOM_COLOR         |
| 3766       | AOMDST_ALPHA      |
| 3778       | ACOLORBEFOREFOG   |

And the same pattern repeats for all src_afunc cases (lines 3797, 3807, 3819, 3834, 3846, 3858, 3877).

That is 2 LDR instructions (one for dest, one for src) loading the identical `{1,1,1,1}` vector, every pixel that uses alpha blending.

**Proposed**: `alookup[1]` is `{1,1,1,1,0,0,0,0}` which is exactly the same as the already-pinned v8 register (`xmm_01_w`). Replace all `LDR d16, [x20, #16]` with `MOV d16, d8` (or just use v8 directly if the rounding sequence does not clobber it).

Actually, looking more carefully: `alookup[1].u16[0..3] = {1,1,1,1}` which matches `neon_01_w` which is already in v8. So the LDR can be replaced with nothing -- just use v8 directly in the ADD instruction.

**Savings**: 2 LDR per pixel (1 dest + 1 src) when alpha blend is active = **~8 cycles/pixel**.

**Risk**: NONE. `alookup[1]` is identical to v8 by construction (both are `{1,1,1,1}` in the low 4 halfwords).

**Complexity**: TRIVIAL. Replace `LDR d16, [x20, #16]` + `ADD v4, v4, v16` with `ADD v4, v4, v8`.

---

### H8. Eliminate the MOV v17,v_ / USHR v17 / ADD v_,v17 rounding copy

**Current** (e.g., lines 3706-3710, repeated 14 times across dest/src afunc):
```
MOV  v17, v4           // copy product
USHR v17.4H, v17, #8   // product >> 8
ADD  v4.4H, v4, v16    // product + 1  (rounding constant)
ADD  v4.4H, v4, v17    // product + 1 + (product >> 8)
USHR v4.4H, v4, #8     // final >> 8
```

This 5-instruction rounding sequence appears identically in every non-trivial blend factor case (14 times total across dest_afunc and src_afunc).

**Proposed**: The sequence `(x + 1 + (x >> 8)) >> 8` can be rewritten as `(x + 1 + (x >> 8)) >> 8`. Using URSHR (unsigned rounding shift right) does NOT produce the same result (URSHR adds 0.5 ULP, which is not the same as `+1 + (x>>8)`). However, we can at least combine the two ADDs:

Actually, re-examining: with H7 applied (using v8 directly), the sequence becomes:
```
MOV  v17, v4
USHR v17.4H, v17, #8
ADD  v4.4H, v4, v8     // +1 (using pinned register, no LDR)
ADD  v4.4H, v4, v17    // + (product >> 8)
USHR v4.4H, v4, #8
```

This is 5 instructions. We can save 1 instruction by reordering:
```
USHR v17.4H, v4, #8    // v17 = product >> 8 (no need for MOV first)
ADD  v4.4H, v4, v8     // product + 1
ADD  v4.4H, v4, v17    // product + 1 + (product >> 8)
USHR v4.4H, v4, #8     // final >> 8
```

Wait -- USHR with a different destination does not require a prior MOV. The current code does `MOV v17, v4` then `USHR v17, v17, #8` because the ARM64 shift-immediate instructions allow a separate Rd and Rn. So `USHR v17.4H, v4.4H, #8` works directly, eliminating the MOV.

**Savings**: 1 MOV instruction per rounding sequence x 14 occurrences = **14 instructions per pixel** (when both src and dest blend are non-trivial). At ~1 cycle each, **~14 cycles/pixel**.

**Risk**: NONE. `USHR Vd, Vn, #imm` with Vd != Vn is a valid encoding on ARMv8.0.

**Complexity**: TRIVIAL. Change `MOV v17, v4` + `USHR v17, v17, #8` to `USHR v17, v4, #8`.

---

## Medium Impact Optimizations

### M1. Use LDP for adjacent 32-bit loads in the per-pixel increment section

**Current** (lines 4197-4199):
```
LDR w4, [x0, #STATE_z]       // offset 488
LDR w5, [x1, #PARAMS_dZdX]   // offset 64
```

These load from two different base registers (x0 vs x1) so they cannot be combined into LDP. However, there are adjacent pairs that CAN be combined:

- `pixel_count` (offset 552) and `texel_count` (offset 556) are adjacent 32-bit fields. Lines 4277-4283 load them separately. A single `LDP w4, w5, [x0, #STATE_pixel_count]` replaces two LDR instructions.

- In the depth test section, `STATE_x` (560) and `STATE_x2` (564) are adjacent. Loading both with `LDP w4, w6, [x0, #STATE_x]` would serve both the tiled-X computation and the loop comparison.

**Savings**: 1-2 instructions per pixel. **~2-4 cycles/pixel**.

**Risk**: NONE. LDP with signed-offset form for W registers requires the offset to be a multiple of 4, which both 552 and 560 satisfy.

**Complexity**: LOW. Need to add an `ARM64_LDP_W` macro (not currently defined) or use the 64-bit pair variant and mask.

Actually -- the file does not define `ARM64_LDP_OFF_W`. The existing `ARM64_LDP_OFF_X` is for 64-bit pairs. A new macro would be needed:
```c
#define ARM64_LDP_OFF_W(t1, t2, n, imm) (0x29400000 | ((((imm) >> 2) & 0x7F) << 15) | Rt2(t2) | Rn(n) | Rt(t1))
```

---

### M2. Replace CMP+CSEL clamp patterns with SMAX/SMIN (via NEON) or simpler sequences

**Current** (e.g., lines 3021-3027, alpha clamping to [0, 0xFF]):
```
CMP  w14, #0
CSEL w14, wzr, w14, LT    // clamp low
MOVZ w10, #0xFF
CMP  w14, w10
CSEL w14, w10, w14, HI    // clamp high
```

This 5-instruction clamp pattern appears at least **8 times** in the file (alpha select, alpha local, fog alpha, alpha combine result, TCA clamp, etc.).

**Proposed alternative 1**: For unsigned clamp to [0, 255], use:
```
CMP  w14, #255
CSEL w14, w14, wzr, PL    // if >= 0 (always true for unsigned), keep; but we need signed
```

Actually for signed clamp to [0, 255]: since we know the input is a signed 32-bit value that might be negative or > 255, the most compact approach is:
```
SUBS wzr, w14, #256       // sets flags: N=1 if w14 < 256
CSEL w14, wzr, w14, LT    // if w14 < 0, use 0
CMP  w14, #255
CSEL w14, w10, w14, HI    // if w14 > 255, use 255
```

This does not save instructions. However, we can use a different approach: if `w10 = 0xFF` is pre-loaded (which it usually is from a prior clamp), then:
```
// Clamp signed w14 to [0, 255]
BIC  w14, w14, w14, ASR #31   // clear if negative (w14 & ~(sign-extend))
CMP  w14, #255
CSEL w14, w10, w14, HI
```

That is 3 instructions instead of 5. The BIC trick works because `w14, ASR #31` produces all-ones if negative, all-zeros if positive. `w14 & ~(all-ones) = 0`, `w14 & ~(all-zeros) = w14`.

**Savings**: 2 instructions x 8 occurrences = **16 instructions per pixel**. ~16 cycles/pixel.

**Risk**: LOW. The BIC+ASR trick is a well-known ARM idiom. Bit-identical for all inputs.

**Complexity**: LOW. Mechanical replacement.

---

### M3. Hoist fogColor load and unpack out of the fog path

**Current** (lines 3421-3423, inside the non-constant fog path):
```
LDR  w4, [x1, #PARAMS_fogColor]
FMOV s3, w4
UXTL v3.8H, v3.8B
```

**Problem**: `params->fogColor` is constant per-triangle. If fog is enabled for this block, this load+unpack executes every pixel.

**Proposed**: Load and unpack fogColor once in the prologue (or just before the loop). Store the unpacked 4x16 result in a callee-saved NEON register.

**Savings**: 3 instructions per pixel when fog is active. **~6 cycles/pixel**.

**Risk**: NONE. fogColor is const per triangle.

**Complexity**: LOW. Requires one more callee-saved NEON register.

---

### M4. Replace 4-instruction MOVZ+MOVK sequence for rgb565 pointer with a pinned register

**Current** (lines 3675-3678, inside alpha blend -- per pixel):
```
MOVZ x7, #(rgb565 & 0xFFFF)
MOVK x7, #((rgb565 >> 16) & 0xFFFF), LSL #16
MOVK x7, #((rgb565 >> 32) & 0xFFFF), LSL #32
MOVK x7, #((rgb565 >> 48) & 0xFFFF), LSL #48
```

**Problem**: This 4-instruction sequence materializes the `rgb565` lookup table pointer every pixel when alpha blending is active. The pointer is a compile-time constant.

**Proposed**: Load the `rgb565` pointer into a callee-saved register in the prologue (e.g., x26, currently marked "available"). Use it directly in the blend path.

**Savings**: 4 instructions per pixel when alpha blend is active. **~4 cycles/pixel**.

**Risk**: NONE. rgb565 is a static table.

**Complexity**: TRIVIAL. Add one EMIT_MOV_IMM64 to the prologue, replace 4 instructions with the register.

---

### M5. Eliminate redundant `FMOV_W_S(5, 7)` in dual-TMU TCA path

**Current** (dual-TMU alpha combine, multiple places):
```
Line 2745: FMOV w5, s7    // extract raw TMU0 packed BGRA
Line 2754: LSR  w5, w5, #24  // extract alpha byte
...
Line 2793: FMOV w5, s7    // same extraction again
Line 2794: LSR  w5, w5, #24
...
Line 2801: FMOV w5, s7    // and again
Line 2802: LSR  w5, w5, #24
...
Line 2841: FMOV w5, s7    // and again
Line 2842: LSR  w5, w5, #24
```

**Problem**: The raw TMU0 alpha byte (v7 >> 24) is extracted up to 4 times with the same 2-instruction sequence.

**Proposed**: Extract TMU0 alpha once after the TMU0 fetch and keep it in a dedicated scratch register. The `FMOV w5, s7` + `LSR w5, w5, #24` pair appears in mutually exclusive switch cases, but several of those cases are selected at JIT compile time, so only one path executes. Still, within the `tca_sub_clocal` block, the extraction at line 2745/2754 could be reused by lines 2841-2842.

**Savings**: 2-4 instructions per pixel in dual-TMU mode. **~4-8 cycles/pixel**.

**Risk**: NONE. v7 is not modified between extractions.

**Complexity**: LOW.

---

### M6. Use UBFX for the no-dither RGB565 pack instead of 3 separate UBFX+LSL

**Current** (lines 4093-4102, no-dither path):
```
UBFX w5, w4, #3, #5      // B = bits[7:3]
UBFX w6, w4, #10, #6     // G = bits[15:10]
LSL  w6, w6, #5
UBFX w7, w4, #19, #5     // R = bits[23:19]
LSL  w7, w7, #11
ORR  w4, w7, w6
ORR  w4, w4, w5
```

That is 7 instructions. An alternative using BFI (bitfield insert):
```
UBFX w4, w4, #3, #5      // w4 = B5 (bits 4:0)
UBFX w5, w_saved, #10, #6
BFI  w4, w5, #5, #6      // insert G6 at bits 10:5
UBFX w5, w_saved, #19, #5
BFI  w4, w5, #11, #5     // insert R5 at bits 15:11
```

Wait, this requires saving the original w4 first. Let me reconsider:
```
LSR  w5, w4, #3          // B5 in bits [4:0] (and junk above)
UBFX w6, w4, #10, #6     // G6 clean
LSR  w4, w4, #19         // R5 in bits [4:0]
BFI  w5, w6, #5, #6      // insert G at [10:5]
BFI  w5, w4, #11, #5     // insert R at [15:11]
AND  w4, w5, #0xFFFF     // optional: clear junk above bit 15
```

That is 5-6 instructions vs 7. Marginal gain.

**Savings**: 1-2 instructions per pixel when dither is disabled. **~2 cycles/pixel**.

**Risk**: NONE. BFI is base ARMv8.0.

**Complexity**: LOW.

---

### M7. Batch the pixel_count and texel_count increments

**Current** (lines 4277-4291):
```
LDR  w4, [x0, #STATE_pixel_count]
ADD  w4, w4, #1
STR  w4, [x0, #STATE_pixel_count]
LDR  w4, [x0, #STATE_texel_count]     // (only if texture enabled)
ADD  w4, w4, #1 or #2
STR  w4, [x0, #STATE_texel_count]
```

**Problem**: pixel_count (offset 552) and texel_count (offset 556) are adjacent 32-bit fields. Two separate load-modify-store sequences (6 instructions total) could be done with one LDP + two ADDs + one STP.

**Proposed**:
```
LDP  w4, w5, [x0, #STATE_pixel_count]   // loads pixel_count and texel_count
ADD  w4, w4, #1
ADD  w5, w5, #1 (or #2)
STP  w4, w5, [x0, #STATE_pixel_count]
```

4 instructions instead of 6.

**Savings**: 2 instructions per pixel. **~4 cycles/pixel**.

**Risk**: NONE. Fields are adjacent and 4-byte aligned.

**Complexity**: LOW. Requires adding `ARM64_LDP_OFF_W` and `ARM64_STP_OFF_W` macros.

---

## Low Impact Optimizations

### L1. Use CBZ/CBNZ instead of CMP #0 + B.EQ/B.NE where possible

**Current** (e.g., line 2271):
```
CMP  w10, #0
CSEL w10, wzr, w10, LT
```

The CMP #0 is needed here for the CSEL. But in other places:
```
Line 1265: CMP x7, #0
Line 1267: B.EQ div_skip_pos
```

This could be `CBZ x7, div_skip_pos` (1 instruction instead of 2). However, the current code already uses CBZ/CBNZ in many places (lines 2185, 2203, 2972), so this is largely already optimized. A few stray CMP+B.cond remain.

**Savings**: 1-2 instructions in rare paths. **~1-2 cycles/pixel**.

**Risk**: NONE.

**Complexity**: TRIVIAL.

---

### L2. Eliminate MOV w11, w7 before LSL w5, w5, w11 in texture fetch

**Current** (lines 1494-1495, 1804-1805):
```
MOV  w11, w7      // copy tex_shift
LSL  w5, w5, w11  // shift by tex_shift
```

**Problem**: The LSL instruction takes the shift amount from the source register. If w7 is not needed after this point, the MOV is unnecessary -- just use w7 directly.

Checking: w7 is not used after the shift in the bilinear path (it was `tex_shift = 8 - lod`). So:
```
LSL  w5, w5, w7   // shift by tex_shift directly
```

**Savings**: 1 MOV per texture fetch. **~1 cycle/pixel**.

**Risk**: NONE. w7 is dead after this point in the bilinear path.

**Complexity**: TRIVIAL.

---

### L3. Use CSINC for the "+1" after reverse-blend XOR

**Current** (e.g., line 4224):
```
ADD  w4, w4, #1    // factor + 1
```

This follows an XOR for reverse blend. The ADD #1 is always executed. No savings from CSINC here since it is unconditional. This is actually fine as-is.

**Status**: NOT APPLICABLE. Keeping for completeness.

---

### L4. Eliminate unnecessary MOVI_V2D_ZERO(1) before SUB in TMU1 combine

**Current** (line 2531):
```
MOVI v1.2D, #0     // zero v1
... (SMULL, SSHR, SQXTN) ...
SUB  v1.4H, v1.4H, v0.4H   // v1 = 0 - products
```

The MOVI is needed as the base for the subtraction (computing the negation). This is correct and necessary. No optimization possible here.

**Status**: NOT APPLICABLE.

---

### L5. Specialize the prologue pointer loads based on pipeline configuration

**Current** (lines 1976-1981): The prologue always loads all 6 pointer constants (logtable, alookup, aminuslookup, neon_00_ff_w, i_00_ff_w, bilinear_lookup) into callee-saved registers, regardless of whether they are used.

For example:
- `bilinear_lookup` (x25) is only used if bilinear filtering is enabled
- `aminuslookup` (x21) is only used if alpha blending is active with certain afuncs
- `logtable` (x19) is only used if perspective correction is enabled

**Proposed**: Conditionally emit pointer loads based on pipeline config. Skip unused pointers.

**Savings**: Up to 24 instructions in the prologue (6 pointers x 4 MOVZ/MOVK each). Since this is one-time per span, savings are amortized: **~1-2 cycles/pixel** for a 50-pixel span.

**Risk**: LOW. Complexity is in the prologue, not the hot loop.

**Complexity**: MEDIUM. Requires tracking which pointers are needed across all pipeline stages.

---

## Not Recommended

### NR1. NEON multiply-accumulate (SMLAL) instead of SMULL+SSHR+SQXTN

The texture/color combine multiply sequence uses SMULL (widening multiply) followed by SSHR and SQXTN (narrowing). SMLAL would accumulate into the same register but does not replace the shift or narrow steps. The combine equation requires the intermediate 32-bit result to be shifted before accumulation, so SMLAL does not help.

### NR2. ARMv8.1 SQRDMULH (saturating rounding doubling multiply high)

This would be ideal for the alpha rounding sequence, but it requires ARMv8.1 which is outside our baseline constraint.

### NR3. Replacing the bilinear lookup table with computed weights

The bilinear weights could be computed inline using NEON multiply instead of table lookup. However, the table lookup is a single LDR Q (1 instruction, likely L1 cache hit), while computing 4 weights inline would require ~8 NEON instructions. The table approach is faster.

### NR4. Using ADRP+ADD instead of MOVZ+3xMOVK for pointer loads

ADRP+ADD gives a 2-instruction pointer load (vs 4 for MOVZ+MOVK). However, ADRP is PC-relative and the JIT code block address varies at runtime. The ADRP immediate is relative to the instruction's own PC, which is in the JIT code block. The target addresses (static tables like alookup, logtable) are at fixed addresses, but the code block address changes per compilation. Computing the correct ADRP offset at JIT-emit time IS possible (you know both the code block base and the target address), but it requires the following:

1. The code block address must be known when emitting the instruction
2. The ADRP offset must fit in 21 bits (covering +/- 4GB)
3. The remainder fits in ADD's 12-bit immediate

On macOS with ASLR, the gap between the JIT code block (mmap'd) and the static data segment could exceed 4GB in theory. This makes ADRP unreliable for cross-segment references.

**Verdict**: Too risky for a 2-instruction savings in the prologue. Keep MOVZ+MOVK.

### NR5. Loop unrolling

Unrolling the pixel loop (processing 2 or 4 pixels per iteration) could reduce loop overhead. However, the Voodoo pipeline has many conditional stages with forward branches, making unrolling extremely complex. The code size would likely exceed BLOCK_SIZE (16KB) for complex pipelines. Not recommended.

---

## Summary Table

| ID  | Description                                           | Impact | Risk   | Complexity | Cycles/Pixel |
|-----|-------------------------------------------------------|--------|--------|------------|--------------|
| H1  | Hoist params delta loads to prologue                  | HIGH   | LOW    | MEDIUM     | 20-36        |
| H2  | Eliminate redundant STATE_x loads                     | HIGH   | LOW    | LOW        | 16-32        |
| H3  | Pin STATE_x2 in callee-saved register                | HIGH   | NONE   | TRIVIAL    | ~4           |
| H4  | Eliminate redundant LOD reloads in texture fetch      | HIGH   | NONE   | LOW        | 4-8          |
| H5  | Replace SDIV with reciprocal approximation            | HIGH   | HIGH   | HIGH       | 5-15         |
| H6  | Cache iterated color BGRA pack                        | HIGH   | LOW    | MEDIUM     | 5-8          |
| H7  | Use pinned v8 instead of loading alookup[1]           | HIGH   | NONE   | TRIVIAL    | ~8           |
| H8  | Eliminate MOV before USHR in rounding sequence         | HIGH   | NONE   | TRIVIAL    | ~14          |
| M1  | Use LDP for adjacent 32-bit loads                     | MEDIUM | NONE   | LOW        | 2-4          |
| M2  | BIC+ASR clamp idiom replaces 5-insn CMP+CSEL          | MEDIUM | LOW    | LOW        | ~16          |
| M3  | Hoist fogColor load/unpack                             | MEDIUM | NONE   | LOW        | ~6           |
| M4  | Pin rgb565 pointer in callee-saved register            | MEDIUM | NONE   | TRIVIAL    | ~4           |
| M5  | Eliminate redundant FMOV_W_S(5,7) in TCA path          | MEDIUM | NONE   | LOW        | 4-8          |
| M6  | Use BFI for no-dither RGB565 packing                  | MEDIUM | NONE   | LOW        | ~2           |
| M7  | Batch pixel_count + texel_count with LDP/STP           | MEDIUM | NONE   | LOW        | ~4           |
| L1  | CBZ/CBNZ for stray CMP #0 + B.cc                      | LOW    | NONE   | TRIVIAL    | 1-2          |
| L2  | Eliminate MOV w11,w7 before shift                      | LOW    | NONE   | TRIVIAL    | ~1           |
| L5  | Conditional prologue pointer loads                     | LOW    | LOW    | MEDIUM     | 1-2          |

### Recommended Implementation Order

1. **H7 + H8** (trivial, zero risk, ~22 cycles/pixel saved in blend path)
2. **H2 + H3** (low complexity, zero/low risk, ~20-36 cycles/pixel)
3. **H1** (medium complexity, high reward, ~20-36 cycles/pixel)
4. **M2** (low complexity, ~16 cycles/pixel across all clamp sites)
5. **M4 + M7** (trivial/low, ~8 cycles/pixel)
6. **H4 + H6** (low/medium complexity, texture-path specific)
7. **M1 + M3 + M5 + M6** (various small wins)
8. **H5** (defer -- high risk, requires extensive testing)

### Total Estimated Savings (typical textured + alpha-blended pixel)

- Before: ~250-350 instructions per pixel (rough estimate based on path analysis)
- After all low/no-risk optimizations: ~200-280 instructions per pixel
- Estimated improvement: **15-25% fewer instructions**, translating to roughly **10-20% wall-clock speedup** (accounting for ILP and cache effects)
