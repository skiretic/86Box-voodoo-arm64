# Voodoo ARM64 JIT Optimization Audit -- Round 2

**Date:** 2026-02-20
**Scope:** Exhaustive review of `vid_voodoo_codegen_arm64.h` (4761 lines)
**Baseline:** All 7 optimization batches complete (~80-100 insns/pixel removed)
**Register budget:** TIGHT -- GPR x19-x28 pinned, NEON v8-v15 pinned

---

## Category 1: Per-Pixel Increment Optimizations

### OPT-R2-01: Hoist dZdX into a callee-saved GPR

**Location:** Lines 4204-4213, per-pixel Z increment
**Current code:**
```
LDR w4, [x0, #STATE_z]
LDR w5, [x1, #PARAMS_dZdX]      <-- reloaded every pixel
ADD/SUB w4, w4, w5
STR w4, [x0, #STATE_z]
```
**Proposed:** Load `PARAMS_dZdX` into a scratch register once in the prologue, similar to how v12 holds the RGBA deltas. Problem: no callee-saved GPRs remain. However, dZdX is a 32-bit scalar and could be stored in an unused lane of an existing callee-saved NEON register. For example, the high 32 bits of v11 are unused when fog is disabled, or we could use v13's high 32 bits (v13 is only used as scratch between color combine and fog, never across iterations).

Actually, examining more carefully: v13 is callee-saved but used as "color-before-fog" which is written fresh every pixel (line 3417). Its value does NOT survive across iterations. So v13's upper lanes are free in the prologue for hoisting.

Alternative: Use `UMOV w5, v12.S[3]` to extract dAdX which shares the same vector as dZdX... wait, dZdX is at PARAMS_dZdX=64, separate from the BGRA deltas at PARAMS_dBdX=48. They are NOT contiguous.

**Verdict:** We cannot hoist dZdX without adding stack spill or repurposing a register. The cost is 1 LDR per pixel. This is a hot-path load from the params struct (likely L1-cached). Net savings: 1 instruction per pixel, but requires careful register engineering.

**Savings:** 1 insn/pixel
**Priority:** LOW
**Risk:** MEDIUM (register pressure)

---

### OPT-R2-02: Hoist TMU0 dWdX and global dWdX into NEON

**Location:** Lines 4226-4243, TMU0 W and global W increments
**Current code:**
```
LDR x10, [x0, #STATE_tmu0_w]
LDR x11, [x1, #PARAMS_tmu0_dWdX]    <-- reloaded every pixel
ADD/SUB x10, x10, x11
STR x10, [x0, #STATE_tmu0_w]

LDR x10, [x0, #STATE_w]
LDR x11, [x1, #PARAMS_dWdX]         <-- reloaded every pixel
ADD/SUB x10, x10, x11
STR x10, [x0, #STATE_w]
```
**Proposed:** Both dWdX values are 64-bit and triangle-invariant. They could be hoisted into a Q-register pair. However, all callee-saved NEON v8-v15 are taken. These would need stack slots.

The TMU0 W delta could share a Q register with the global W delta (they're both 64 bits = one Q register). But we have no free callee-saved NEON registers.

**Verdict:** Not feasible without expanding the stack frame and adding save/restore cost. The 2 LDR per pixel from params are likely L1 hits. Net savings: 2 insns/pixel but with prologue/epilogue cost.

**Savings:** 2 insns/pixel (but offset by prologue/epilogue cost)
**Priority:** LOW
**Risk:** MEDIUM

---

### OPT-R2-03: Combine Z increment with ADD-to-memory pattern

**Location:** Lines 4204-4213
**Current code (4 insns):**
```
LDR w4, [x0, #STATE_z]
LDR w5, [x1, #PARAMS_dZdX]
ADD w4, w4, w5
STR w4, [x0, #STATE_z]
```
**x86-64 reference (1 insn):** `ADD state->z[EDI], EAX` (memory-to-memory ADD)

ARM64 has no memory-destination ADD. This is an inherent ISA difference. No optimization possible here.

**Savings:** 0
**Priority:** N/A

---

### OPT-R2-04: Combine TMU0 W and global W loads into LDP

**Location:** Lines 4226-4243
**Current code:**
```
LDR x10, [x0, #STATE_tmu0_w]    -- offset 512
...
LDR x10, [x0, #STATE_w]         -- offset 544
```
These are not adjacent (512 vs 544, 32-byte gap), and they're used at different times with stores in between. LDP not applicable.

**Savings:** 0
**Priority:** N/A

---

### OPT-R2-05: Pair TMU1 dWdX load with LDP

**Location:** Lines 4259-4267
**Current code:**
```
LDR x10, [x0, #STATE_tmu1_w]     -- offset 536
LDR x11, [x1, #PARAMS_tmu1_dWdX] -- different base register
```
Cannot LDP across different base registers.

**Savings:** 0
**Priority:** N/A

---

## Category 2: Texture Fetch Optimizations

### OPT-R2-06: Eliminate redundant tex_s/tex_t stores when not needed

**Location:** Lines 1334, 1352 (perspective path); Lines 1409, 1415 (non-perspective path)
**Current code:**
```
STR w6, [x0, #STATE_tex_t]     -- line 1334
STR w5, [x0, #STATE_tex_s]     -- line 1352
```
Then at line 1449:
```
LDP w4, w5, [x0, #STATE_tex_s]  -- loads them back!
```

**Analysis:** The perspective path stores tex_s and tex_t into state, then immediately reloads them in the bilinear/point-sample section. This is a classic store-then-load pattern.

The values are computed in `w5` (tex_s) and `w6` (tex_t) at lines 1311-1313. If we can keep them in registers instead of storing and reloading, we save 2 STR + 1 LDP = 3 instructions.

However, there's a subtlety: the LOD calculation between the stores and loads modifies several registers. Let me check which registers survive:
- After STR w6 at line 1334, w6 is still live with tex_t value.
- After STR w5 at line 1352, w5 is still live with tex_s value.
- But then at line 1378: `MOV w6, w4` overwrites w6 with LOD!
- The bilinear section starts at line 1440 and needs tex_s and tex_t.

So we could:
1. Keep tex_s in w5 (it survives until the LDP at 1449 overrides w5)
2. Move tex_t to a different scratch register before w6 gets overwritten by LOD

Actually the stores to STATE_tex_s/STATE_tex_t may be needed by the non-JIT fallback or debugging. But within the JIT block itself, they're only consumed by the same codegen_texture_fetch function. If no external observer reads them between store and load, the store-load pair is purely internal.

**Wait:** These stores ARE needed. They set up state that the point-sample and bilinear sections read from. But the JIT code itself emits both the stores and the loads -- there's no external reader between them. So we can eliminate the stores AND the loads, keeping the values in registers.

**Problem:** The register shuffle around LOD calculation (lines 1325-1378) destroys w6 (stores LOD into w6 at line 1378). We'd need to move tex_t to a different register.

**Proposed:** Instead of STR tex_t and tex_s, keep tex_t in w13 (scratch) and tex_s in w5 through the LOD calculation. Then in the bilinear/point-sample sections, use w13/w5 directly instead of loading from memory.

But this optimization is complex and spans two large code sections with branching. The risk of subtle register-lifetime bugs is significant.

**Savings:** 3 insns/pixel (2 STR + 1 LDP eliminated)
**Priority:** MEDIUM
**Risk:** HIGH (register lifetime across complex LOD calculation)

---

### OPT-R2-07: Eliminate ebp_store memory round-trip in bilinear path

**Location:** Lines 1511, 1587/1640
**Current code:**
```
STR w10, [x0, #STATE_ebp_store]    -- line 1511 (store bilinear shift)
...
LDR w10, [x0, #STATE_ebp_store]    -- line 1587 or 1640 (reload)
```

The bilinear_shift value (`w10`) is stored to `STATE_ebp_store` and then reloaded later. Between the store (line 1511) and the reload (line 1587), the code executes:
- Texture base pointer load (uses x11, x12)
- tex_shift operations (uses w7)
- T coordinate setup (uses w13, w5)
- S/T clamp/wrap (uses w14, w15)
- Row address computation (uses x13, x14)

Register w10 is overwritten at line 1443 (`MOVZ w10, 1`), line 1471 (`SUB w4, w4, w10`), and line 1485 (`MOV w10, w4`).

**Proposed:** Could we use a different scratch register to hold the bilinear_shift across this section? Looking at what's free: w16/w17 are intra-procedure scratch but not used until later. Actually w16 is used at line 1518 (`ADD x11, x0, #STATE_tex_n(tmu)`) via the ADD_IMM_X macro which encodes into the instruction, not register 16.

Wait, looking more carefully at register usage between lines 1511-1587:
- w10 is reused as scratch starting at line 1485
- w16 is not used as a GPR in this section (the NEON LDR Q at line 1702 uses v16)
- x17 is available as scratch

We could use x17 to hold bilinear_shift from line 1504 through to line 1587, eliminating the store-load pair.

**Savings:** 2 insns/pixel (in bilinear path only)
**Priority:** MEDIUM
**Risk:** LOW (x17 is documented as intra-procedure scratch and available)

---

### OPT-R2-08: Eliminate STATE_lod store-load in point-sample path

**Location:** Lines 1377-1378, then 1791/1803
**Current code (perspective path):**
```
STR w4, [x0, #STATE_lod]     -- line 1377
MOV w6, w4                    -- line 1378 (H4 cache)
```
Then in point-sample clamp section:
```
LDR w11, [x0, #STATE_lod]    -- line 1791 or 1803 (reloaded!)
```

The LOD is stored to state AND cached in w6. But later, the clamp/wrap section needs LOD again and loads it from state instead of using w6.

**Wait:** At line 1759, `ADD w6, w6, #4` modifies w6, making it `lod+4`. So the original LOD is no longer in w6 when we reach line 1791. That's why it's reloaded from STATE_lod.

**Proposed:** Save the original LOD in a different register before modifying w6. For example, `MOV w11, w6` before `ADD w6, w6, #4`, then use w11 later instead of loading from memory. But w11 is used as scratch between lines 1759 and 1791.

Actually looking at the code path: after `ADD w6, w6, #4` at line 1759, the next use of w11 is not until the clamp section at line 1791. The registers between 1759-1791 use w4, w5, w6, w7 for mirror/shift operations. w11 is free from 1759 to 1791.

Wait, actually w11 is the destination of `LDR w11, [x0, STATE_tex_n(tmu)]` at line 1751. Then used at 1752. After that, w11 is free until line 1791 where it's loaded from STATE_lod.

**Proposed fix:** At line 1758 (before ADD w6, w6, #4), insert `MOV w11, w6` to save original LOD. Then replace `LDR w11, [x0, #STATE_lod]` at lines 1791/1803 with using the saved w11. But wait, w11 would need to survive through the mirror and shift operations. Let me trace:
- Line 1765-1777: mirror uses w4, w5 only
- Line 1779-1782: LSR uses w4, w5, w6
- Lines 1785+: clamp/wrap starts

So w11 IS free from line 1758 through to line 1791. This saves 1 LDR per pixel.

But in the non-perspective path (lines 1412-1419), LOD is stored as `w5 >> 8` then `MOV w6, w5` (H4 cache). The point-sample section at line 1803 also loads STATE_lod. Same optimization applies.

**Savings:** 1 insn/pixel (point-sample path only)
**Priority:** LOW
**Risk:** LOW

---

### OPT-R2-09: Merge MOVZ+SUB for (1<<48) constant

**Location:** Lines 1282-1285
**Current code (4 insns):**
```
MOVZ x4, #0          -- low 16 bits
MOVK x4, #0, LSL #16
MOVK x4, #0, LSL #32
MOVK x4, #1, LSL #48
```
**Proposed:** This loads `0x0001_0000_0000_0000`. Since only the top halfword is nonzero, this can be done with:
```
MOVZ x4, #1, LSL #48
```
That's 1 instruction instead of 4. The `MOVZ` with `hw=3` zeros all other bits and places `1` in bits [63:48].

Currently the code uses `ARM64_MOVZ_X(4, 0)` which is `MOVZ X4, #0` (pointless -- just zeros the register, then overwrites with MOVKs). The MOVZ_X macro hardcodes hw=0. We need a new variant or use `ARM64_MOVK_X` differently.

Wait: `ARM64_MOVZ_X(d, imm16)` is defined as `(0xD2800000 | IMM16(imm16) | Rd(d))`. This doesn't include the `hw` field. For `MOVZ X4, #1, LSL #48` we need hw=3: `0xD2E00000 | IMM16(1) | Rd(4)`.

We could add a macro `ARM64_MOVZ_X_HW(d, imm16, hw)` or just use `ARM64_MOVK_X` style with the hw field.

Actually there's already `ARM64_MOVK_X(d, imm16, hw)` which supports the hw field. We just need a `MOVZ` variant. Or we can write it inline.

**Savings:** 3 insns/block (one-time, in perspective division path only)
**Priority:** LOW
**Risk:** LOW

---

### OPT-R2-10: MADD for LOD computation

**Location:** Lines 1361-1362
**Current code:**
```
LDR w10, [x0, #STATE_tmu_lod(tmu)]
ADD w4, w4, w10
```
This is `lod = logtable_result + state->tmu[tmu].lod`. The ADD uses two registers. No optimization possible since one operand comes from memory.

However, looking at the full LOD pipeline (lines 1325-1377), there may be opportunities to restructure the entire sequence. The x86-64 uses BSR; ARM64 uses CLZ which gives the complementary result. The current translation has 5 extra instructions (CLZ, MOVZ #63, SUB, MOV, SUB #19) compared to x86's (BSR, MOV, SUB #19). But 2 of those (CLZ→63-CLZ) are inherent to the ISA difference.

**Savings:** 0 (already optimized)
**Priority:** N/A

---

## Category 3: Color/Alpha Combine Optimizations

### OPT-R2-11: Eliminate redundant MOVZ w10, #0xFF in alpha clamp sequences

**Location:** Lines 3089-3092, 3123-3126, 3297-3300, 3527-3530, 3537-3540, etc.
**Pattern (repeated ~8 times across the file):**
```
MOVZ w10, #0xFF
BIC  w_reg, w_reg, w_reg, ASR #31
CMP  w_reg, #0xFF
CSEL w_reg, w10, w_reg, HI
```

The `MOVZ w10, #0xFF` is emitted every time this pattern appears. If two clamp sequences are adjacent (e.g., a_other and a_local both use CLAMP(ia >> 12)), the second one can reuse w10 from the first.

**Example at lines 3086-3092 (a_other = ITER_A) followed by 3119-3127 (a_local = ITER_A when a_sel != A_SEL_ITER_A):**
- First sequence sets w10 = 0xFF at line 3089
- Second sequence sets w10 = 0xFF again at line 3123

If a_sel == A_SEL_ITER_A, the second path just does `MOV w15, w14` (line 3118) -- no redundancy.
If a_sel != A_SEL_ITER_A AND cca_localselect == CCA_LOCALSELECT_ITER_A, lines 3121-3126 recompute with a fresh MOVZ w10, #0xFF. But at this point w10 might have been clobbered by... let me check.

Between line 3092 and 3123: no instructions modify w10 (lines 3095-3118 only touch w14, w15). So the second `MOVZ w10, #0xFF` IS redundant.

**Analysis across the file:** This is a compile-time optimization of the C code that emits the JIT. The emitter could track whether w10 already holds 0xFF. However, implementing such tracking would add significant complexity to the C codegen for minimal per-pixel savings (the MOVZ is 1 cycle, likely absorbed in the pipeline).

**Savings:** 1-4 insns/pixel depending on pipeline config
**Priority:** LOW
**Risk:** LOW (but code complexity HIGH)

---

### OPT-R2-12: Use DUP_V4H_GPR instead of FMOV+DUP for scalar-to-vector broadcast

**Location:** Lines 2559-2560, 2564-2565, 2781-2782, 2786-2787, 3327-3328, 3355-3356, 3365-3366
**Current pattern (2 insns):**
```
FMOV s_reg, w_reg
DUP v_reg.4H, v_reg.H[0]
```
**Proposed (1 insn):**
```
DUP v_reg.4H, w_reg
```
The `DUP Vd.4H, Wn` instruction (`ARM64_DUP_V4H_GPR`) broadcasts a GPR's low 16 bits to all 4 halfword lanes in a single instruction.

**Wait:** The current pattern FMOV+DUP works because FMOV puts the 32-bit GPR value into S[0] (which covers H[0] and H[1]), then DUP broadcasts H[0] to all 4 halfword lanes. The DUP_V4H_GPR does the same thing in one step: takes the low 16 bits of Wn and broadcasts.

This works correctly if the value fits in 16 bits (0-255 for alpha values, detail bias values, LOD fractions). All the use cases here are indeed 16-bit values or less.

**Savings:** 1 insn per occurrence (up to 7 occurrences, but most are in conditional paths)
**Priority:** MEDIUM
**Risk:** LOW (semantically equivalent)

---

### OPT-R2-13: Avoid MOV_V(16, 0) before SMULL in color combine multiply

**Location:** Lines 3377-3393
**Current code:**
```
MOV v16.16B, v0.16B       -- save v0
EOR v3, v3, v9 (or not)
ADD v3.4H, v3.4H, v8.4H
SMULL v17.4S, v16.4H, v3.4H
SSHR v17.4S, v17.4S, #8
SQXTN v0.4H, v17.4S
```
The MOV v16, v0 saves v0 because the reverse-blend EOR might modify v3 which uses v0 in the multiply. Wait, no: v3 is the blend factor, v0 is the color difference. The EOR/ADD modify v3, not v0. Then the SMULL reads v16 (saved v0) and v3.

Actually: the MOV is needed because SMULL's destination v17 is different from both inputs. But the MOV copies v0 to v16 and then uses v16 in the SMULL. Could we just use v0 directly?

```
SMULL v17.4S, v0.4H, v3.4H   -- use v0 directly
```

This works because v0 is not modified between the MOV and the SMULL. The MOV v16, v0 is simply unnecessary!

Let me verify: after line 3377 (MOV v16, v0), the next operations are:
- Line 3381: EOR v3, v3, v9 -- modifies v3, not v0
- Line 3383-3384: ADD v3, v3, v8 -- modifies v3, not v0
- Line 3391: SMULL v17, v16, v3 -- reads v16 (was v0)

So v0 is NOT modified. The MOV is redundant. We can use v0 directly in the SMULL.

**Savings:** 1 insn/pixel (when cc_mselect != 0 || cc_reverse_blend != 0)
**Priority:** MEDIUM
**Risk:** LOW

---

### OPT-R2-14: SMLAL instead of SMULL+ADD in TMU combine

**Location:** Lines 2611-2614 (TMU1 combine), 2801-2804 (TMU0 combine)
**Current code:**
```
SMULL v16.4S, v3.4H, v0.4H    -- v16 = clocal * factor
SSHR  v16.4S, v16.4S, #8      -- v16 >>= 8
SQXTN v0.4H, v16.4S           -- v0 = narrow(v16)
SUB   v1.4H, v1.4H, v0.4H    -- v1 = 0 - product (since v1 was zeroed)
```

The negate is performed as `v1 = 0 - result`. Then `tc_add_clocal` adds v3 back. This matches the x86 PSUBW XMM1(zero), XMM0 pattern.

Could use `NEG v0.4H, v0.4H` (NEON negate) + `ADD v1, v0, v3` if tc_add_clocal, saving 1 instruction in that path. But the x86 code explicitly uses the subtract-from-zero pattern for a reason (it handles the saturation differently). Leave as-is for correctness.

**Savings:** 0
**Priority:** N/A

---

## Category 4: Alpha Blend Optimizations

### OPT-R2-15: Use URSHR for alpha blend rounding instead of 4-instruction sequence

**Location:** Lines 3741-3744, 3749-3752, etc. (repeated 16 times!)
**Current rounding sequence (4 insns per occurrence):**
```
USHR v17.4H, v_src.4H, #8     -- temp = product >> 8
ADD  v_src.4H, v_src.4H, v8.4H -- product += 1 (v8 = xmm_01_w)
ADD  v_src.4H, v_src.4H, v17.4H -- product += temp
USHR v_src.4H, v_src.4H, #8    -- result = product >> 8
```
This implements: `result = (product + 1 + (product >> 8)) >> 8`

**Can we use URSHR (unsigned rounding shift right)?**
URSHR computes: `result = (product + (1 << (shift-1))) >> shift`
For shift=8: `result = (product + 128) >> 8`

The current formula: `(product + 1 + (product >> 8)) >> 8`
URSHR formula: `(product + 128) >> 8`

These are NOT equivalent for all inputs. Example:
- product = 255: current = (255 + 1 + 0) >> 8 = 1; URSHR = (255+128)>>8 = 1 -- same
- product = 256: current = (256 + 1 + 1) >> 8 = 1; URSHR = (256+128)>>8 = 1 -- same
- product = 65025 (255*255): current = (65025 + 1 + 254) >> 8 = 255; URSHR = (65025+128)>>8 = 254 -- DIFFERENT!

So URSHR does NOT produce identical results. This was the exact analysis done in Batch 1 (H7/H8). The current formula is the Voodoo hardware's exact rounding behavior. Cannot change.

**Savings:** 0 (correctness violation)
**Priority:** N/A

---

### OPT-R2-16: Deduplicate alpha blend lookup table load

**Location:** Lines 3737-3738, 3816-3817 (ASRC_ALPHA for dest then src)
**Current code:**
When both dest_afunc and src_afunc are ASRC_ALPHA, the alookup table entry is loaded twice:
```
// dest_afunc == ASRC_ALPHA
ADD x7, x20, x12, LSL #3     -- x7 = alookup + src_alpha*16
LDR d5, [x7]                  -- v5 = alookup[src_alpha]
...
// src_afunc == ASRC_ALPHA
ADD x7, x20, x12, LSL #3     -- SAME computation
LDR d16, [x7]                 -- v16 = alookup[src_alpha] (same value!)
```

**Proposed:** If both afuncs use the same lookup key, reuse the loaded vector. However, this is a JIT-time optimization: the C codegen emits each switch case independently. To deduplicate, we'd need to check at emit time whether the same key was already loaded and skip the redundant load.

This would require:
1. Tracking which alookup/aminuslookup entries are currently in which NEON register
2. Emitting the load only if not already available

The complexity is moderate but the savings only apply to specific afunc combinations.

**Savings:** 2 insns/pixel (ADD+LDR) for specific afunc combos (e.g., src=dst=ASRC_ALPHA)
**Priority:** LOW
**Risk:** LOW (but code complexity MEDIUM)

---

### OPT-R2-17: Use UQADD for alpha blend final combine instead of ADD+SQXTUN

**Location:** Lines 3896-3899
**Current code:**
```
ADD   v0.4H, v0.4H, v4.4H     -- src_blended + dst_blended (can overflow 255)
SQXTUN v0.8B, v0.8H           -- pack with unsigned saturation
```
**Analysis:** This is already near-optimal. The ADD can produce values >255 (up to 510), and SQXTUN saturates to [0,255].

Alternative: If both src and dst were in 8B form, we could use `UQADD v0.8B, v0.8B, v4.8B` (saturating unsigned add) in one instruction. But they're in 4H form (16-bit per channel) at this point, so the ADD+SQXTUN is necessary.

**Savings:** 0
**Priority:** N/A

---

## Category 5: Fog Optimizations

### OPT-R2-18: Use MADD for fog table interpolation

**Location:** Lines 3507-3511
**Current code (3 insns):**
```
MUL w5, w5, w7           -- fraction * dfog
LSR w5, w5, #10          -- >> 10
ADD w4, w6, w5            -- fog_a = fog + interpolated
```
**Proposed:** `MADD w4, w5, w7, w6` would compute `w4 = w5*w7 + w6`, but we need the `>>10` between the multiply and the add. So MADD doesn't apply directly.

Could restructure as: `MADD w4, w5, w7, w6_shifted` but the shift needs to happen on the product, not the addend. No win here.

**Savings:** 0
**Priority:** N/A

---

### OPT-R2-19: Eliminate fogColor UXTL when fog_mode is constant

**Location:** Lines 3454-3457 vs 3460-3466
**Current code:**
- FOG_CONSTANT path (line 3457): `UQADD v0.8B, v0.8B, v11.8B` -- uses v11 directly as packed bytes. Good, no UXTL needed.
- Non-constant path (line 3466): `UXTL v3.8H, v11.8B` -- widens fogColor for math. This is necessary because the subsequent SUB and MUL operate on 16-bit lanes.

**Savings:** 0 (already optimized in Batch 7/M3)
**Priority:** N/A

---

## Category 6: Depth Test Optimizations

### OPT-R2-20: Eliminate depth write reload of STATE_new_depth

**Location:** Lines 4128-4137
**Current code:**
```
MOV  w4, w28                           -- x index
LDRH w5, [x0, #STATE_new_depth]        -- reload from state
STRH w5, [x9, x4, LSL #1]
```
But `new_depth` was just stored at line 2357: `STR w10, [x0, #STATE_new_depth]`.

The value in w10 may or may not survive to this point. Let me trace w10's uses between line 2357 and 4128:
- Line 2357 stores w10 (new_depth)
- Then texture fetch section (Phase 3): w10 is used extensively as scratch
- So w10 is definitely clobbered

**Proposed:** We could store new_depth in a spare register that survives the pipeline stages. But all callee-saved GPRs are taken. The value could be stored in a NEON lane (e.g., `FMOV s13, w10` or `INS v13.H[4], w10`). But v13 is used for color-before-fog.

Alternatively, we could avoid the STATE_new_depth store entirely by keeping the value in a NEON lane from the depth computation through to the depth write. But this is a very long liveness range spanning all pipeline stages.

**Savings:** 1 insn/pixel (LDRH eliminated, STR to state also eliminable)
**Priority:** LOW
**Risk:** HIGH (very long register liveness)

---

### OPT-R2-21: Consolidate x_tiled loads across depth test, depth write, fb write

**Location:** Multiple locations load STATE_x_tiled or use w28 (cached STATE_x)
**Pattern:** The x coordinate (tiled or not) is loaded multiple times:
1. Depth test (line 2392): `LDR w4, [x0, #STATE_x_tiled]`
2. Alpha blend dest read (line 3683): `LDR w5, [x0, #STATE_x_tiled]`
3. Alpha blend src read (line 3696): `LDR w4, [x0, #STATE_x_tiled]`
4. Depth write alpha path (line 3950): `LDR w4, [x0, #STATE_x_tiled]`
5. FB write (line 3986): `LDR w14, [x0, #STATE_x_tiled]`
6. Depth write Z path (line 4131): `LDR w4, [x0, #STATE_x_tiled]`

x_tiled is computed once per pixel (line 2194: `STR w5, [x0, #STATE_x_tiled]`) and never changes. We could cache it in a register.

**Problem:** The x_tiled value must survive from line 2194 through line 4131. That's essentially the entire pipeline. No scratch register survives this long.

**Proposed:** Cache x_tiled in a callee-saved register. But all are taken.

Alternative: Don't store x_tiled to state at all -- recompute it inline at each use site from w28 (cached STATE_x). The tiled computation is 4 instructions: AND+LSR+ADD_LSL+result. But that's 4 insns per use site vs 1 LDR per use site.

Or: Use the stack frame. Store x_tiled to a stack slot ([SP, #160] is padding, available) and LDR from there. This eliminates the dependency on the state struct but doesn't save instructions.

Actually, the LDR from state is fine -- it's an L1-cached load. The multiple loads are a code quality issue but not a performance issue on OoO cores that can pipeline L1 hits.

**Savings:** Negligible (L1 hits are fast, re-computation is more expensive)
**Priority:** LOW
**Risk:** N/A

---

## Category 7: Prologue/Epilogue Optimizations

### OPT-R2-22: Conditional NEON saves based on pipeline features

**Location:** Lines 1942-1960 (prologue), 4326-4345 (epilogue)
**Current code:** Always saves/restores d8-d15 (8 NEON registers = 4 STP/4 LDP = 8 insns).

**Proposed:** Some NEON callee-saved registers are only needed for specific pipeline features:
- v11: only used when `FOG_ENABLE`
- v14: only used when `dual_tmus`
- v15: always used (TMU0 ST deltas) -- but only if texture is enabled
- v12: always used (RGBA deltas)

If we conditionally skip saving/restoring unused registers, we save 1-2 STP/LDP pairs in specific configurations.

**Problem:** STP/LDP pairs must be 16-byte aligned offsets. Removing one pair shifts all subsequent offsets. This would require different stack layouts per configuration, complicating the epilogue.

Also, this is a per-BLOCK cost (once per scanline), not per-pixel. The savings are amortized across all pixels in the span. For a typical 100-pixel span, saving 2 instructions in the prologue is 0.02 insns/pixel.

**Savings:** 0.02-0.04 insns/pixel (negligible)
**Priority:** LOW
**Risk:** LOW

---

### OPT-R2-23: MOVZ with hw field for 64-bit pointer loads

**Location:** Lines 1988-2003 (EMIT_MOV_IMM64 macro), used 7 times in prologue
**Current code (4 insns each, 28 insns total):**
```
MOVZ Xd, #low16
MOVK Xd, #mid16, LSL #16
MOVK Xd, #mid32, LSL #32
MOVK Xd, #high16, LSL #48
```
**Proposed:** On macOS, user-space pointers are in the range `0x0000_0001_xxxx_xxxx` to `0x0000_0007_xxxx_xxxx`. The high halfword (bits 63:48) is always 0. So MOVK for hw=3 is unnecessary when the high halfword is 0.

Similarly, if any halfword is 0, the corresponding MOVK can be skipped (since MOVZ zeros everything, the 0 halfword is already correct).

**Implementation:** Change EMIT_MOV_IMM64 to conditionally emit only non-zero halfwords:
```c
#define EMIT_MOV_IMM64(d, ptr)                                      \
    do {                                                             \
        uint64_t _v = (uint64_t)(uintptr_t)(ptr);                   \
        addlong(ARM64_MOVZ_X((d), (_v) & 0xFFFF));                  \
        if (((_v) >> 16) & 0xFFFF)                                   \
            addlong(ARM64_MOVK_X((d), ((_v) >> 16) & 0xFFFF, 1));   \
        if (((_v) >> 32) & 0xFFFF)                                   \
            addlong(ARM64_MOVK_X((d), ((_v) >> 32) & 0xFFFF, 2));   \
        if (((_v) >> 48) & 0xFFFF)                                   \
            addlong(ARM64_MOVK_X((d), ((_v) >> 48) & 0xFFFF, 3));   \
    } while (0)
```

For macOS ARM64 pointers, hw=3 is always 0, saving 1 MOVK per pointer load. With 7 pointer loads in the prologue, that's 7 instructions saved per block.

Same optimization applies to EMIT_LOAD_NEON_CONST (lines 2025-2033), used 3 times = 3 more instructions.

Also applies to dither_rb pointer load (lines 3998-4003) and g_offset (lines 4068-4074) where higher halfwords may be zero.

**Savings:** ~10-14 insns/block (prologue-only, amortized over span)
**Priority:** MEDIUM
**Risk:** LOW (the check is compile-time JIT-time, not runtime)

---

## Category 8: Loop Structure Optimizations

### OPT-R2-24: Move STATE_x load out of loop (eliminate per-pixel LDR)

**Location:** Line 2090
**Current code:**
```
loop_jump_pos = block_pos;
LDR w28, [x0, #STATE_x]       -- reload STATE_x every iteration
```
**Analysis:** w28 is the cached STATE_x. It's stored at line 4309 (`STR w5, [x0, #STATE_x]`) and then updated at line 4311 (`MOV w28, w5`). So at the top of the loop, w28 already holds the correct x value from the previous iteration.

**Wait:** The first iteration needs the initial load. But subsequent iterations already have w28 set from line 4311. So the LDR at line 2090 is only needed for the FIRST iteration.

**Proposed:** Move the LDR before the loop (line 2087), and remove it from inside the loop body. The `MOV w28, w5` at line 4311 keeps it updated for subsequent iterations.

Wait, looking at this more carefully: the LDR at line 2090 is inside the loop (after loop_jump_pos). The first iteration loads from state; subsequent iterations have w28 from line 4311. But line 2090 loads from state every iteration, overwriting w28.

Is this redundant? After the per-pixel increments, w5 = new x (line 4303-4306), then `STR w5, [x0, #STATE_x]` (line 4309), then `MOV w28, w5` (line 4311). So w28 == STATE_x at the top of each iteration. The LDR at 2090 is loading the SAME value that's already in w28.

**This is a genuine redundancy!** The LDR w28 at line 2090 can be moved BEFORE loop_jump_pos (to handle the first iteration), and removed from the loop body.

**Savings:** 1 insn/pixel
**Priority:** HIGH
**Risk:** LOW (the value is provably the same)

---

### OPT-R2-25: Use SUBS+B.NE for loop control instead of CMP+B.NE

**Location:** Lines 4300-4319
**Current code:**
```
MOV w4, w28                    -- save old x
ADD/SUB w5, w4, #1             -- new x
STR w5, [x0, #STATE_x]
MOV w28, w5                    -- update cache
CMP w4, w27                    -- compare old x with bound
B.NE loop
```
**Proposed:** The MOV w4, w28 copies x, then immediately does ADD/SUB w5, w4, #1. We could instead:
```
ADD/SUB w5, w28, #1             -- new x (no need for w4 copy)
STR w5, [x0, #STATE_x]
CMP w28, w27                    -- compare old x with bound
MOV w28, w5                     -- update cache (after CMP uses old value)
B.NE loop
```
This eliminates the MOV w4, w28 at the cost of reordering the CMP and MOV. The CMP must use w28 (old value) before w28 is updated. This saves 1 instruction.

**Savings:** 1 insn/pixel
**Priority:** HIGH
**Risk:** LOW (reordering is safe, CMP before MOV)

---

## Category 9: Miscellaneous

### OPT-R2-26: Conditional texel_count increment could use ADD_IMM #2 directly

**Location:** Lines 4271-4290
**Current code:**
```
ADD x7, x0, #STATE_pixel_count
LDP w4, w5, [x7]
ADD w4, w4, #1
ADD w5, w5, #1  (or #2)
STP w4, w5, [x7]
```
For the non-texture path (lines 4286-4290):
```
LDR w4, [x0, #STATE_pixel_count]
ADD w4, w4, #1
STR w4, [x0, #STATE_pixel_count]
```

The x86-64 uses `ADD [mem], imm` which is 1 instruction. ARM64 needs LDR+ADD+STR (3 insns). No optimization possible due to ISA difference.

But: if `pixel_count` and `texel_count` were hoisted into GPRs and only stored at the end of the span (in the epilogue), we'd save 3-5 insns/pixel. This would require:
1. Load counts before loop
2. Increment in registers during loop
3. Store counts after loop

**Problem:** The counts must be correct if the function is interrupted (though this is a JIT function called from a single thread context). Also, no free GPRs.

The counts could use NEON: keep them in v-scratch lanes and use UMOV/INS at the end. But the complexity doesn't justify the savings for simple counter increments.

**Savings:** 3-5 insns/pixel (if hoisted to registers)
**Priority:** LOW (counter updates are cheap, likely absorbed by pipeline)
**Risk:** MEDIUM (register pressure)

---

### OPT-R2-27: Stipple pattern: eliminate MOV w5, w28 + MVN w5, w5

**Location:** Lines 2133-2136
**Current code:**
```
MOV w5, w28        -- copy STATE_x
MVN w5, w5         -- NOT x
AND w5, w5, #7     -- (~x) & 7
```
**Proposed:** Use `ORN` or bitfield extract. Actually, `MVN + AND` on the same register can be replaced by `BIC` or `EOR`:
```
EOR w5, w28, #7    -- x XOR 7 = ~x & 7 for the low 3 bits... wait, that's not right.
```
Actually `(~x) & 7` = `7 - (x & 7)` for unsigned values, but that's 2 instructions too. The MVN+AND pattern is the standard way on ARM64.

However, the MOV w5, w28 is needed because we want to preserve w28. But we could use w5 as the destination of MVN directly from w28:
```
MVN w5, w28        -- w5 = ~w28 (eliminates MOV)
AND w5, w5, #7
```
This saves 1 instruction.

**Savings:** 1 insn/pixel (stipple pattern path only)
**Priority:** LOW
**Risk:** LOW

---

### OPT-R2-28: Pattern stipple: use ORR with LSL to merge bit index

**Location:** Lines 2129-2139
**Current code (6 insns):**
```
AND  w4, w24, #3         -- real_y & 3
LSL  w4, w4, #3          -- << 3
MVN  w5, w28             -- (after OPT-R2-27: 1 insn)
AND  w5, w5, #7          -- (~x) & 7
ORR  w4, w4, w5          -- bit_index
```
**Proposed:** Use `ORR w4, w5, w4, LSL #3` to combine the shift and OR into one instruction. But we need AND before the ORR. No net savings.

**Savings:** 0
**Priority:** N/A

---

### OPT-R2-29: Use UBFX to extract dither R and B simultaneously

**Location:** Lines 4029-4031 (2x2) or 4039-4041 (4x4)
**Current code:**
```
UBFX w13, w4, #16, #8        -- R = byte2
AND  w6, w4, #0xFF            -- B = byte0 (AND_MASK with width 8)
```
This is already using UBFX efficiently. No optimization.

**Savings:** 0
**Priority:** N/A

---

### OPT-R2-30: No-dither RGB565 pack: replace ORR w4, w4, w5 with ORR+shift

**Location:** Lines 4101-4111
**Current code (Batch 7/M6, 6 insns):**
```
UBFX w5, w4, #3, #5          -- B >> 3
UBFX w6, w4, #10, #6         -- G >> 2 (from bit 10)
UBFX w4, w4, #19, #5         -- R >> 3 (from bit 19)
LSL  w4, w4, #11             -- R << 11
BFI  w4, w6, #5, #6          -- insert G at bits [10:5]
ORR  w4, w4, w5              -- insert B at bits [4:0]
```
This is already optimized from Batch 7. The x86-64 uses 7 instructions (MOV+MOVZX+SHR+SHR+SHL+AND+AND+OR+OR). We're at 6. Hard to do better.

**Alternative using EXTR or BFI:** We could try to pack everything with BFI:
```
UBFX w5, w4, #3, #5          -- B5
UBFX w6, w4, #10, #6         -- G6
UBFX w4, w4, #19, #5         -- R5
LSL  w4, w4, #11             -- R in position
BFI  w4, w6, #5, #6          -- G in position
BFI  w4, w5, #0, #5          -- B in position
```
Same 6 instructions. No improvement.

**Savings:** 0
**Priority:** N/A

---

## Category 10: Cross-Reference with x86-64

### OPT-R2-31: x86-64 uses PXOR XMM2, XMM2 (zero register) once -- ARM64 zeros with MOVI per-use

**Location:** Multiple MOVI_V2D_ZERO calls throughout the pipeline
**Analysis:** The x86-64 zeroes XMM2 once (line 853-856) and keeps it zeroed throughout the pipeline for use in `PUNPCKLBW XMM0, XMM2` (byte-to-word unpack). ARM64 uses `UXTL` for unpacking which doesn't need a zero register.

For other zeroing needs, ARM64 uses `MOVI Vd.2D, #0` which is a 1-instruction operation. The x86-64 approach of keeping a persistent zero register is not needed because MOVI is equally fast.

**Savings:** 0 (already handled differently)
**Priority:** N/A

---

### OPT-R2-32: x86-64 uses XMM11 for minus_254 constant -- ARM64 removed this

**Analysis:** Verified that v11 was repurposed for fogColor in Batch 7/M3. The `minus_254` constant was used in the original x86 fog path but the ARM64 fog path uses a different sequence that doesn't need it. No issue.

**Savings:** 0
**Priority:** N/A

---

### OPT-R2-33: x86-64 per-pixel increment section loads deltas from params every pixel

**Location:** x86-64 lines 3275-3292 load `params->dBdX`, `params->dZdX`, `params->tmu[0].dSdX`, `params->tmu[0].dWdX`, `params->dWdX` from memory every pixel.

**ARM64 status:** Already optimized in Batch 3 (H1) -- BGRA deltas are hoisted in v12, TMU ST deltas in v14/v15. However, dZdX, tmu0.dWdX, and dWdX are still loaded per-pixel (see OPT-R2-01/02 above). The x86-64 also loads them per-pixel, so we're on par.

**Savings:** 0 (parity with x86-64)
**Priority:** N/A

---

## Summary Table

| ID | Description | Savings (insns/pixel) | Priority | Risk | Notes |
|----|-------------|----------------------|----------|------|-------|
| **R2-24** | **Move STATE_x load before loop** | **1** | **HIGH** | **LOW** | Redundant LDR inside loop body |
| **R2-25** | **Eliminate MOV in loop control** | **1** | **HIGH** | **LOW** | Reorder CMP before MOV update |
| **R2-13** | **Eliminate MOV v16,v0 in cc multiply** | **1** | **MEDIUM** | **LOW** | v0 not modified before SMULL |
| **R2-12** | **DUP_V4H_GPR replaces FMOV+DUP** | **1** (per occurrence) | **MEDIUM** | **LOW** | Up to 7 sites, most conditional |
| **R2-07** | **Eliminate ebp_store round-trip** | **2** (bilinear only) | **MEDIUM** | **LOW** | Use x17 to hold bilinear shift |
| **R2-23** | **Skip zero halfwords in MOV_IMM64** | **~0.1** (amortized) | **MEDIUM** | **LOW** | ~10 insns/block saved in prologue |
| **R2-09** | **MOVZ x4, #1, LSL #48 (1 insn)** | **~0.03** (amortized) | **LOW** | **LOW** | 3 insns/block saved |
| **R2-06** | **Eliminate tex_s/tex_t store-load** | **3** (perspective) | **MEDIUM** | **HIGH** | Complex register lifetime |
| **R2-08** | **Cache original LOD for point-sample** | **1** (point-sample) | **LOW** | **LOW** | Save w6 before ADD #4 |
| **R2-27** | **MVN directly from w28 in stipple** | **1** (stipple only) | **LOW** | **LOW** | Eliminate MOV copy |
| **R2-11** | **Deduplicate MOVZ w10, #0xFF** | **1-4** (conditional) | **LOW** | **LOW** | Tracking complexity |
| **R2-01** | **Hoist dZdX** | **1** | **LOW** | **MEDIUM** | No free registers |
| **R2-16** | **Deduplicate alookup load** | **2** (rare combos) | **LOW** | **LOW** | Tracking complexity |
| **R2-26** | **Hoist pixel/texel counters** | **3-5** | **LOW** | **MEDIUM** | No free registers |
| **R2-20** | **Eliminate new_depth reload** | **1** | **LOW** | **HIGH** | Very long liveness |

### Recommended Implementation Order

**Batch 8 (safe, mechanical, ~3-4 insns/pixel):**
1. **R2-24**: Move STATE_x LDR before loop -- 1 insn/pixel
2. **R2-25**: Eliminate MOV in loop control -- 1 insn/pixel
3. **R2-13**: Eliminate redundant MOV v16,v0 -- 1 insn/pixel (conditional paths)
4. **R2-27**: MVN directly from w28 in stipple -- 1 insn/pixel (stipple path)

**Batch 9 (moderate complexity, ~2-3 insns/pixel):**
5. **R2-07**: Eliminate ebp_store round-trip -- 2 insns/pixel (bilinear)
6. **R2-12**: DUP_V4H_GPR -- 1 insn per site
7. **R2-08**: Cache LOD for point-sample -- 1 insn/pixel

**Batch 10 (prologue, one-time):**
8. **R2-23**: Skip zero halfwords in pointer loads
9. **R2-09**: MOVZ with hw for (1<<48) constant

### Total Estimated Savings

- **Per-pixel (hot path):** 2-4 instructions (R2-24, R2-25, R2-13)
- **Per-pixel (bilinear texture):** +2 instructions (R2-07)
- **Per-pixel (conditional paths):** +1-3 instructions (R2-12, R2-27, R2-08)
- **Per-block (prologue):** ~10-17 instructions (R2-23, R2-09)

### Overall Assessment

The codebase is well-optimized after 7 batches. The remaining opportunities are smaller wins (1-2 insns each) compared to the 5-15 insn wins of earlier batches. The two highest-impact findings (R2-24 and R2-25) are simple loop-structure improvements that remove 2 instructions from every pixel unconditionally. R2-13 is another easy win. Together, these three save ~3 instructions per pixel with zero risk.

The texture fetch section (R2-06, R2-07, R2-08) has moderate opportunities but higher complexity. The prologue optimizations (R2-23, R2-09) are one-time costs amortized over the entire span -- useful for very short spans but negligible for typical workloads.

No correctness-critical issues were found. The Batch 1 rounding fix (H7/H8) remains correct and cannot be simplified with URSHR (OPT-R2-15 confirmed this).
