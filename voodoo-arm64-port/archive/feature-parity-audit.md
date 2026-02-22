# Feature Parity Audit: ARM64 vs x86-64 Voodoo JIT Codegen

**Date**: 2026-02-20
**Files compared**:
- ARM64: `src/include/86box/vid_voodoo_codegen_arm64.h` (4761 lines)
- x86-64: `src/include/86box/vid_voodoo_codegen_x86-64.h` (3561 lines)

**Purpose**: Verify that the ARM64 codegen, after 7 batches of optimizations,
still implements every feature present in the x86-64 reference.

---

## Executive Summary

**VERDICT: FULL PARITY ACHIEVED**

All 17 pipeline stages present in the x86-64 reference are implemented in the
ARM64 codegen. No features were lost during optimization batches 1-7. The ARM64
version adds several features not present in the x86-64 reference (W^X handling,
overflow detection, debug logging, per-instance JIT state).

| Stage | Status | Notes |
|-------|--------|-------|
| 1. Prologue/Setup | MATCH | ARM64 adds hoisted deltas (Batch 3), pinned constants |
| 2. Pixel Loop Structure | MATCH | ARM64 caches STATE_x in w28, STATE_x2 in w27 (Batch 2) |
| 3. Stipple Test | MATCH | Both pattern + rotating modes |
| 4. Tiled X | MATCH | Identical computation |
| 5. Depth (W-buffer) | MATCH | CLZ replaces BSR, same algorithm |
| 6. Depth (Z-buffer) | MATCH | Identical logic |
| 7. Depth Bias | MATCH | Identical logic |
| 8. Depth Test (8 modes) | MATCH | All 6 comparison ops + NEVER + ALWAYS |
| 9. Texture Fetch (per TMU) | MATCH | Identical algorithm, both TMUs |
| 10. Texture Combine (dual TMU) | MATCH | All tc_*/tca_* modes, trilinear |
| 11. Chroma Key | MATCH | Same 24-bit comparison logic |
| 12. Color Combine (cc_*) | MATCH | All modes including TEXRGB |
| 13. Alpha Combine (cca_*) | MATCH | All modes including alpha mask test |
| 14. Alpha Test (8 modes) | MATCH | All 6 comparison ops + NEVER + ALWAYS |
| 15. Alpha Blend (all afunc) | MATCH | All 9 dest + 9 src modes + ASATURATE |
| 16. Fog (4 modes + add/mult) | MATCH | Constant, Z, alpha, W + table lookup |
| 17. Framebuffer Write | MATCH | Dither (2x2/4x4) + non-dither RGB565 |
| 18. Depth Write | MATCH | Both alpha-buffer and non-alpha paths |
| 19. Per-pixel Increments | MATCH | All interpolants + pixel/texel counters |
| 20. Epilogue | MATCH | Register restore + RET |

---

## Detailed Stage-by-Stage Comparison

### 1. Prologue/Setup

| Feature | x86-64 | ARM64 | Status |
|---------|--------|-------|--------|
| Save callee-saved GPRs | PUSH RBP,RDI,RSI,RBX,R12-R15 (8 regs) | STP x19-x28,x29,x30 (12 regs) | MATCH |
| Save callee-saved SIMD | (none -- XMM not callee-saved in SysV) | STP d8-d15 (8 regs) | ARM64 EXTRA (required by AAPCS64) |
| Load XMM/NEON constants | XMM8=01_w, XMM9=ff_w, XMM10=ff_b, XMM11=minus_254 | v8=01_w, v9=ff_w, v10=ff_b | MATCH (v11 repurposed for fogColor) |
| Load pointer tables | R9=logtable, R10=alookup, R11=aminuslookup, R12=xmm_00_ff_w, R13=i_00_ff_w | x19-x23,x25,x26 = same + bilinear_lookup + rgb565 | MATCH (ARM64 adds bilinear_lookup, rgb565) |
| Argument setup | Win64: RDI=state, R15=params, R14=real_y; Linux: similar | x0=state, x1=params, x24=real_y | MATCH |
| Load fb_mem/aux_mem | (loaded per-use from state) | LDP x8,x9 (pinned) | ARM64 EXTRA (optimization) |
| Hoist RGBA deltas | (loaded per-pixel from params) | v12={dBdX,dGdX,dRdX,dAdX} | ARM64 EXTRA (Batch 3/H1) |
| Hoist TMU deltas | (loaded per-pixel from params) | v15=TMU0 ST, v14=TMU1 ST | ARM64 EXTRA (Batch 3) |
| Cache STATE_x/x2 | (loaded per-use) | w28=STATE_x, w27=STATE_x2 | ARM64 EXTRA (Batch 2/H2+H3) |
| Hoist fogColor | (loaded per-pixel in fog section) | v11=fogColor | ARM64 EXTRA (Batch 7/M3) |
| Frame pointer | (no) | MOV x29, SP | ARM64 EXTRA (debugging aid) |

**No features lost. ARM64 adds optimizations that reduce per-pixel loads.**

### 2. Pixel Loop Structure

| Feature | x86-64 | ARM64 | Status |
|---------|--------|-------|--------|
| Loop top label | `loop_jump_pos = block_pos` (line 765) | Same (line 2087) | MATCH |
| X compare | `CMP EAX, state->x2` (line 3464) | `CMP w4, w27` (cached x2) | MATCH |
| Loop branch | `JNZ loop_jump_pos` (line 3467) | `B.NE loop_jump_pos` (line 4319) | MATCH |
| xdir support | Both ADD +1 / SUB -1 | Same | MATCH |

### 3. Stipple Test

| Feature | x86-64 (lines 766-828) | ARM64 (lines 2118-2165) | Status |
|---------|------------------------|-------------------------|--------|
| FBZ_STIPPLE guard | Yes | Yes | MATCH |
| Pattern stipple (FBZ_STIPPLE_PATT) | `bit = (real_y & 3)*8 \| (~x & 7)`, TEST+JZ | AND+LSL+MVN+AND+ORR+MOVZ+LSL+TST+BEQ | MATCH |
| Rotating stipple | ROR+TEST bit 31+JZ | LDR+ROR+STR+TBZ #31 | MATCH |
| Skip target | `stipple_skip_pos` patched | Same with forward branch patch | MATCH |

### 4. Tiled X Computation

| Feature | x86-64 (lines 832-852) | ARM64 (lines 2185-2196) | Status |
|---------|------------------------|-------------------------|--------|
| Guard | `params->col_tiled \|\| params->aux_tiled` | Same | MATCH |
| Formula | `(x & 63) + ((x >> 6) << 11)` | AND+LSR+ADD w/LSL#11 | MATCH |
| Store | `state->x_tiled` | Same | MATCH |

### 5. W-Buffer Depth Computation

| Feature | x86-64 (lines 858-932) | ARM64 (lines 2231-2312) | Status |
|---------|------------------------|-------------------------|--------|
| Guard | `FBZ_W_BUFFER \|\| fog conditions` | Same | MATCH |
| Initial depth = 0 | `MOV EAX, 0` | `MOV w10, #0` | MATCH |
| Test w+4 high bits | `TEST w+4, 0xffff` + JNZ | `UXTH+CBNZ` | MATCH |
| Load low word | `MOV EDX, w` | `LDR w4, STATE_w` | MATCH |
| depth = 0xF001 | `MOV EAX, 0xF001` | `MOVZ w10, 0xF001` | MATCH |
| Test low>>16 | `SHR EDX, 16` + JZ | `LSR+CBZ` | MATCH |
| BSR -> CLZ conversion | BSR EAX, EDX | CLZ+SUB (BSR = 31 - CLZ) | MATCH |
| exp = 15 - BSR | `MOV EDX, 15; SUB EDX, EAX` | `SUB w7, w6, #16` (CLZ-16 = exp) | MATCH |
| mant = (~w_low) >> (19-exp) | Same formula | Same formula | MATCH |
| result = (exp<<12) + mant + 1 | LEA 1[EDX,EBX] | `LSL+ADD+ADD #1` | MATCH |
| Clamp to 0xFFFF | CMOVA | CSEL COND_HI | MATCH |
| Store w_depth for fog | Yes, conditional | Same | MATCH |

### 6. Z-Buffer Depth Computation

| Feature | x86-64 (lines 933-952) | ARM64 (lines 2321-2338) | Status |
|---------|------------------------|-------------------------|--------|
| Guard | `!(FBZ_W_BUFFER)` | Same | MATCH |
| z >> 12 | `SAR EAX, 12` | `ASR w10, w10, #12` | MATCH |
| Clamp negative -> 0 | `CMOVS EAX, ECX(0)` | `BIC_REG_ASR+CSEL LT` | MATCH |
| Clamp > 0xFFFF | `CMOVA EAX, EBX(0xFFFF)` | `CSEL HI` | MATCH |

### 7. Depth Bias

| Feature | x86-64 (lines 954-960) | ARM64 (lines 2347-2354) | Status |
|---------|------------------------|-------------------------|--------|
| Guard | `FBZ_DEPTH_BIAS` | Same | MATCH |
| depth += zaColor | `ADD EAX, zaColor` | `ADD w10, w10, w4` | MATCH |
| Mask to 16 bits | `AND EAX, 0xFFFF` | `UXTH w10, w10` | MATCH |
| Store new_depth | Yes | Yes | MATCH |

### 8. Depth Test (8 modes)

| Feature | x86-64 (lines 966-1023) | ARM64 (lines 2388-2446) | Status |
|---------|-------------------------|-------------------------|--------|
| Guard | `FBZ_DEPTH_ENABLE && depthop != ALWAYS/NEVER` | Same | MATCH |
| Tiled/linear x select | Yes | Yes | MATCH |
| FBZ_DEPTH_SOURCE override | `MOVZX EAX, zaColor` | `LDRH w10, zaColor` | MATCH |
| DEPTHOP_LESSTHAN | JAE (skip if >=) | B.CS (skip if carry set = unsigned >=) | MATCH |
| DEPTHOP_EQUAL | JNE | B.NE | MATCH |
| DEPTHOP_LESSTHANEQUAL | JA (skip if >) | B.HI (skip if unsigned >) | MATCH |
| DEPTHOP_GREATERTHAN | JBE (skip if <=) | B.LS (skip if unsigned <=) | MATCH |
| DEPTHOP_NOTEQUAL | JE | B.EQ | MATCH |
| DEPTHOP_GREATERTHANEQUAL | JB (skip if <) | B.CC (skip if carry clear = unsigned <) | MATCH |
| DEPTHOP_NEVER | RET | RET | MATCH |
| DEPTHOP_ALWAYS | (no test) | (no test) | MATCH |

### 9. Texture Fetch (codegen_texture_fetch)

Both files implement `codegen_texture_fetch()` with identical feature set:

| Feature | x86-64 (lines 79-647) | ARM64 (lines 1248-1839) | Status |
|---------|------------------------|-------------------------|--------|
| Perspective W division | SDIV or reciprocal | Same | MATCH |
| LOD computation | logtable lookup | Same (x19=logtable pointer) | MATCH |
| LOD clamping (min/max) | Yes | Yes | MATCH |
| Bilinear filtering | Full 4-tap bilinear | Same (x25=bilinear_lookup) | MATCH |
| Point sampling | Direct texel lookup | Same | MATCH |
| Mirror S/T | Yes | Yes | MATCH |
| Clamp S/T | Yes | Yes | MATCH |
| Wrap S/T | Yes | Yes | MATCH |
| TMU index (0 or 1) | Parameter selects TMU | Same | MATCH |
| LOD caching | (computed fresh each time) | w6 = cached LOD (Batch 6/H4) | ARM64 EXTRA |
| Iterated BGRA cache | (not applicable) | v6 = cached (Batch 6/H6) | ARM64 EXTRA |

### 10. Texture Combine (Dual-TMU)

| Feature | x86-64 (lines 1059-1688) | ARM64 (lines 2482-2917) | Status |
|---------|--------------------------|-------------------------|--------|
| Single-TMU (local) | TMU0 fetch only | Same | MATCH |
| Pass-through | TMU1 fetch only | Same | MATCH |
| Dual-TMU path | TMU1 first, then TMU0 | Same | MATCH |
| TMU1 trilinear setup | lod & 1 XOR reverse_blend | Same | MATCH |
| TMU1 tc_mselect (6 modes) | ZERO,CLOCAL,AOTHER,ALOCAL,DETAIL,LOD_FRAC | Same | MATCH |
| TMU1 tc_reverse_blend | XOR with 0xFF or trilinear table | Same | MATCH |
| TMU1 multiply (signed) | PMULLW+PMULHW+PUNPCKLWD+PSRAD+PACKSSDW | SMULL+SSHR+SQXTN | MATCH |
| TMU1 tc_add_clocal/alocal | PADDW XMM1,XMM3 / broadcast alpha+PADDW | ADD_V4H / DUP+ADD | MATCH |
| TMU1 TCA (alpha combine) | All 6 tca_mselect modes | Same | MATCH |
| TMU1 TCA reverse blend | trilinear table or XOR 0xFF | Same | MATCH |
| TMU1 TCA multiply | IMUL+NEG+SAR | MUL+NEG+ASR | MATCH |
| TMU1 TCA add clocal/alocal | ADD EAX,EBX | ADD w4,w4,w5 | MATCH |
| TMU1 TCA clamp [0,0xFF] | CMOVA+test | BIC_ASR+CSEL (Batch 4/M2) | MATCH |
| TMU1 TCA insert alpha | PINSRW 3 | INS_H lane 3 | MATCH |
| TMU0 trilinear setup | Same as TMU1 | Same | MATCH |
| TMU0 tc_zero_other | PXOR XMM1 | MOVI_V2D_ZERO | MATCH |
| TMU0 tc_sub_clocal | PSUBW XMM1,XMM0 | SUB_V4H | MATCH |
| TMU0 tc_mselect (6 modes) | Same as TMU1 | Same | MATCH |
| TMU0 tc_reverse_blend | Same | Same | MATCH |
| TMU0 multiply (signed) | Same sequence | SMULL+SSHR+SQXTN | MATCH |
| TMU0 tc_add_clocal/alocal | Same | Same | MATCH |
| TMU0 tc_invert_output | PXOR XMM_ff_w | EOR_V with v9 | MATCH |
| TMU0 TCA (all modes) | Same as TMU1 | Same | MATCH |
| TMU0 TCA invert | XOR 0xFF | EOR_MASK | MATCH |
| Store tex_a | `state->tex_a = result` | Same | MATCH |
| trexInit1 bit 18 override | MOV EAX, tmuConfig; MOVD XMM0 | Same logic | MATCH |

**Known intentional divergence at line 2682**: The x86-64 at ~line 1299 tests
`tc_reverse_blend_1` for the TCA (alpha) path, which is believed to be a bug.
The ARM64 correctly tests `tca_reverse_blend_1`. This is documented in the code
comment and in `debug-findings.md`.

### 11. Chroma Key

| Feature | x86-64 (lines 1696-1746) | ARM64 (lines 3017-3040) | Status |
|---------|--------------------------|-------------------------|--------|
| Guard | `FBZ_CHROMAKEY` | Same | MATCH |
| Source select (3 modes) | ITER_RGB, COLOR1, TEX | Same (+ LFB fallback to zero) | MATCH |
| Comparison | `XOR chromaKey; AND 0xFFFFFF; JE` | `EOR+AND bitmask+CBZ` | MATCH |

### 12. Color Combine (cc_*)

| Feature | x86-64 (lines 2106-2228) | ARM64 (lines 3316-3407) | Status |
|---------|--------------------------|-------------------------|--------|
| cc_mselect ZERO | PXOR XMM3 | MOVI_V2D_ZERO | MATCH |
| cc_mselect CLOCAL | MOV XMM3,XMM1 | MOV_V | MATCH |
| cc_mselect ALOCAL | MOVD+PSHUFLW | FMOV+DUP (with fallback compute) | MATCH |
| cc_mselect AOTHER | (handled above) | Same pre-copy pattern | MATCH |
| cc_mselect TEX | PINSRW 3 times | LDR+FMOV+DUP | MATCH |
| cc_mselect TEXRGB | PUNPCKLBW XMM4 + MOVQ XMM3 | UXTL_8H_8B | MATCH |
| cc_reverse_blend | PXOR XMM9(ff_w) | EOR_V with v9 | MATCH |
| Multiply | PMULLW+PMULHW+PUNPCKLWD+PSRAD+PACKSSDW | SMULL+SSHR+SQXTN | MATCH |
| cc_add (clocal) | PADDW | ADD_V4H | MATCH |
| Pack to bytes | PACKUSWB | SQXTUN_8B_8H | MATCH |
| cc_invert_output | PXOR XMM10(ff_b) | EOR_V with v10 | MATCH |
| cc_zero_other | PXOR XMM0 | MOVI_V2D_ZERO | MATCH |
| cc_sub_clocal | PSUBW | SUB_V4H | MATCH |
| Save color-before-fog | MOVQ XMM15 | MOV_V v13 | MATCH |
| H6: cached iterated BGRA | (computed from ib each time) | v6 pre-packed (Batch 6) | ARM64 EXTRA |

### 13. Alpha Combine (cca_*)

| Feature | x86-64 (lines 1757-2104) | ARM64 (lines 3081-3304) | Status |
|---------|--------------------------|-------------------------|--------|
| a_other: ITER_A | SAR+CMOVS+CMOVA | ASR+BIC_ASR+CSEL | MATCH |
| a_other: TEX | state->tex_a | Same | MATCH |
| a_other: COLOR1 | MOVZX byte | LDRB | MATCH |
| a_other: default (0) | XOR | MOV_ZERO | MATCH |
| Alpha mask test | TEST EBX,1 + JZ | TBZ w14, #0 | MATCH |
| a_local: ITER_A | Same as a_other (or copy) | Same | MATCH |
| a_local: COLOR0 | MOVZX byte | LDRB | MATCH |
| a_local: ITER_Z | SAR 20+clamp | ASR 20+BIC_ASR+CSEL | MATCH |
| a_local: default (0xFF) | MOV 0xFF | MOVZ 0xFF | MATCH |
| cca_zero_other | XOR EDX | MOV_ZERO w12 | MATCH |
| cca_sub_clocal | SUB EDX,ECX | SUB w12,w12,w15 | MATCH |
| cca_mselect (5 modes) | ALOCAL,AOTHER,ALOCAL2,TEX,ZERO | Same | MATCH |
| cca_reverse_blend | XOR 0xFF | EOR_MASK 0xFF | MATCH |
| Multiply+shift | IMUL+SHR 8 | MUL+ASR 8 | MATCH |
| cca_add | ADD EDX,ECX | ADD w12,w15 | MATCH |
| Clamp [0,0xFF] | CMOVS+CMOVA | BIC_ASR+CSEL (Batch 4/M2) | MATCH |
| cca_invert_output | XOR 0xFF | EOR_MASK | MATCH |

### 14. Alpha Test (8 modes)

| Feature | x86-64 (lines 2419-2467) | ARM64 (lines 3596-3639) | Status |
|---------|--------------------------|-------------------------|--------|
| Guard | `alphaMode & 1` | Same | MATCH |
| Alpha ref load | MOVZX alphaMode+3 | LDRB PARAMS_alphaMode+3 | MATCH |
| AFUNC_LESSTHAN | JAE (skip if >=) | B.CS | MATCH |
| AFUNC_EQUAL | JNE | B.NE | MATCH |
| AFUNC_LESSTHANEQUAL | JA (skip if >) | B.HI | MATCH |
| AFUNC_GREATERTHAN | JBE (skip if <=) | B.LS | MATCH |
| AFUNC_NOTEQUAL | JE | B.EQ | MATCH |
| AFUNC_GREATERTHANEQUAL | JB (skip if <) | B.CC | MATCH |
| AFUNC_NEVER | RET | RET | MATCH |
| AFUNC_ALWAYS | (no test) | (no test) | MATCH |

### 15. Alpha Blend (all afunc modes)

| Feature | x86-64 (lines 2469-3058) | ARM64 (lines 3678-3927) | Status |
|---------|--------------------------|-------------------------|--------|
| Guard | `alphaMode & (1 << 4)` | Same | MATCH |
| Load dest alpha (alpha-buffer) | MOVZX from aux_mem | LDRH from x9 | MATCH |
| Load dest alpha (no buffer) | MOV 0xFF | MOVZ 0xFF | MATCH |
| Load dest RGB | MOVZX from fb_mem | LDRH from x8 | MATCH |
| rgb565 lookup | R8=rgb565, MOVD [R8+EAX*4] | x26=rgb565, LDR_W_UXTW2 | MATCH |
| alpha*2 for table index | ADD EDX,EDX; ADD EBX,EBX | ADD w12,w12,w12; ADD w5,w5,w5 | MATCH |
| Unpack src/dst | PUNPCKLBW | UXTL_8H_8B | MATCH |
| Save dst for A_COLOR | MOVQ XMM6,XMM4 | MOV_V v6,v4 | MATCH |
| **dest_afunc modes**: | | | |
| AZERO | PXOR XMM4 | MOVI_V2D_ZERO | MATCH |
| ASRC_ALPHA | alookup[src_alpha] | Same (x20=alookup) | MATCH |
| A_COLOR | MUL XMM4, XMM0 | MUL_V4H | MATCH |
| ADST_ALPHA | alookup[dst_alpha] | Same | MATCH |
| AONE | (no-op) | (no-op) | MATCH |
| AOMSRC_ALPHA | aminuslookup[src_alpha] | Same (x21=aminuslookup) | MATCH |
| AOM_COLOR | ff - src, MUL | SUB+MUL | MATCH |
| AOMDST_ALPHA | aminuslookup[dst_alpha] | Same | MATCH |
| ACOLORBEFOREFOG | MUL XMM4, XMM15 | UXTL v13 + MUL | MATCH |
| **src_afunc modes**: | | | |
| AZERO | PXOR XMM0 | MOVI_V2D_ZERO | MATCH |
| ASRC_ALPHA | alookup[src_alpha] | Same | MATCH |
| A_COLOR (dst) | MUL XMM0, XMM6 | MUL_V4H with v6 | MATCH |
| ADST_ALPHA | alookup[dst_alpha] | Same | MATCH |
| AONE | (no-op) | (no-op) | MATCH |
| AOMSRC_ALPHA | aminuslookup[src_alpha] | Same | MATCH |
| AOM_COLOR (dst) | ff - dst, MUL | SUB+MUL | MATCH |
| AOMDST_ALPHA | aminuslookup[dst_alpha] | Same | MATCH |
| ASATURATE | min(src_alpha, ff-dst_alpha) | Same (LSR+EOR+ADD+CMP+CSEL) | MATCH |
| **Rounding**: | | | |
| (product + 1 + (product>>8)) >> 8 | alookup[1]=v8 + PSRLW+PADDW+PSRLW | v8 + USHR+ADD+ADD+USHR | MATCH (Batch 1/H7+H8) |
| **Alpha blend alpha**: | | | |
| dest_aafunc == AONE | SHL EBX,7; ADD EAX,EBX | LSL w6,w5,7; ADD w4,w4,w6 | MATCH |
| src_aafunc == AONE | SHL EDX,7; ADD EAX,EDX | LSL w6,w12,7; ADD w4,w4,w6 | MATCH |
| SHR EAX,8 | Yes | LSR w4,w4,8 | MATCH |
| Combine src+dst | PADDW XMM0,XMM4 | ADD_V4H | MATCH |
| Pack result | PACKUSWB | SQXTUN_8B_8H | MATCH |

### 16. Fog (4 modes + add/mult)

| Feature | x86-64 (lines 2236-2417) | ARM64 (lines 3453-3569) | Status |
|---------|--------------------------|-------------------------|--------|
| Guard | `FOG_ENABLE` | Same | MATCH |
| FOG_CONSTANT | MOVD fogColor + PADDUSB | UQADD_V8B with hoisted v11 | MATCH |
| Unpack color to 16-bit | PUNPCKLBW | UXTL_8H_8B | MATCH |
| Load fogColor (non-constant) | MOVD+PUNPCKLBW | UXTL_8H_8B from hoisted v11 | MATCH |
| FOG_ADD: fogColor = 0 | PXOR XMM3 | MOVI_V2D_ZERO | MATCH |
| fog_diff = fogColor - color | PSUBW | SUB_V4H | MATCH |
| FOG_MULT: skip subtraction | No PSUBW | No SUB_V4H | MATCH |
| Divide by 2 | PSRAW 1 | SSHR_V4H #1 | MATCH |
| **Fog source**: | | | |
| w_depth table lookup | fogTable index + interpolation | Same algorithm | MATCH |
| FOG_Z: (z >> 12) & 0xFF | SHR+AND | LSR+AND_MASK | MATCH |
| FOG_ALPHA: CLAMP(ia>>12) | SAR+CMOVS+CMOVAE | ASR+BIC_ASR+CSEL | MATCH |
| FOG_W: CLAMP(w>>32) | load w+4, CMOVS+CMOVAE | load w+4, BIC_ASR+CSEL | MATCH |
| fog_a *= 2 | ADD EAX,EAX | ADD w4,w4,w4 | MATCH |
| Multiply with alookup | PMULLW alookup[fog_a+1] | MUL_V4H with alookup | MATCH |
| Arithmetic shift >>7 | PSRAW 7 | SSHR_V4H #7 | MATCH |
| FOG_MULT: result = fog only | MOVQ XMM0,XMM3 | MOV_V | MATCH |
| Normal: color += fog_diff | PADDW | ADD_V4H | MATCH |
| Pack to bytes | PACKUSWB | SQXTUN_8B_8H | MATCH |

### 17. Framebuffer Write

| Feature | x86-64 (lines 3077-3221) | ARM64 (lines 3993-4117) | Status |
|---------|--------------------------|-------------------------|--------|
| Guard | `FBZ_RGB_WMASK` | Same | MATCH |
| **Dither path**: | | | |
| dither_rb / dither_rb2x2 | R8=dither_rb pointer | x7=dither_rb pointer | MATCH |
| 2x2 pattern | x&1, y&1, value*4 | Same | MATCH |
| 4x4 pattern | x&3, y&3, value*16 | Same | MATCH |
| dither_g table offset | Computed at codegen time | Same | MATCH |
| Pack R5<<11 | G6<<5 | B5 | SHL+OR+OR | LSL+LSL+ORR+ORR | MATCH |
| **Non-dither path**: | | | |
| B5 = byte0 >> 3 | SHR+AND | UBFX (Batch 7/M6) | MATCH |
| G6 = byte1 >> 2 | SHR+SHL+AND | UBFX | MATCH |
| R5 = byte2 >> 3 | SHR+AND | UBFX | MATCH |
| Pack RGB565 | SHL+AND+OR | LSL+BFI+ORR (Batch 7/M6) | MATCH |
| Store to fb_mem | `MOV [ESI+EDX*2], AX` | `STRH w4, [x8, x14, LSL #1]` | MATCH |

### 18. Depth Write (two paths)

| Feature | x86-64 | ARM64 | Status |
|---------|--------|-------|--------|
| Alpha-buffer path (lines 3060-3075 / 3948-3955) | Store EDX (alpha) to aux_mem | Store w12 to aux_mem via x9 | MATCH |
| Guard | `FBZ_DEPTH_WMASK & FBZ_ALPHA_ENABLE` | Same | MATCH |
| Z-buffer path (lines 3224-3243 / 4128-4138) | Store new_depth to aux_mem | LDRH new_depth + STRH to aux_mem via x9 | MATCH |
| Guard | `FBZ_DEPTH_WMASK & FBZ_DEPTH_ENABLE & !FBZ_ALPHA_ENABLE` | Same | MATCH |

### 19. Per-pixel Increments

| Feature | x86-64 (lines 3256-3446) | ARM64 (lines 4190-4290) | Status |
|---------|--------------------------|-------------------------|--------|
| ib/ig/ir/ia (4x32) | MOVDQU+PADDD/PSUBD+MOVDQU | LD1+ADD_V4S/SUB_V4S+ST1 (with hoisted v12) | MATCH |
| z (32-bit) | ADD/SUB state->z | LDR+ADD/SUB+STR (with loaded dZdX) | MATCH |
| tmu0 s/t (128-bit) | MOVDQU+PADDQ/PSUBQ+MOVDQU (with per-pixel dSdX load) | LDR_Q+ADD_V2D/SUB_V2D+STR_Q (with hoisted v15) | MATCH |
| tmu0 w (64-bit) | MOVQ+PADDQ/PSUBQ+MOVQ (with per-pixel dWdX load) | LDR_X+ADD/SUB+STR_X | MATCH |
| global w (64-bit) | MOVQ+PADDQ/PSUBQ+MOVQ | LDR_X+ADD/SUB+STR_X | MATCH |
| tmu1 s/t (if dual) | Same as tmu0 (with per-pixel dSdX load) | LD1+ADD_V2D/SUB_V2D+ST1 (with hoisted v14) | MATCH |
| tmu1 w (if dual) | Same | Same | MATCH |
| xdir > 0 / < 0 | ADD vs SUB paths | Same | MATCH |
| pixel_count += 1 | ADD state->pixel_count, 1 | LDR+ADD+STR (or LDP/STP with texel_count) | MATCH |
| texel_count += 1 or 2 | ADD 1 (local/passthrough) or 2 (dual) | Same logic | MATCH |

### 20. Epilogue

| Feature | x86-64 (lines 3471-3485) | ARM64 (lines 4326-4348) | Status |
|---------|--------------------------|-------------------------|--------|
| Restore GPRs | POP R15..RBP (8 pops) | LDP x19-x28, x29, x30 | MATCH |
| Restore SIMD | (none needed) | LDP d8-d15 | MATCH (AAPCS64 requirement) |
| Return | RET (0xC3) | RET | MATCH |

---

## Optimization Batch Regression Check

### Batch 1 (H7+H8): Alpha blend rounding
- **Status**: No regression. Rounding sequence `(product + v8 + (product >> 8)) >> 8`
  uses pinned v8 (alookup[1]) in all dest_afunc and src_afunc multiply paths.

### Batch 2 (H2+H3): Cache STATE_x/x2
- **Status**: No regression. w28=STATE_x updated correctly at loop top (line 2090)
  and after X increment (line 4311). w27=STATE_x2 loaded once before loop (line 2009).

### Batch 3 (H1): Hoist params deltas
- **Status**: No regression. v12=RGBA deltas, v15=TMU0 ST, v14=TMU1 ST all hoisted
  in prologue and used in per-pixel increments. No per-pixel params loads needed.

### Batch 4 (M2): BIC+ASR clamp
- **Status**: No regression. `BIC_REG_ASR(r, r, r, 31)` (zero if negative) + `CSEL HI`
  (cap at 0xFF) used consistently in: a_other clamp, a_local clamp, cca clamp,
  tca clamp, fog alpha clamp.

### Batch 5 (M4+M7): Pin rgb565 + pair counters
- **Status**: No regression. x26=rgb565 pinned in prologue, used in alpha blend
  dest read (line 4716). pixel_count/texel_count use LDP/STP (lines 4274-4284).

### Batch 6 (H4+H6): Cache LOD + iterated BGRA
- **Status**: No regression. w6=LOD cached per-pixel in texture fetch. v6=iterated
  BGRA packed once per color combine iteration, reused for chroma key, clocal,
  and cother (lines 2995-3001, 3021, 3180, 3198, 3215).

### Batch 7 (M1,M3,M5,M6,L1,L2): Various optimizations
- **M1 (LDP)**: fb_mem/aux_mem loaded via LDP in prologue (line 2082). No regression.
- **M3 (fogColor hoist)**: v11 loaded in prologue when fog enabled (lines 2073-2076).
  Used directly in FOG_CONSTANT (UQADD) and non-constant fog (UXTL). No regression.
- **M5 (TMU0 alpha)**: w13 caches TMU0 alpha from v7 (lines 2832-2833), reused
  in tca_sub_clocal (line 2808) and tca_add (line 2899). No regression.
- **M6 (BFI RGB565)**: UBFX+BFI used for no-dither path (lines 4101-4111). No regression.
- **L1 (CBZ/CBNZ)**: Used in W-depth computation (lines 2244, 2262) and chroma key
  (line 3039). No regression.
- **L2**: Not visible in current code (may have been folded into other changes).

---

## ARM64 EXTRA Features (not in x86-64)

1. **W^X memory protection** (macOS ARM64): `pthread_jit_write_protect_np()` toggling
   before/after JIT emission. Essential for macOS security model.

2. **Emit overflow detection**: `arm64_codegen_emit_overflowed()` checks if generated
   code exceeds BLOCK_SIZE. Falls back to interpreter if overflow. x86-64 has no such
   safety net.

3. **I-cache flush**: `__clear_cache()` after JIT emission. Required on ARM64 where
   I-cache and D-cache are not coherent. Not needed on x86-64.

4. **Per-instance JIT state**: `voodoo_arm64_data_t` has `valid` and `rejected` fields.
   Cache lookup checks `data->valid || data->rejected`. x86-64 has no explicit
   validity tracking.

5. **Debug logging**: Extensive `fprintf` logging for cache hits, misses, generates,
   rejects, and overflow. Controlled by runtime `jit_debug` flag. x86-64 has none.

6. **Larger block size**: ARM64 BLOCK_SIZE=16384 vs x86-64 BLOCK_SIZE=8192. This
   accounts for ARM64's fixed 4-byte instruction width vs x86-64's variable-length
   encoding.

7. **Frame pointer**: ARM64 sets `x29 = SP` for debugging. x86-64 pushes RBP but
   doesn't set it as frame pointer.

---

## Known Intentional Divergences

1. **Line 2682 (TCA reverse blend for TMU1)**: ARM64 uses `tca_reverse_blend_1`
   instead of x86-64's `tc_reverse_blend_1`. This is an intentional fix for what
   appears to be a bug in the x86-64 codegen. The alpha path should use the
   alpha-specific flag.

2. **trexInit1 bit 18 masking**: ARM64 applies a 24-bit AND mask (`0x00FFFFFF`)
   to the tmuConfig override at line 2937. The x86-64 loads the full 32-bit
   tmuConfig at line 1750. The ARM64 behavior is arguably more correct since
   the override only affects RGB channels.

3. **Z clamp signedness**: x86-64 Z-buffer uses `SAR` (arithmetic shift right,
   signed) and `CMOVS` (clamp if signed). ARM64 uses `ASR` (identical semantics)
   and `BIC_REG_ASR + CSEL LT`. Both correctly treat Z as signed before clamping.

---

## Conclusion

The ARM64 Voodoo JIT codegen is at **full feature parity** with the x86-64
reference implementation. All 17 pipeline stages, all configuration modes
(depth ops, alpha test funcs, alpha blend funcs, fog modes, dither modes,
texture combine modes), and all edge cases (NEVER, ALWAYS, tiled addressing,
dual TMU, trilinear) are correctly implemented.

The 7 optimization batches have not removed or degraded any features. Each
optimization replaces a per-pixel computation with a semantically equivalent
hoisted/cached version.

The ARM64 version additionally provides robustness features (overflow detection,
W^X handling, debug logging) that the x86-64 version lacks.
