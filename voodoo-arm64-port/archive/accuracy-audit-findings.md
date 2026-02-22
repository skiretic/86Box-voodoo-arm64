# Accuracy Audit Findings — JIT vs Interpreter

**Date**: 2026-02-22
**Scope**: ARM64 JIT, x86-64 JIT, and C interpreter compared across all alpha blend, color combine, and depth paths
**Method**: 4 parallel verification agents, each doing independent source code analysis with line-level citations

---

## Summary

| # | Finding | Verdict | Impact | Action |
|---|---------|---------|--------|--------|
| 1 | Alpha blend /255 vs >>8 | **FALSE ALARM** | None | None needed |
| 2 | TMU1 RGB negate ordering | **FIXED in ARM64 JIT** | ±1 per channel, sub-perceptual | Done |
| 3 | AFUNC_ASATURATE operand | **REAL BUG in interpreter** | Wrong color blended entirely | **Fix recommended** |
| 4 | zaColor depth bias clamp vs truncate | **FIXED in ARM64 JIT** | Edge case only | Done |

**ARM64-specific bugs found: 0**

---

## Finding 1: Alpha Blend /255 — FALSE ALARM

### Claim
The JIT uses `(n + 1 + (n >> 8)) >> 8` which allegedly approximates `/255`, while the interpreter uses `>> 8` (division by 256).

### Reality
- The interpreter uses `/ 255` (not `>> 8`) at `vid_voodoo_render.h:179-181`
- The JIT formula `(n + 1 + (n >> 8)) >> 8` is **mathematically identical** to `floor(n / 255)` for all n in [0, 65025]
- This is the entire valid range for `color * alpha` products (0-255 × 0-255)
- All three implementations produce **bit-identical** results

### Math proof
`1/255 = 1/256 × 1/(1 - 1/256)`. Geometric series: `floor(n/255) = floor((n + floor(n/256) + 1) / 256) = (n + (n >> 8) + 1) >> 8`

### Verdict: No difference exists. No fix needed.

---

## Finding 2: TMU1 RGB tc_sub_clocal Negate Ordering — REAL

### The difference

When TMU1 color combine uses `tc_sub_clocal_1` (subtract local color), the interpreter and JITs negate in different order:

**Interpreter** (`vid_voodoo_render.c:483-491`):
```c
r = (-state->tex_r[1] * (factor_r + 1)) >> 8;  // negate FIRST, then multiply, then shift
```
Computation: `(-P) >> 8` = `-ceil(P / 256)`

**x86-64 JIT** (`vid_voodoo_codegen_x86-64.h:1192-1216`):
```asm
PMULLW XMM0, XMM3     ; positive multiply
PMULHW XMM5, XMM3     ; signed high
PUNPCKLWD/PSRAD/PACK  ; widen, shift, narrow
PSUBW XMM1, XMM0      ; negate LAST
```
Computation: `-(P >> 8)` = `-floor(P / 256)`

**ARM64 JIT** (`vid_voodoo_codegen_arm64.h:2639-2642`):
```asm
SMULL v16.4S, v3.4H, v0.4H  ; positive widening multiply
SSHR  v16.4S, v16.4S, #8     ; shift right 8
SQXTN v0.4H, v16.4S          ; narrow
SUB   v1.4H, v1.4H, v0.4H   ; negate LAST
```
Computation: `-(P >> 8)` — matches x86-64

### Error magnitude
Exactly 1 LSB per channel when `texel * adjusted_factor % 256 != 0` (the common case).

### Scope
- **Only TMU1 RGB** — NOT TMU1 alpha (TCA uses negate-before-shift, matching interpreter)
- NOT TMU0 (different code path)
- NOT color combine
- Common in Voodoo 2 trilinear/multitexture, rare in Voodoo 1

### Fix (2026-02-22)

Negated clocal BEFORE the widening multiply to match the interpreter's `(-clocal * factor) >> 8` order.
Changed `SMULL(v3,v0) → SSHR → SQXTN → SUB` to `SUB(negate) → SMULL → SSHR → SQXTN`.
Same instruction count. ARM64 JIT now matches interpreter exactly on this path; x86-64 JIT still has the bug.

Verify mode results: mismatch events dropped 68% (2.9M → 926K), differing pixels dropped 74% (22.3M → 5.8M).

### Verdict: ~~Real ±1 difference, inherited from x86-64.~~ **FIXED** — ARM64 JIT now more accurate than x86-64.

---

## Finding 3: AFUNC_ASATURATE — REAL BUG IN INTERPRETER

### The bug

**File**: `src/include/86box/vid_voodoo_render.h`, lines 256-261

```c
case AFUNC_ASATURATE:
    _a    = MIN(src_a, 255 - dest_a);
    src_r = (dest_r * _a) / 255;     // BUG: reads dest_r, should be src_r
    src_g = (dest_g * _a) / 255;     // BUG: reads dest_g, should be src_g
    src_b = (dest_b * _a) / 255;     // BUG: reads dest_b, should be src_b
    break;
```

### Why it's wrong

GL_SRC_ALPHA_SATURATE is a **source** blend factor: `f = min(src_alpha, 1 - dest_alpha)`, applied as `src_color * f`. The code should multiply `src_r/g/b` (source color), not `dest_r/g/b` (destination color).

### 10 independent proofs

1. **Pattern**: Every other `src_afunc` case reads `src_r` on the RHS. ASATURATE is the only outlier reading `dest_r`.
2. **dest_afunc contrast**: The `dest_afunc` switch modifies `newdest_r/g/b` using `dest_r/g/b`. When ASATURATE was there, `dest_r` was correct.
3. **Git history**: Commit `f39c3491b` moved ASATURATE from `dest_afunc` to `src_afunc`. LHS updated (`newdest_r` → `src_r`), RHS left as `dest_r`. Copy-paste error.
4. **Partial fix**: Commit `573f4c89c` (7 min later) fixed `1 - dest_a` → `255 - dest_a` but missed the `dest_r` issue.
5. **OpenGL spec**: `GL_SRC_ALPHA_SATURATE` factor applied to source color, not destination.
6. **x86-64 JIT** (`vid_voodoo_codegen_x86-64.h:2991`): Multiplies XMM0 (source) — correct.
7. **ARM64 JIT** (`vid_voodoo_codegen_arm64.h:3911`): Multiplies v0 (source) — correct.
8. **Blend equation**: Final = `src_blended + dest_blended`. src_afunc computes source contribution.
9. **Numerical**: src_r=200, dest_r=50, factor=100/255 → buggy=19, correct=78. On black backgrounds, bug zeroes source entirely.
10. **Structural**: ASATURATE (0xF) shares value with ACOLORBEFOREFOG in a different switch. Commit correctly separated them but botched the operand.

### Impact

- AFUNC_ASATURATE is **rare** (GL_SRC_ALPHA_SATURATE — anti-aliased lines, some HUD effects)
- When triggered, produces **completely wrong colors** (not ±1, but using wrong color entirely)
- Both JITs are correct — only the interpreter fallback path is affected
- In verify mode (jit_debug=2), creates guaranteed mismatches for any ASATURATE pipeline

### Fix

```diff
 case AFUNC_ASATURATE:
     _a    = MIN(src_a, 255 - dest_a);
-    src_r = (dest_r * _a) / 255;
-    src_g = (dest_g * _a) / 255;
-    src_b = (dest_b * _a) / 255;
+    src_r = (src_r * _a) / 255;
+    src_g = (src_g * _a) / 255;
+    src_b = (src_b * _a) / 255;
     break;
```

### Verdict: Real upstream 86Box bug. JITs are correct. Interpreter fix recommended.

---

## Finding 4: zaColor Depth Bias Clamp vs Truncate — REAL

### The difference

When applying zaColor depth bias to the Z value:

**Interpreter** (`vid_voodoo_render.c:1152`):
```c
new_depth = CLAMP16(new_depth + (int16_t) params->zaColor);
```
`CLAMP16` (`vid_voodoo_common.h:24`): clamps to [0, 0xFFFF]

**x86-64 JIT** (`vid_voodoo_codegen_x86-64.h:954-960`):
```asm
ADD EAX, [params->zaColor]    ; 32-bit add
AND EAX, 0xFFFF               ; truncate (wrap)
```

**ARM64 JIT** (`vid_voodoo_codegen_arm64.h:2377-2384`):
```asm
LDR  w4, [x1, #PARAMS_zaColor]
ADD  w10, w10, w4
UXTH w10, w10                  ; truncate (wrap)
```

### Behavior at boundaries

| Scenario | depth | bias | Interpreter | Both JITs |
|----------|-------|------|-------------|-----------|
| Overflow | 0xFFF0 | +32 | 0xFFFF (clamped) | 0x0010 (wrapped) |
| Underflow | 0x0010 | -32 | 0x0000 (clamped) | 0xFFF0 (wrapped) |
| Normal | 0x8000 | +256 | 0x8100 | 0x8100 |

### Historical note
The old **x86 32-bit JIT** (`vid_voodoo_codegen_x86.h:792-812`) used CMOVS/CMOVA for proper clamping. The x86-64 JIT simplified this to AND, losing the clamp behavior.

### Impact
Near zero. Requires depth + bias to cross the 0 or 0xFFFF boundary, which needs extreme depth values AND large bias — essentially never happens in real games.

### Fix (2026-02-22)

Replaced `UXTH` (truncate/wrap) with proper CLAMP16 sequence: `SXTH` (sign-extend zaColor) +
`CMP/CSEL` (clamp to 0) + `MOVZ/CMP/CSEL` (clamp to 0xFFFF). Same pattern as the z-buffer
depth source clamp immediately above. Adds 5 net instructions for FBZ_DEPTH_BIAS blocks.

### Verdict: ~~Real difference, inherited from x86-64.~~ **FIXED** — ARM64 JIT now matches interpreter exactly.

---

## Overall Conclusions

1. **The ARM64 JIT has zero unique bugs** — every difference found was inherited from the x86-64 JIT
2. **One real upstream bug found** (Finding 3: ASATURATE) — affects interpreter only, both JITs are correct
3. **Two inherited rounding differences fixed** (Findings 2 & 4) — ARM64 JIT now matches interpreter on both paths
4. **One false alarm** (Finding 1) — the math is actually identical across all implementations
5. **The ARM64 JIT is now more accurate than the x86-64 JIT** on three paths:
   - Fog blending: fog_a++ fix (commit ad136e14)
   - TMU1 RGB negate ordering: negate-before-multiply fix (Finding 2)
   - Depth bias clamping: CLAMP16 instead of truncate (Finding 4)
