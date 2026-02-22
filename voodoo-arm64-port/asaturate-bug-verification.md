# AFUNC_ASATURATE Interpreter Bug Verification

**Date**: 2026-02-22
**Confidence**: 100% CONFIRMED BUG
**Severity**: Medium (affects transparency in games using GL_SRC_ALPHA_SATURATE)

---

## 1. The Bug

**File**: `src/include/86box/vid_voodoo_render.h`, lines 256-261

```c
case AFUNC_ASATURATE:                            \
    _a        = MIN(src_a, 255 - dest_a);        \
    src_r     = (dest_r * _a) / 255;             \   // BUG: should be src_r
    src_g     = (dest_g * _a) / 255;             \   // BUG: should be src_g
    src_b     = (dest_b * _a) / 255;             \   // BUG: should be src_b
    break;                                       \
```

**Should be**:

```c
case AFUNC_ASATURATE:                            \
    _a        = MIN(src_a, 255 - dest_a);        \
    src_r     = (src_r * _a) / 255;              \
    src_g     = (src_g * _a) / 255;              \
    src_b     = (src_b * _a) / 255;              \
    break;                                       \
```

The code reads `dest_r/g/b` (destination framebuffer color) instead of `src_r/g/b`
(incoming source fragment color) on the right-hand side of the assignment. This
means the source blend contribution is computed from destination pixels instead of
source pixels.

---

## 2. Proof: Pattern Analysis of the src_afunc Switch

Every other case in the `src_afunc` switch (lines 220-262) follows the pattern:

```
src_r = (src_r * FACTOR) / 255;
```

| Case             | Line | Factor           | Reads src_r? |
|------------------|------|------------------|:------------:|
| AFUNC_AZERO      | 222  | 0 (zeroed)       | N/A          |
| AFUNC_ASRC_ALPHA | 225  | src_a            | YES          |
| AFUNC_A_COLOR    | 230  | dest_r           | YES          |
| AFUNC_ADST_ALPHA | 235  | dest_a           | YES          |
| AFUNC_AONE       | 240  | 1 (no-op)        | YES (kept)   |
| AFUNC_AOMSRC_ALPHA| 242 | (255-src_a)      | YES          |
| AFUNC_AOM_COLOR  | 247  | (255-dest_r)     | YES          |
| AFUNC_AOMDST_ALPHA| 252 | (255-dest_a)     | YES          |
| **AFUNC_ASATURATE** | **258** | **_a = min(src_a,255-dest_a)** | **NO (reads dest_r!)** |

ASATURATE is the ONLY case that reads `dest_r` instead of `src_r`. This is
structurally inconsistent with every other case.

---

## 3. Proof: dest_afunc Switch Comparison

The `dest_afunc` switch (lines 174-218) modifies `newdest_r/g/b` and reads from
`dest_r/g/b`:

```c
case AFUNC_ASRC_ALPHA:
    newdest_r = (dest_r * src_a) / 255;   // reads dest_r
```

When ASATURATE was in the dest_afunc switch (before commit f39c3491b), using
`dest_r` made sense because dest_afunc operates on destination color. But when
the code was moved to src_afunc, the operand was not changed from `dest_r` to
`src_r`.

---

## 4. Proof: Git History

### Commit f39c3491b (2025-01-08)
"Voodoo: Fixes HUD transparency bugs in Extreme Assault"

This commit moved ASATURATE from `dest_afunc` to `src_afunc` and moved
ACOLORBEFOREFOG from `src_afunc` to `dest_afunc`. The diff shows:

```diff
-            case AFUNC_ASATURATE:                            \
-                _a        = MIN(src_a, 1 - dest_a);          \
-                newdest_r = (dest_r * _a) / 255;             \
-                newdest_g = (dest_g * _a) / 255;             \
-                newdest_b = (dest_b * _a) / 255;             \
+            case AFUNC_ASATURATE:                            \
+                _a        = MIN(src_a, 1 - dest_a);          \
+                src_r     = (dest_r * _a) / 255;             \
+                src_g     = (dest_g * _a) / 255;             \
+                src_b     = (dest_b * _a) / 255;             \
```

The left-hand sides were correctly changed from `newdest_r/g/b` to `src_r/g/b`,
but the right-hand sides were NOT changed from `dest_r/g/b` to `src_r/g/b`.

Also note the `1 - dest_a` bug (should be `255 - dest_a` in 8-bit space).

### Commit 573f4c89c (2025-01-08, 7 minutes later)
"Fix saturate alpha blending modes on interpreter."

Fixed `MIN(src_a, 1 - dest_a)` to `MIN(src_a, 255 - dest_a)`, but did NOT fix
the `dest_r/g/b` vs `src_r/g/b` issue.

**The dest_r/g/b bug has been present since f39c3491b and was never fixed.**

---

## 5. Proof: OpenGL/Glide Specification

GL_SRC_ALPHA_SATURATE (equivalent to Voodoo AFUNC_ASATURATE) is defined as:

```
Source blend factor: f = min(As, 1 - Ad)
Final color: Cs * f + Cd * dest_factor
```

Where:
- `Cs` = source color (src_r/g/b)
- `Cd` = destination color (dest_r/g/b)
- `As` = source alpha
- `Ad` = destination alpha
- `f` = saturation factor

The source contribution should be `src_color * f`, NOT `dest_color * f`.

Reference: OpenGL 2.1 Spec, Section 4.1.8 "Blending", Table 4.1.

---

## 6. Proof: x86-64 JIT (Correct Implementation)

File: `src/include/86box/vid_voodoo_codegen_x86-64.h`, lines 2977-3020

The x86-64 JIT is in the `src_afunc` switch (line 2771) and operates on **XMM0**
(source color):

```asm
; Compute saturation factor
MOV EAX, EBX          ; EAX = dest_alpha*2
SHR EAX, 1            ; EAX = dest_alpha
XOR AL, 0xFF          ; EAX = 255 - dest_alpha
SHL EAX, 1            ; EAX = (255 - dest_alpha) * 2
CMP EDX, EAX          ; compare src_alpha*2 vs (255-dest_alpha)*2
CMOVB EAX, EDX        ; EAX = min(src_alpha*2, (255-dest_alpha)*2)

; Apply factor to SOURCE color (XMM0)
PMULLW XMM0, alookup[EAX*8]   ; XMM0 = XMM0 * factor
; ... rounding shift sequence ...
PSRLW XMM0, 8                 ; XMM0 = result >> 8
```

Register convention: XMM0 = source, XMM4 = destination.
The JIT correctly multiplies **XMM0** (source) by the factor.

---

## 7. Proof: ARM64 JIT (Also Correct)

File: `src/include/86box/vid_voodoo_codegen_arm64.h`, lines 3900-3916

The ARM64 JIT operates on **v0** (source color):

```
MUL_V4H(0, 0, 16)    ; v0 = v0 * v16(sat_factor)
```

Register convention: v0 = source, v4 = destination.
The ARM64 JIT correctly multiplies **v0** (source) by the factor.

---

## 8. The Full Blend Equation

After both switches execute, the ALPHA_BLEND macro combines (lines 264-270):

```c
src_r += newdest_r;    // final = src_blended + dest_blended
src_g += newdest_g;
src_b += newdest_b;
src_r = CLAMP(src_r);  // clamp to [0, 255]
```

So:
- `src_r/g/b` after src_afunc = **source blend contribution** = src_color * src_factor
- `newdest_r/g/b` after dest_afunc = **dest blend contribution** = dest_color * dest_factor
- Final = src_blended + dest_blended

---

## 9. Concrete Numerical Example

Input values:
- src_r = 200 (bright red source fragment)
- src_a = 100 (source alpha)
- dest_r = 50 (dim destination pixel)
- dest_a = 80 (destination alpha)
- dest_afunc = AFUNC_AONE (pass destination through, for simplicity)

### Saturation Factor

```
_a = min(src_a, 255 - dest_a)
_a = min(100, 255 - 80) = min(100, 175) = 100
```

### BUGGY interpreter (current code)

```
src_r = (dest_r * _a) / 255 = (50 * 100) / 255 = 5000 / 255 = 19
newdest_r = dest_r = 50  (AFUNC_AONE)
result = src_r + newdest_r = 19 + 50 = 69
```

### CORRECT behavior (per spec, per JITs)

```
src_r = (src_r * _a) / 255 = (200 * 100) / 255 = 20000 / 255 = 78
newdest_r = dest_r = 50  (AFUNC_AONE)
result = src_r + newdest_r = 78 + 50 = 128
```

### Difference

| Value | Buggy | Correct | Error |
|-------|-------|---------|-------|
| src_r (blended) | 19 | 78 | 59 (75% too dim) |
| final result | 69 | 128 | 59 (46% too dim) |

The interpreter produces a dramatically darker result than intended. The source
fragment's contribution is based on the destination pixel (50) instead of the
source fragment (200), which inverts the luminance relationship.

### Pathological case

When dest_r = 0 (black background) and src_r = 255 (white source):

- Buggy: `src_r = (0 * _a) / 255 = 0` -- source contribution is ZERO regardless of src_r!
- Correct: `src_r = (255 * _a) / 255 = _a` -- source contributes proportionally

On a black background, the buggy code makes ASATURATE equivalent to
AFUNC_AZERO (complete source discard), which is catastrophically wrong.

---

## 10. Impact Assessment

### Who is affected?

- **Interpreter only** (used when JIT cache miss occurs, or on non-JIT platforms)
- Both JITs (x86-64 and ARM64) are CORRECT
- Games that use `GL_SRC_ALPHA_SATURATE` via Glide API

### Games that use ASATURATE

ASATURATE is uncommon in typical Glide games. The primary known user is:
- **Extreme Assault** (noted in commit f39c3491b)
- Games using multipass transparency with alpha-saturate blending

### Severity

**Medium**. The interpreter is a fallback path; most rendering goes through the
JIT. However, when the interpreter IS used for ASATURATE pixels (cache misses,
or platforms without JIT), the output will be visibly wrong -- much too dark,
especially against dark backgrounds.

The JIT-vs-interpreter mismatch also means that verify mode (jit_debug=2) will
report false mismatches on ASATURATE pixels, complicating future debugging.

---

## 11. Fix

Single-line change in `src/include/86box/vid_voodoo_render.h`:

```diff
             case AFUNC_ASATURATE:                            \
                 _a        = MIN(src_a, 255 - dest_a);        \
-                src_r     = (dest_r * _a) / 255;             \
-                src_g     = (dest_g * _a) / 255;             \
-                src_b     = (dest_b * _a) / 255;             \
+                src_r     = (src_r * _a) / 255;              \
+                src_g     = (src_g * _a) / 255;              \
+                src_b     = (src_b * _a) / 255;              \
                 break;                                       \
```

### Also note: x86 32-bit JIT is stale

File `src/include/86box/vid_voodoo_codegen_x86.h` still has ASATURATE in the
**dest_afunc** switch (line 2535), operating on XMM4. It was never updated by
commit f39c3491b. This is a separate (pre-existing) bug in the 32-bit x86 JIT.

---

## 12. Verification Checklist

- [x] Read interpreter code (lines 256-261): confirmed `dest_r/g/b` on RHS
- [x] Analyzed all other src_afunc cases: all read `src_r/g/b` -- ASATURATE is the only outlier
- [x] Read dest_afunc switch: confirms `dest_r/g/b` was correct when ASATURATE was there
- [x] Traced git history: f39c3491b moved code but did not update operand
- [x] Verified OpenGL spec: GL_SRC_ALPHA_SATURATE applies to source color
- [x] Verified x86-64 JIT: multiplies XMM0 (source) -- CORRECT
- [x] Verified ARM64 JIT: multiplies v0 (source) -- CORRECT
- [x] Traced full blend equation: src_blended + dest_blended
- [x] Constructed numerical example showing wrong output
- [x] Checked 32-bit x86 JIT: ASATURATE still in dest_afunc switch (separate bug)

---

**VERDICT: 100% confirmed real bug. The interpreter's AFUNC_ASATURATE reads
`dest_r/g/b` where it should read `src_r/g/b`. This was introduced in commit
f39c3491b when the case was moved from dest_afunc to src_afunc without updating
the operands. Both JITs are correct.**
