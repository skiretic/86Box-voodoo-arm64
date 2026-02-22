# Batch 7 Optimization Audit

File: `src/include/86box/vid_voodoo_codegen_arm64.h` (4753 lines, current HEAD `05f68249a`)
Date: 2026-02-20

---

## M1: LDP for Adjacent 32-bit Loads

**Claim**: There are adjacent 32-bit loads from the same base register that can be combined into LDP instructions.

### Findings

Searched all `addlong(ARM64_LDR_W(...))` calls for consecutive pairs from the same base register with offsets differing by exactly 4 bytes.

#### LDP Candidate 1: Lines 1434 + 1438 (bilinear texture fetch)

```c
// Line 1434:
addlong(ARM64_LDR_W(4, 0, STATE_tex_s));   // offset 188
// Line 1436: (intervening LSL)
addlong(ARM64_LSL_IMM(10, 10, 3));
// Line 1438:
addlong(ARM64_LDR_W(5, 0, STATE_tex_t));   // offset 192
```

- Offsets: 188, 192 -- differ by 4. **LDP candidate**.
- **PROBLEM**: STATE_tex_s=188 is NOT 4-byte aligned for LDP_OFF_W encoding. The LDP W immediate is `imm * 4`, so offset 188 requires `imm = 47` which is within the signed 7-bit range [-64..63]. This is fine: 188/4 = 47.
- **BUT**: There is one intervening instruction (LSL at line 1436) between them. To use LDP, these two loads must be truly consecutive. The intervening `LSL w10, w10, #3` can be reordered after the LDP since it doesn't depend on w4 or w5.
- **Registers**: w4 and w5 -- different destination registers. Valid for LDP.
- **Verdict**: CONFIRMED -- `LDP w4, w5, [x0, #188]` saves 1 instruction. Reorder the LSL after the LDP.

#### LDP Candidate 2: Lines 1750 + 1752 (point-sample texture fetch)

```c
// Line 1750:
addlong(ARM64_LDR_W(4, 0, STATE_tex_s));   // offset 188
// Line 1751: (comment only)
// Line 1752:
addlong(ARM64_LDR_W(5, 0, STATE_tex_t));   // offset 192
```

- Same offsets as above, truly adjacent (no intervening instructions).
- **Verdict**: CONFIRMED -- `LDP w4, w5, [x0, #188]` saves 1 instruction. Straightforward replacement.

#### LDP Candidate 3: Lines 2068 + 2070 (prologue fb_mem/aux_mem) -- 64-bit

```c
// Line 2068:
addlong(ARM64_LDR_X(8, 0, STATE_fb_mem));   // offset 456
// Line 2070:
addlong(ARM64_LDR_X(9, 0, STATE_aux_mem));  // offset 464
```

- Offsets: 456, 464 -- differ by 8. **LDP X candidate**.
- 456/8 = 57, within signed 7-bit range. Valid encoding.
- This is in the prologue, runs once per scanline. Very low impact but easy.
- **Verdict**: CONFIRMED -- `LDP x8, x9, [x0, #456]` saves 1 instruction.

#### Non-candidates checked and rejected:

- Lines 1354/1358 (lod_min/lod_max): Both load into w10 (same dest register). LDP requires two *different* dest registers. **Not a candidate**.
- Lines 4197/4199 (STATE_z / PARAMS_dZdX): Different base registers (x0 vs x1). **Not a candidate**.
- Lines 4218/4219, 4228/4229, 4252/4253 (tmu_w / dWdX): Different base registers. **Not a candidate**.
- Lines 1262-1268 (tmu_s/tmu_t/tmu_w): TMU-parameterized offsets, could be adjacent for specific TMU values (TMU0: 496,504,512; TMU1: 520,528,536). But for LDP X, TMU0 s=496 and t=504 differ by 8 -- however these use different register numbers (x5,x6). Wait, there's 3 lines gap. Let me re-check:
  - Line 1262: `LDR x5, [x0, #STATE_tmu_s(tmu)]`
  - Line 1265: `LDR x6, [x0, #STATE_tmu_t(tmu)]`
  - 3-line gap with a blank line and comment. Two intervening `addlong` calls between them.
  - For TMU0: offsets 496, 504 (diff 8). For TMU1: 520, 528 (diff 8). **LDP X candidates** but requires reordering.
  - Line 1268: `LDR x7, [x0, #STATE_tmu_w(tmu)]` -- TMU0: 512, TMU1: 536. Could pair t+w as well.
  - **MODIFIED**: These could be LDP candidates but they are in the perspective-correct W division path which already has many instructions. The savings (1 insn per pair) is marginal.

### Summary for M1

| Site | Lines | Base | Offsets | Type | Savings | Verdict |
|------|-------|------|---------|------|---------|---------|
| tex_s + tex_t (bilinear) | 1434, 1438 | x0 | 188, 192 | LDP W | 1 insn | CONFIRMED |
| tex_s + tex_t (point) | 1750, 1752 | x0 | 188, 192 | LDP W | 1 insn | CONFIRMED |
| fb_mem + aux_mem (prologue) | 2068, 2070 | x0 | 456, 464 | LDP X | 1 insn | CONFIRMED |
| tmu_s + tmu_t (persp) | 1262, 1265 | x0 | TMU-dep | LDP X | 1 insn | CONFIRMED (reorder needed) |

**Total savings**: 4 instructions across the file (2 in hot loop, 2 in prologue/setup).

**Verdict: CONFIRMED** -- The claim is correct. 4 LDP sites found.

---

## M3: Hoist fogColor to v11 (replacing dead neon_minus_254)

**Claim**: `params->fogColor` is loaded per-pixel but is triangle-invariant. Can be hoisted to prologue in v11 since neon_minus_254 is dead.

### v11 Usage Audit

All references to v11 (NEON register 11) in the generated code:

1. **Line 109**: Register assignment comment: `v11 = minus_254 constant {0xFF02,...}`
2. **Line 1951**: `STP d10, d11, [SP, #112]` -- prologue save
3. **Line 2013**: Comment: `v11 = minus_254 {0xFF02,...}`
4. **Line 2033**: `EMIT_LOAD_NEON_CONST(11, &neon_minus_254)` -- loads v11 in prologue
5. **Line 4323**: `LDP d10, d11, [SP, #112]` -- epilogue restore

**NO reads of v11 anywhere in the loop body or any pipeline stage.** The only references are:
- Save/restore in prologue/epilogue
- The initial load

**v11 is confirmed DEAD** -- loaded but never read.

### fogColor Usage Analysis

fogColor is loaded at two sites in the fog section:

**Site 1: Line 3446 (FOG_CONSTANT path)**
```c
addlong(ARM64_LDR_W(4, 1, PARAMS_fogColor));   // line 3446
addlong(ARM64_FMOV_S_W(3, 4));                  // line 3447
addlong(ARM64_UQADD_V8B(0, 0, 3));              // line 3449
```
This path uses fogColor as packed bytes (8B). 3 instructions for load+use.

**Site 2: Lines 3457-3459 (non-constant fog path)**
```c
addlong(ARM64_LDR_W(4, 1, PARAMS_fogColor));    // line 3457
addlong(ARM64_FMOV_S_W(3, 4));                   // line 3458
addlong(ARM64_UXTL_8H_8B(3, 3));                 // line 3459
```
This path unpacks fogColor to 4x16 halfwords. 3 instructions for load+unpack.

**Key question**: Is fogColor triangle-invariant?

`params->fogColor` is loaded from `x1` (params pointer), which points to `voodoo_params_t`. This structure is set up per-triangle and does NOT change per-pixel. **CONFIRMED triangle-invariant.**

### Hoisting Strategy

The complication is that the two sites use fogColor in different forms:
- Site 1 (FOG_CONSTANT): needs packed bytes in v3 (8B format)
- Site 2 (non-constant fog): needs unpacked halfwords in v3 (8H format)

If we hoist fogColor into v11 as packed bytes (the common denominator), then:
- Site 1: `MOV v3, v11` (1 insn, saves 2)
- Site 2: `UXTL v3.8H, v11.8B` (1 insn, saves 2)

The FOG_ADD sub-path (line 3461-3462) sets v3 to zero instead of fogColor, so it doesn't use fogColor at all.

### Prologue Changes

Replace the current dead v11 load (5 instructions at lines 2020-2033):
```c
EMIT_LOAD_NEON_CONST(11, &neon_minus_254);  // 4 MOV/MOVK + 1 LDR_Q = 5 insns
```

With fogColor load into v11 (only needed when fog is enabled):
```c
// Only emit when fog is enabled AND not FOG_ADD-only
addlong(ARM64_LDR_W(16, 1, PARAMS_fogColor));
addlong(ARM64_FMOV_S_W(11, 16));
```
That's 2 instructions instead of 5. Net savings in prologue: 3 instructions.

### Per-pixel Savings

Per fog pixel: saves 2 instructions (one LDR + one FMOV eliminated).

### Verdict: CONFIRMED

- v11 is truly dead (no reads in loop body)
- fogColor is triangle-invariant (loaded from params)
- Saves 3 prologue instructions + 2 per-pixel instructions in fog path
- The neon_minus_254 static variable initialization (lines 1912-1915) can also be removed from `voodoo_generate()`

**IMPORTANT CAVEAT**: When fog is NOT enabled, v11 doesn't need to be loaded at all. Currently it wastes 5 prologue instructions loading the dead neon_minus_254. The fix should conditionally load fogColor only when `params->fogMode & FOG_ENABLE`, and skip the v11 load entirely otherwise. This saves 5 prologue instructions in the no-fog case (most common case for many games).

---

## M5: Eliminate Redundant FMOV+LSR in Dual-TMU TCA Path

**Claim**: In the dual-TMU TCA path, there's a redundant FMOV+LSR sequence extracting TMU0 alpha that could be done once and reused.

### Analysis

The dual-TMU TCA section is at lines 2822-2889. The TMU0 alpha extraction pattern `FMOV_W_S(reg, 7) + LSR_IMM(reg, reg, 24)` appears at:

1. **Lines 2792 + 2801** (conditional on `tca_sub_clocal`):
   ```c
   addlong(ARM64_FMOV_W_S(5, 7));      // line 2792 (before multiply)
   ...                                    // (multiply happens)
   addlong(ARM64_LSR_IMM(5, 5, 24));    // line 2801 (after multiply)
   ```
   This extracts TMU0 alpha into w5 for use as `clocal_alpha`.

2. **Lines 2827-2828** (`!tca_zero_other` path, extracts TMU1 alpha from v3):
   ```c
   addlong(ARM64_FMOV_W_S(4, 3));       // line 2827 -- TMU1 alpha, NOT TMU0
   addlong(ARM64_LSR_IMM(4, 4, 24));    // line 2828
   ```
   This is TMU1 alpha (v3), not TMU0 -- **not a duplicate**.

3. **Lines 2840-2841** (TCA_MSELECT_CLOCAL -- TMU0 alpha from v7):
   ```c
   addlong(ARM64_FMOV_W_S(5, 7));       // line 2840
   addlong(ARM64_LSR_IMM(5, 5, 24));    // line 2841
   ```

4. **Lines 2848-2849** (TCA_MSELECT_ALOCAL -- TMU0 alpha from v7):
   ```c
   addlong(ARM64_FMOV_W_S(5, 7));       // line 2848
   addlong(ARM64_LSR_IMM(5, 5, 24));    // line 2849
   ```

5. **Lines 2844-2845** (TCA_MSELECT_AOTHER -- TMU1 alpha from v3):
   ```c
   addlong(ARM64_FMOV_W_S(5, 3));       // line 2844 -- TMU1, not TMU0
   addlong(ARM64_LSR_IMM(5, 5, 24));    // line 2845
   ```

6. **Lines 2886-2887** (`tca_add_clocal || tca_add_alocal`):
   ```c
   addlong(ARM64_FMOV_W_S(5, 7));       // line 2886
   addlong(ARM64_LSR_IMM(5, 5, 24));    // line 2887
   ```

### Redundancy Analysis

The `FMOV_W_S(_, 7) + LSR_IMM(_, _, 24)` pattern (extracting TMU0 alpha from v7) appears at up to 4 sites:
- Sites 1 (lines 2792/2801): into w5, conditional on `tca_sub_clocal`
- Sites 3/4 (lines 2840/2848): into w5, in tca_mselect switch cases
- Site 6 (lines 2886/2887): into w5, conditional on `tca_add_clocal || tca_add_alocal`

However, these are inside switch cases -- only ONE of cases 3/4 executes per compilation. And the switch cases and the other sites are gated by different conditions, so at most 2-3 of these will appear in any given compiled block.

**The key redundancy**: When `tca_sub_clocal` is true AND `tca_mselect` is CLOCAL or ALOCAL, w5 already has TMU0 alpha from site 1 (line 2801). But then the switch case at site 3 or 4 overwrites it with the same value. This is genuinely redundant.

Similarly, when `tca_add_clocal/alocal` is true, w5 may already contain TMU0 alpha from the mselect step, but it gets clobbered by the multiply at line 2881 (`MUL w4, w4, w5`), so site 6 must re-extract.

### Proposed Fix

Extract TMU0 alpha from v7 once (just before the TCA section begins), save in a register like w13 (free at this point), then use w13 wherever TMU0 alpha is needed:

```c
// Once, at line ~2791:
addlong(ARM64_FMOV_W_S(13, 7));
addlong(ARM64_LSR_IMM(13, 13, 24));

// Then replace each FMOV+LSR(5,7,24) with MOV w5, w13 (1 insn)
// And replace FMOV+LSR at line 2801 with MOV w5, w13
```

This saves 1 instruction per site (replacing 2-insn FMOV+LSR with 1-insn MOV), with 2-4 sites benefiting depending on configuration. Net savings: 2-4 instructions per compiled block that uses dual-TMU TCA.

**BUT**: w13 is callee-saved (x13 is saved/restored). In the generated JIT code, w13 is used as scratch in texture fetch (row addresses). By the time we reach the TCA section (line 2822), texture fetch is complete and w13 is free.

Wait -- actually w13 is NOT callee-saved on ARM64. x13 is a caller-saved scratch register. It's fine to use it here.

### Verdict: CONFIRMED (with modification)

The claim is correct that there's redundancy, but the original claim says "FMOV+LSR" which implies the sequence appears redundantly. The fix should extract TMU0 alpha once and reuse. Savings: **2-4 instructions** depending on which TCA modes are active.

---

## M6: BFI for No-dither RGB565 Packing

**Claim**: The no-dither RGB565 packing currently uses 7 instructions and can be reduced to 5-6 using BFI.

### Current Code (lines 4094-4103)

```c
addlong(ARM64_UBFX(5, 4, 3, 5));      // 1: w5 = B[7:3] (5 bits)
addlong(ARM64_UBFX(6, 4, 10, 6));     // 2: w6 = G[15:10] (6 bits)
addlong(ARM64_LSL_IMM(6, 6, 5));      // 3: w6 = G << 5
addlong(ARM64_UBFX(7, 4, 19, 5));     // 4: w7 = R[23:19] (5 bits)
addlong(ARM64_LSL_IMM(7, 7, 11));     // 5: w7 = R << 11
addlong(ARM64_ORR_REG(4, 7, 6));      // 6: w4 = R | G
addlong(ARM64_ORR_REG(4, 4, 5));      // 7: w4 = (R|G) | B
```

**Current count: 7 instructions**. The claim of 7 is correct.

### BFI Approach

BFI (Bit Field Insert) copies a bitfield from one register into another register, replacing specific bits while preserving the rest.

**ARM64_BFI macro does NOT exist** in the current codebase. It would need to be added:

```c
/* BFI Wd, Wn, #lsb, #width -- alias for BFM Wd, Wn, #(-lsb mod 32), #(width-1) */
#define ARM64_BFI(d, n, lsb, width) \
    (0x33000000 | IMMR((-(lsb)) & 0x1F) | IMMS((width) - 1) | Rn(n) | Rd(d))
```

With BFI, the sequence could be:

```c
addlong(ARM64_UBFX(4, 4, 3, 5));       // 1: w4 = B = bits[7:3] -- destructive, but we're done with raw BGRA
// Hmm, we need R and G from the original w4 but UBFX just overwrote it.
```

Actually, BFI requires the destination to already contain the "base" value. Let me reconsider:

```c
addlong(ARM64_LSR_IMM(5, 4, 3));       // 1: w5 = BGRA >> 3 (B is in bits [4:0])
addlong(ARM64_AND_MASK(5, 5, 5));      // 2: w5 = B[4:0] = 5 low bits
addlong(ARM64_UBFX(6, 4, 10, 6));      // 3: w6 = G[5:0]
addlong(ARM64_BFI(5, 6, 5, 6));        // 4: w5[10:5] = G (insert G into bits 5-10)
addlong(ARM64_UBFX(6, 4, 19, 5));      // 5: w6 = R[4:0]
addlong(ARM64_BFI(5, 6, 11, 5));       // 6: w5[15:11] = R (insert R into bits 11-15)
// w5 = RGB565, move to w4 or use directly
```

That's 6 instructions + possibly 1 MOV = 6-7. Not much better.

**Better approach** using UBFX+BFI more efficiently:

```c
addlong(ARM64_UBFX(5, 4, 3, 5));       // 1: w5 = B[4:0]
addlong(ARM64_UBFX(6, 4, 10, 6));      // 2: w6 = G[5:0]
addlong(ARM64_UBFX(7, 4, 19, 5));      // 3: w7 = R[4:0]
addlong(ARM64_BFI(5, 6, 5, 6));        // 4: w5 = B | (G << 5)
addlong(ARM64_BFI(5, 7, 11, 5));       // 5: w5 = B | (G<<5) | (R<<11)
addlong(ARM64_MOV_REG(4, 5));          // 6: w4 = RGB565 (if w4 is needed for store)
```

**6 instructions** (or 5 if the store can use w5 directly instead of w4).

Looking at line 4108: `addlong(ARM64_STRH_REG_LSL1(4, 8, 14))` -- the store uses w4. But we could build the result in w4 directly:

```c
addlong(ARM64_UBFX(4, 4, 3, 5));       // 1: w4 = B[4:0] (destroys packed BGRA!)
```

No, we need the original packed BGRA (w4) for all three extractions. So we can't use w4 as both source and dest.

Actually wait -- we can extract into separate regs first, then BFI into one:

```c
addlong(ARM64_UBFX(5, 4, 3, 5));       // 1: w5 = B
addlong(ARM64_UBFX(6, 4, 10, 6));      // 2: w6 = G
addlong(ARM64_UBFX(4, 4, 19, 5));      // 3: w4 = R (now safe to overwrite w4)
addlong(ARM64_LSL_IMM(4, 4, 11));      // 4: w4 = R << 11
addlong(ARM64_BFI(4, 6, 5, 6));        // 5: w4[10:5] = G
addlong(ARM64_BFI(4, 5, 0, 5));        // 6: w4[4:0] = B
```

**6 instructions**. But BFI(4, 5, 0, 5) is just `ORR w4, w4, w5` since B occupies bits [4:0]. So:

```c
addlong(ARM64_UBFX(5, 4, 3, 5));       // 1: w5 = B
addlong(ARM64_UBFX(6, 4, 10, 6));      // 2: w6 = G
addlong(ARM64_UBFX(4, 4, 19, 5));      // 3: w4 = R (destroys packed, but we're done with it)
addlong(ARM64_LSL_IMM(4, 4, 11));      // 4: w4 = R << 11
addlong(ARM64_BFI(4, 6, 5, 6));        // 5: w4[10:5] = G
addlong(ARM64_ORR_REG(4, 4, 5));       // 6: w4 |= B
```

**6 instructions** (vs current 7). Saves 1 instruction.

### Verdict: MODIFIED

- The claim of "7 instructions" is **correct**.
- The claim of "5-6 using BFI" is **partially correct**: achievable is 6 (saves 1), not 5.
- **BFI macro does not exist** and must be added.
- Net savings: **1 instruction** per no-dither pixel write.
- The saving is real but modest. BFI replaces one LSL+ORR pair with one BFI.

---

## L1: Replace CMP #0 + B.EQ/B.NE with CBZ/CBNZ

**Claim**: There are stray `CMP #0` followed by `B.EQ`/`B.NE` that can be replaced with CBZ/CBNZ.

### All CMP #0 Sites

| Line | Instruction | Context | Next Branch | CBZ/CBNZ? |
|------|-------------|---------|-------------|-----------|
| 1279 | `CMP_IMM_X(7, 0)` | tmu_w == 0 check | B.EQ (line 1281) | **YES**: `CBZ x7, skip` |
| 1532 | `CMP_IMM(13, 0)` | Clamp T1 >= 0 | CSEL LT (line 1533) | **NO**: flags used by CSEL, not branch |
| 1538 | `CMP_IMM(5, 0)` | Clamp T0 >= 0 | CSEL LT (line 1539) | **NO**: flags used by CSEL, not branch |
| 1578 | `CMP_IMM(4, 0)` | Clamp S >= 0 | CSEL LT (1580) + B.LT (1584) | **NO**: flags used by both CSEL and B.cond. CSEL needs N flag, not just zero. |
| 1786 | `CMP_IMM(4, 0)` | Point-sample S clamp | CSEL LT (line 1787) | **NO**: flags used by CSEL LT |
| 1804 | `CMP_IMM(5, 0)` | Point-sample T clamp | CSEL LT (line 1805) | **NO**: flags used by CSEL LT |
| 2318 | `CMP_IMM(10, 0)` | Z-depth clamp >= 0 | CSEL LT (line 2320) | **NO**: flags used by CSEL LT |

### Analysis

Only **1 site** (line 1279) is a genuine CBZ/CBNZ candidate. The remaining 6 sites use `CMP #0` to set condition flags that are consumed by CSEL with COND_LT (testing the negative/sign flag). CBZ/CBNZ only tests for zero/non-zero and does NOT set condition flags, so they cannot replace these patterns.

For line 1279:
```c
// Current (2 instructions):
addlong(ARM64_CMP_IMM_X(7, 0));           // line 1279
addlong(ARM64_BCOND_PLACEHOLDER(COND_EQ)); // line 1281

// Replacement (1 instruction):
addlong(ARM64_CBZ_X_PLACEHOLDER(7));       // needs ARM64_CBZ_X_PLACEHOLDER macro
```

Wait -- `ARM64_CBZ_X_PLACEHOLDER` does not exist. We have `ARM64_CBZ_W_PLACEHOLDER` (line 708) and `ARM64_CBZ_X` (line 680), but no X-sized placeholder. The register x7 is 64-bit here (it holds tmu_w which is 64-bit). Need to add `ARM64_CBZ_X_PLACEHOLDER`.

Actually, looking at the encoding: `ARM64_CBZ_X(t, off) = 0xB4000000 | OFFSET19(off) | Rt(t)`. A placeholder with off=0 would be `0xB4000000 | Rt(7) = 0xB4000007`. Then patched with `PATCH_FORWARD_CBxZ`.

### Verdict: MODIFIED

- **Only 1 candidate** (line 1279), not multiple as claimed.
- The other 6 `CMP #0` sites use CSEL with COND_LT, which tests the sign bit -- CBZ cannot replace these.
- Savings: **1 instruction** at one site.
- Need to add `ARM64_CBZ_X_PLACEHOLDER` macro (trivial: `(0xB4000000 | Rt(t))`).

---

## L2: Eliminate MOV w11,w7 before LSL

**Claim**: In texture fetch, there's a `MOV w11, w7` before `LSL w5, w5, w11` that can be eliminated by using w7 directly.

### Sites Found

1. **Line 1509** (bilinear texture fetch):
   ```c
   /* MOV w11, w7  (w11 = tex_shift for SHL later) */
   addlong(ARM64_MOV_REG(11, 7));    // line 1509
   ```
   Used later at lines 1562-1563:
   ```c
   addlong(ARM64_LSL_REG(5, 5, 11));    // line 1562
   addlong(ARM64_LSL_REG(13, 13, 11));  // line 1563
   ```

2. **Line 1818** (point-sample texture fetch):
   ```c
   /* MOV w11, w7  (tex_shift) */
   addlong(ARM64_MOV_REG(11, 7));    // line 1818
   ```
   Used at line 1820:
   ```c
   addlong(ARM64_LSL_REG(5, 5, 11));    // line 1820
   ```

### Analysis -- Is w7 still live at the use sites?

**Site 1 (bilinear, line 1509)**:
- w7 is set at line 1430: `SUB w7, w7, w6` (tex_shift = 8 - lod)
- Between line 1509 and lines 1562-1563, w7 is NOT modified. All intervening code uses w4, w5, w6, w10, w11, w13, w14, w15 -- not w7.
- **w7 is live and unchanged** at the use sites.
- The MOV at line 1509 is genuinely redundant. Replace `LSL_REG(5, 5, 11)` with `LSL_REG(5, 5, 7)` and same for `LSL_REG(13, 13, 11)`.

**Site 2 (point-sample, line 1818)**:
- w7 is set at line 1743: `SUB w7, w7, w6` (tex_shift = 8 - lod)
- Between line 1818 and line 1820, nothing modifies w7.
- **w7 is live and unchanged**.
- Replace `LSL_REG(5, 5, 11)` with `LSL_REG(5, 5, 7)`.

### Verdict: CONFIRMED

Both sites can safely use w7 directly, eliminating the MOV. Saves **2 instructions** total (1 per texture fetch path).

---

## Bonus: Remove Dead v11 neon_minus_254 Load

**Claim**: The prologue code that loads neon_minus_254 into v11 is dead and can be removed.

### Current Prologue Load (lines 2020-2033)

```c
EMIT_LOAD_NEON_CONST(11, &neon_minus_254);
```

This expands to 5 instructions:
```
MOVZ x16, #<low16>
MOVK x16, #<bits 16-31>, LSL #16
MOVK x16, #<bits 32-47>, LSL #32
MOVK x16, #<bits 48-63>, LSL #48
LDR q11, [x16]
```

### Confirmation: v11 Never Read

Exhaustive search of the entire file for any instruction that reads v11 (NEON register 11) in the loop body:

- `grep` for all `(11,` patterns in addlong calls within the loop body (lines 2075-4312):
  - No NEON operations reference register 11 as a source operand.
  - The STP/LDP at prologue/epilogue save/restore d11 but don't use its value.

**The load is 100% dead.**

### Also Dead: neon_minus_254 Initialization

Lines 1912-1915 in `voodoo_generate()`:
```c
neon_minus_254.u32[0] = 0xff02ff02;
neon_minus_254.u32[1] = 0xff02ff02;
neon_minus_254.u32[2] = 0;
neon_minus_254.u32[3] = 0;
```

These 4 C statements are also dead code (they initialize a constant that's loaded but never used).

### Savings

- **5 prologue instructions** saved (the MOVZ+MOVK+LDR_Q sequence)
- **4 lines of C code** removed (neon_minus_254 initialization)
- If combined with M3 (fogColor hoist), the 5 dead instructions are **replaced** with 2 fogColor-load instructions when fog is enabled, or with nothing when fog is disabled.

### Verdict: CONFIRMED

v11 (neon_minus_254) is unconditionally dead. The load and initialization should be removed.

---

## Executive Summary

| Opt | Claim | Verdict | Savings | Notes |
|-----|-------|---------|---------|-------|
| **M1** | LDP for adjacent loads | **CONFIRMED** | 4 insns | 4 sites found (2 hot, 2 setup) |
| **M3** | Hoist fogColor to v11 | **CONFIRMED** | 3+2/pixel | v11 dead, fogColor is triangle-invariant |
| **M5** | Eliminate FMOV+LSR in TCA | **CONFIRMED** | 2-4 insns | Extract TMU0 alpha once, reuse |
| **M6** | BFI for RGB565 packing | **MODIFIED** | 1 insn | 6 insns (not 5), BFI macro needed |
| **L1** | CBZ/CBNZ for CMP #0 | **MODIFIED** | 1 insn | Only 1 site (not multiple), 6 others use CSEL |
| **L2** | Eliminate MOV w11,w7 | **CONFIRMED** | 2 insns | Both texture fetch paths, safe |
| **Bonus** | Remove dead v11 load | **CONFIRMED** | 5 insns | Combined with M3 for best effect |

**Total estimated savings**: ~15-20 instructions per compiled block (varies by pipeline configuration).

**Recommended implementation order**:
1. Bonus + M3 (combined: remove dead v11 load, optionally hoist fogColor)
2. L2 (trivial, 2 lines changed)
3. M1 (add LDP calls at 4 sites)
4. M5 (extract TMU0 alpha once in TCA section)
5. M6 (add BFI macro, change no-dither packing) -- lowest priority, 1 insn savings
6. L1 (add CBZ_X placeholder, change 1 site) -- lowest priority, 1 insn savings
