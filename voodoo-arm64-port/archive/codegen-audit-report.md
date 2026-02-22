# ARM64 Voodoo Codegen Comprehensive Audit Report

**Date**: 2026-02-20
**Auditor**: voodoo-debug agent
**File audited**: `src/include/86box/vid_voodoo_codegen_arm64.h` (4766 lines)
**Reference**: `src/include/86box/vid_voodoo_codegen_x86-64.h` (3562 lines)

---

## 1. Executive Summary

The ARM64 Voodoo JIT codegen is a thorough, well-documented port of the x86-64 pixel pipeline JIT. The code covers all pipeline stages: prologue/epilogue, stipple, tiled framebuffer, W/Z depth, depth test, texture fetch (LOD + bilinear + point-sample), dual-TMU combine, color combine, alpha combine, fog, alpha test, alpha blend, dither, framebuffer write, depth write, per-pixel increments, and loop control.

**Overall Quality**: HIGH — the port is structurally sound and semantically faithful to the x86-64 reference.

| Severity | Count |
|----------|-------|
| Critical | 0 |
| Major | 0 |
| Minor | 5 |
| Observations | 8 |

No correctness bugs were found. MAJOR-1 (aafunc) matches the x86-64 limitation and is not a regression. MAJOR-2 (fog blend) was a false positive — detailed re-analysis confirmed the ARM64 code is correct (see section 3).

---

## 2. Critical Issues

**None found.**

---

## 3. Major Issues

### MAJOR-1: `dest_aafunc` / `src_aafunc` only handles value 4 (AONE)

**File**: `vid_voodoo_codegen_arm64.h`
**Lines**: 3903-3925
**x86-64 ref**: Lines 3034-3057

**ARM64 code**:
```c
addlong(ARM64_MOV_ZERO(4));  /* w4 = 0 */

if (dest_aafunc == 4) {
    addlong(ARM64_LSL_IMM(6, 5, 7));
    addlong(ARM64_ADD_REG(4, 4, 6));
}

if (src_aafunc == 4) {
    addlong(ARM64_LSL_IMM(6, 12, 7));
    addlong(ARM64_ADD_REG(4, 4, 6));
}

addlong(ARM64_LSR_IMM(4, 4, 8));
addlong(ARM64_MOV_REG(12, 4));
```

**x86-64 code**:
```asm
XOR EAX, EAX
if dest_aafunc == 4: SHL EBX, 7 ; ADD EAX, EBX
if src_aafunc == 4:  SHL EDX, 7 ; ADD EAX, EDX
ROR EAX, 8
MOV EDX, EAX
```

**What's wrong**: The x86-64 also only handles `aafunc == 4`. So the ARM64 is actually a faithful port of the x86-64 limitation. For any `dest_aafunc` or `src_aafunc` value other than 4 (AONE), the blended alpha channel after alpha blending is always 0. This is the same behavior as x86-64.

**Verdict**: Not a regression from x86-64, but worth noting. **No fix needed unless you want to go beyond x86-64 parity.**

### ~~MAJOR-2: Fog blend factor off-by-one + missing rounding~~ — FALSE POSITIVE

**Status**: Re-analyzed and confirmed **correct as-is**.

The original audit incorrectly claimed the `+16` LDR_D offset was loading the wrong table entry and that a rounding PADDW was missing. Detailed re-analysis shows:

1. **The `+16` offset is correct**: It implements the interpreter's `fog_a++` before the multiply. `alookup[fog_a + 1]` contains the value `fog_a + 1` in each 16-bit lane, matching `vid_voodoo_render.h` lines 100-104.

2. **There is no rounding PADDW in the x86-64 fog section**: The auditor confused the fog section with the **alpha blend** section (which does use `PADDW alookup[1]`). The x86-64 fog code at lines 2385-2395 does: `PMULLW [R10 + EAX*8 + 16]` then `PSRAW 7` — no PADDW.

3. **Both JITs match the interpreter**: `(diff / 2) * (fog_a + 1) >> 7` = `diff * (fog_a + 1) >> 8` = interpreter's `fog_a++; (diff * fog_a) >> 8`.

**No fix needed.**

---

## 4. Minor Issues

### MINOR-1: Dead NEON register save in dual-TMU combine

**File**: `vid_voodoo_codegen_arm64.h`
**Line**: 2747

```c
/* Save v1 (other-clocal) in v5 before multiply */
addlong(ARM64_MOV_V(5, 1));
```

v5 is saved here but never read back. The x86-64 uses XMM5 in the PMULLW/PMULHW/PUNPCKLWD multiply sequence, but the ARM64 uses `SMULL_4S_4H` which combines multiply and widen in one instruction, making the saved copy unnecessary. Dead code — wastes 4 bytes, no functional impact.

**Status**: RESOLVED — dead code removed (2026-02-20).

### MINOR-2: `CC_MSELECT_TEX` broadcasts tex_a to all 4 NEON lanes instead of 3

**File**: `vid_voodoo_codegen_arm64.h`
**Lines**: 3321-3325

```c
addlong(ARM64_LDR_W(4, 0, STATE_tex_a));
addlong(ARM64_FMOV_S_W(3, 4));
addlong(ARM64_DUP_V4H_LANE(3, 3, 0));  /* broadcasts to all 4 lanes */
```

The x86-64 only sets lanes 0-2 of XMM3 to tex_a via three PINSRW instructions, leaving lane 3 (alpha) undefined. ARM64 broadcasts to all 4 lanes. The alpha lane of the CC result is not used (the CCA path provides the final alpha), so this difference is functionally harmless.

### MINOR-3: Dead `MOVI_V2D_ZERO(2)` at line 2140

```c
/* Zero v2 for later unpacking (PXOR XMM2, XMM2 equivalent) */
addlong(ARM64_MOVI_V2D_ZERO(2));
```

v2 is zeroed here but never referenced afterward. The x86-64 uses XMM2 as a zero constant for PUNPCKLBW (byte-to-halfword unpack), but the ARM64 uses `UXTL` instead, which doesn't need a zero register. Dead code — wastes 4 bytes.

**Status**: RESOLVED — dead code removed (2026-02-20).

### MINOR-4: Signed vs unsigned alpha clamp comparison

**File**: `vid_voodoo_codegen_arm64.h`
**Lines**: 3256-3257

```c
addlong(ARM64_CMP_IMM(12, 0xFF));
addlong(ARM64_CSEL(12, 10, 12, COND_GT));  /* if > 0xFF, 0xFF */
```

The x86-64 uses `CMOVA` (unsigned above), while ARM64 uses `COND_GT` (signed greater). However, the preceding clamp at line 3253-3254 handles negative values with `COND_LT`, so by the time we reach line 3256, w12 >= 0. For non-negative values, `COND_GT` and unsigned `COND_HI` produce identical results when comparing against 0xFF.

**Verdict**: Functionally correct. No change needed.

### MINOR-5: TCA alpha clamp at lines 2853-2857 uses signed comparisons

Same pattern as MINOR-4 — uses signed `COND_GT` vs x86-64's unsigned `CMOVA`. Same analysis: functionally correct because the negative case is handled first.

---

## 5. Observations (Correct patterns worth noting)

### OBS-1: x86-64 `tc_reverse_blend_1` bug correctly fixed

ARM64 line 2623: `} else if (!tca_reverse_blend_1) {`
x86-64 line 1299: `} else if (!tc_reverse_blend_1) {` (BUG)

The ARM64 port correctly uses the TCA (alpha) flag instead of the TC (color) flag in the TMU1 alpha combine reverse blend path. Intentional and correct divergence.

### OBS-2: W-depth CLZ-to-BSR conversion is correct

The ARM64 W-depth computation at lines 2224-2236 correctly translates the x86-64 BSR-based depth encoding:
- `CLZ w6, w4` → count of leading zeros
- `SUB w7, w6, #16` → `exp = CLZ - 16` = `15 - BSR` for 16-bit input
- Remaining mantissa computation (NOT, shift, mask) matches x86-64

### OBS-3: FOG_Z uses LSR matching x86-64 SHR

ARM64: `ARM64_LSR_IMM(4, 4, 12)` — logical shift right by 12.
x86-64: `SHR EAX, 12` — logical shift right by 12.
Both use logical (unsigned) shift. Correct.

### OBS-4: Stack frame properly sized and aligned

160 bytes total: 9 register pairs × 16 bytes = 144 bytes + 16 bytes padding = 160 bytes. Prologue (`STP x29, x30, [SP, #-160]!`) and epilogue (`LDP x29, x30, [SP], #160`) correctly match. All STP/LDP offsets are within frame. Highest access: `STP d12, d13, [SP, #128]` (bytes 128-143).

### OBS-5: Struct offset assertions provide compile-time safety

Lines 1071-1123: `VOODOO_ASSERT_OFFSET` macro uses `_Static_assert` to verify every struct offset constant matches the actual struct layout at compile time. Eliminates an entire class of bugs.

### OBS-6: Overflow protection on code emission

`addlong()` macro (lines 188-198) checks bounds before every 4-byte write. If a code block exceeds `BLOCK_SIZE` (16384), the overflow flag is set and the block is rejected (falls back to interpreter). Prevents buffer overflows.

### OBS-7: Bilinear texture lookup uses correct indexing

Bilinear weight lookup at lines 1672-1678 correctly computes `bilinear_index * 32` as byte offset into `bilinear_lookup` table. Each `voodoo_neon_reg_t` is 16 bytes, 2 entries per index (row0 + row1 weights), so 32 bytes per index is correct.

### OBS-8: W^X compliance is thorough

`voodoo_get_block()` (lines 4456-4576) correctly:
1. Calls `arm64_codegen_set_writable()` before JIT emission
2. Calls `arm64_codegen_set_executable()` after emission
3. Calls `__clear_cache()` after making executable
4. Handles failure at each step (rejects block, returns NULL for interpreter fallback)
5. Uses `pthread_jit_write_protect_np()` on macOS (MAP_JIT requirement)

---

## 6. Summary of Actionable Items

| # | Severity | Description | Action |
|---|----------|-------------|--------|
| MAJOR-1 | Major | `aafunc` only handles value 4 (AONE) | Same as x86-64 — no fix needed |
| ~~MAJOR-2~~ | ~~Major~~ | ~~Fog blend loads wrong table entry + missing rounding~~ | **FALSE POSITIVE — code is correct** |
| MINOR-1 | Minor | Dead `MOV_V(5, 1)` in dual-TMU combine | ~~Remove dead code~~ RESOLVED |
| MINOR-3 | Minor | Dead `MOVI_V2D_ZERO(2)` at line 2140 | ~~Remove dead code~~ RESOLVED |
| MINOR-4 | Minor | Signed vs unsigned comparison for alpha clamp | No change needed (functionally correct) |
| MINOR-5 | Minor | TCA alpha clamp uses signed comparisons | No change needed (functionally correct) |

**Recommendation**: The only issue with potential for visual artifacts is **MAJOR-2** (fog blend factor). All other items are cosmetic or have no functional impact. The codebase is in excellent shape overall.
