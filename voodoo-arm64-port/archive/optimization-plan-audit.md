# Optimization Plan Audit

**Date**: 2026-02-20
**Auditor**: voodoo-debug agent (findings extracted from analysis)

## Summary

Verified all 20 optimizations (H1-H8, M1-M7, L1-L5) against the actual codegen at `src/include/86box/vid_voodoo_codegen_arm64.h`. Found **2 errors** and **3 warnings** in the plan. All optimization logic and encoding claims verified correct.

## Issues Found

### ERROR-1: v13 is NOT available — used for color-before-fog

**Plan claims**: v13 is available, proposed for `{dSdX_0, dTdX_0, dSdX_1, dTdX_1} or dZdX (H1)`

**Actual**: v13 is used in the generated code:
- Line 3370: `MOV v13, v0` — saves color-before-fog
- Line 3776: `UXTL_8H_8B(16, 13)` — unpacks v13 for ACOLORBEFOREFOG alpha blend factor

**Impact**: The H1 register budget needs revision. v13 cannot be used for hoisted deltas. Use v14 or v15 instead (both confirmed available).

**Severity**: ERROR — would cause visual corruption if implemented as written.

---

### ERROR-2: v10 is NOT available — used for cc_invert

**Plan claims**: v10 is available, proposed for "iterated BGRA cache (H6)"

**Actual**: v10 holds `neon_ff_b` and is used at:
- Line 3359: `EOR v0, v0, v10` — XOR with 0xFF mask for `cc_invert_output`

**Impact**: The H6 register assignment needs revision. Cannot use v10 for iterated BGRA cache. Use v12 instead (confirmed available) or v11 (see WARNING-1).

**Severity**: ERROR — would cause visual corruption in cc_invert path.

---

### WARNING-1: v11 is dead weight — loaded but never used

**Plan claims**: v11 is available, proposed for "fogColor unpacked (M3)"

**Actual**: v11 is loaded with `neon_minus_254` in the prologue (line ~1984) but is **never referenced** in any NEON operation in the generated code body. All occurrences of register 11 in the codegen are GPR (w11/x11), not NEON v11.

**Impact**: v11 IS effectively available for reuse (plan is correct by accident). The dead prologue load of neon_minus_254 should be removed when repurposing v11. Note: this is also an additional dead code finding — the neon_minus_254 prologue load wastes 4 instructions.

**Severity**: WARNING — plan conclusion is correct but reasoning is wrong.

---

### WARNING-2: Some M2 line numbers are off by 5-6 lines

**Plan cites**: `~3256` and `~2853` for clamp patterns

**Actual**:
- The clamp at "~3256" is actually at lines 3247-3251
- The clamp at "~2853" is actually at lines 2847-2851 (line 2853 is `tca_invert_output`)

**Impact**: Minor — the clamp patterns DO exist at those locations, just offset. The BIC+ASR optimization is still valid.

**Severity**: WARNING — could cause confusion during implementation.

---

### WARNING-3: M2 requires a new encoding macro

**Plan claims**: BIC+ASR #31 replaces the clamp pattern

**Actual**: The existing `ARM64_BIC_REG` macro (line 405, encoding `0x0A200000`) does NOT support shifted register operands. A new `ARM64_BIC_REG_ASR` macro is needed:
```c
#define ARM64_BIC_REG_ASR(d, n, m, shift) (0x0A200000 | (2 << 22) | (((shift) & 0x3F) << 10) | Rm(m) | Rn(n) | Rd(d))
```

**Impact**: Minor — just needs a new macro definition. The math is verified correct.

**Severity**: WARNING — plan omits implementation detail.

---

## Verified Correct

| ID | Claim | Status |
|----|-------|--------|
| H1 | Params deltas (dBdX etc.) are loop-invariant, loaded every pixel at lines ~4186-4270 | CORRECT |
| H2 | STATE_x loaded 9 times per pixel, only written at line 4310 (end of loop) | CORRECT |
| H3 | STATE_x2 loaded once at line 4313, never written in loop | CORRECT |
| H4 | LOD stored at line 1352, reloaded at line 1411 (~59 instructions later) with no intervening write | CORRECT |
| H5 | SDIV deferred as high-risk — appropriate | CORRECT |
| H6 | Iterated BGRA computed up to 4 times identically — logic correct (register assignment wrong, see ERROR-2) | LOGIC CORRECT |
| H7 | v8 (neon_01_w) = alookup[1] = {1,1,1,1,0,0,0,0} — values identical | CORRECT |
| H7 | 14 `LDR d16, [x20, #16]` sites found | CORRECT (exactly 14) |
| H8 | 14 MOV v17 + USHR v17,v17 sites (7 use v4 source, 7 use v0 source) — all replaceable | CORRECT |
| H8 | USHR with different Rd/Rn is valid on ARMv8.0 | CORRECT |
| M2 | BIC+ASR #31 math: zeros negative, preserves non-negative | CORRECT |
| M2 | Clamp patterns exist at cited locations (with minor line number offsets) | CORRECT |
| M4 | rgb565 pointer materialized with 4-instruction MOVZ+MOVK per pixel | CORRECT |
| M7 | pixel_count and texel_count are adjacent 32-bit fields | CORRECT |
| All | Stack frame currently 160 bytes | CORRECT |

## Register Availability Verification

| Register | Plan Claims | Actual Status | Verdict |
|----------|-------------|---------------|---------|
| x26 (GPR) | Available → rgb565 pointer | Not used in codegen | AVAILABLE |
| x27 (GPR) | Available → STATE_x2 | Not used in codegen | AVAILABLE |
| x28 (GPR) | Available → STATE_x cache | Not used in codegen | AVAILABLE |
| v8 (NEON) | Pinned neon_01_w | Used, also = alookup[1] | CORRECT |
| v9 (NEON) | Pinned neon_ff_w | Used | CORRECT |
| v10 (NEON) | Available → iterated BGRA | **Used for neon_ff_b (cc_invert at line 3359)** | **NOT AVAILABLE** |
| v11 (NEON) | Available → fogColor | Loaded with neon_minus_254 but **never read** — dead | AVAILABLE (dead weight) |
| v12 (NEON) | Available → dBdX deltas | Not used as NEON register | AVAILABLE |
| v13 (NEON) | Available → texture deltas | **Used for color-before-fog (lines 3370, 3776)** | **NOT AVAILABLE** |
| v14 (NEON) | Available | Not used as NEON register | AVAILABLE |
| v15 (NEON) | Available | Not used as NEON register | AVAILABLE |

## Corrected Register Budget

| Register | Proposed Use |
|----------|-------------|
| v8 | neon_01_w (pinned) + alookup[1] replacement (H7) |
| v9 | neon_ff_w (pinned) |
| v10 | **KEEP as neon_ff_b** (used for cc_invert) |
| v11 | fogColor unpacked (M3) — remove dead neon_minus_254 load |
| v12 | {dBdX, dGdX, dRdX, dAdX} (H1) |
| v13 | **KEEP as color-before-fog** (used for ACOLORBEFOREFOG) |
| v14 | Texture deltas or iterated BGRA cache (H6) — reassigned from v10 |
| v15 | Additional deltas (H1) if needed |

## Conclusion

The optimization plan is **largely accurate**. All optimization logic, encoding claims, cycle estimates, and safety analysis are correct. The two errors are both register assignment issues in the budget table — v10 and v13 are claimed available but are actually in use. These are easily fixed by reassigning to v14/v15 which are genuinely available. The optimizations themselves (H1-H8, M1-M7) are all valid and safe to implement with the corrected register assignments.

**Recommendation**: Update the plan's register budget table before implementation. No changes needed to the optimization logic or implementation order.
