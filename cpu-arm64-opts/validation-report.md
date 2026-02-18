# ARM64 CPU JIT Backend -- Phase 1 & Phase 2 Validation Report

**Date**: 2026-02-17
**Branch**: `86box-arm64-cpu`
**Commits validated**: `53e5658b2` (Phase 1), `32ecb6448` (Phase 2)
**Validator**: Claude Opus 4.6 (cpu-jit-debug agent)

---

## Phase 1: PFRSQRT Bug Fix + 3DNow! FRECPE/FRSQRTE

### 1.1 Opcode Encoding Verification

All four new opcodes validated against ARM Architecture Reference Manual (DDI 0487) bit-by-bit.

| Opcode | Stored | Match |
|--------|--------|-------|
| `OPCODE_FRECPE_V2S` | `0x0ea1d800` | PASS |
| `OPCODE_FRSQRTE_V2S` | `0x2ea1d800` | PASS |
| `OPCODE_FRECPS_V2S` | `0x0e20fc00` | PASS |
| `OPCODE_FRSQRTS_V2S` | `0x0ea0fc00` | PASS |
| `OPCODE_FMUL_V2S` (existing) | `0x2e20dc00` | PASS |

### 1.2 Emitter Register Field Placement

Single-operand emitters (FRECPE, FRSQRTE): `Rd` at bits [4:0], `Rn` at bits [9:5]. Matches ARM encoding. -- PASS

Three-operand emitters (FRECPS, FRSQRTS): `Rd` at bits [4:0], `Rn` at bits [9:5], `Rm` at bits [20:16]. Matches ARM three-same encoding. -- PASS

### 1.3 Newton-Raphson Operand Order

**PFRCP**: FRECPS computes `2.0 - Vn * Vm`. With `Vn=dest_reg` (estimate) and `Vm=src_reg_a` (original value): `temp = 2.0 - estimate * value`. Correct NR refinement for reciprocal. -- PASS

**PFRSQRT**: FRSQRTS computes `(3.0 - Vn * Vm) / 2.0`. With `Vn=REG_V_TEMP` (=dest^2) and `Vm=src_reg_a` (original value): correct NR refinement for reciprocal square root. Both use fused multiply internally (single rounding). -- PASS

### 1.4 Precision Analysis

ARM minimum guarantee: FRECPE/FRSQRTE produce at least 8 bits of mantissa accuracy. Apple Silicon provides ~12 bits; Cortex-A53/A55 provide ~8 bits.

AMD 3DNow! requirements: PFRCP >= 14-bit, PFRSQRT >= 15-bit.

After one Newton-Raphson step (precision doubles):

| Initial bits | PFRCP result | vs AMD 14-bit | PFRSQRT result | vs AMD 15-bit |
|:---:|:---:|:---:|:---:|:---:|
| 8 (ARM min) | 16.0 bits | +2.0 margin | 15.4 bits | +0.4 margin |
| 12 (Apple) | 24.0 bits | +10.0 margin | 23.4 bits | +8.4 margin |

Worst-case (8-bit initial, PFRSQRT): `e_1 = (3/2) * (2^-8)^2 = 2.29e-5 < 2^-15 = 3.05e-5`. -- PASS

Note: PFRSQRT margin at ARM minimum is tight (0.4 bits). Safe because ARM spec guarantees >= 8 bits and FRSQRTS uses fused multiply.

### 1.5 ARMv8.0 Baseline Compliance

All instructions (FRECPE, FRSQRTE, FRECPS, FRSQRTS, FMUL vector) are ARMv8.0-A base FEAT_AdvSIMD. No extensions required. -- PASS

### 1.6 Bug Found: Same-Register Clobber in PFRCP and PFRSQRT

**Severity**: MEDIUM (correctness bug in specific edge case)

When `dest_reg == src_reg_a` (e.g., `PFRCP MM0, MM0`), FRECPE/FRSQRTE overwrites the destination before the NR step reads the original source.

The x86-64 backend handles this correctly by saving src to temp first. The old ARM64 PFRCP was NOT affected because it used FDIV which reads both operands before writing.

**Recommended fix**: Use `REG_V_TEMP` for initial estimate instead of `dest_reg`:

```c
// PFRCP fix:
host_arm64_FRECPE_V2S(block, REG_V_TEMP, src_reg_a);
host_arm64_FRECPS_V2S(block, dest_reg, REG_V_TEMP, src_reg_a);
host_arm64_FMUL_V2S(block, dest_reg, REG_V_TEMP, dest_reg);
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);

// PFRSQRT fix:
host_arm64_FRSQRTE_V2S(block, REG_V_TEMP, src_reg_a);
host_arm64_FMUL_V2S(block, dest_reg, REG_V_TEMP, REG_V_TEMP);
host_arm64_FRSQRTS_V2S(block, dest_reg, dest_reg, src_reg_a);
host_arm64_FMUL_V2S(block, dest_reg, REG_V_TEMP, dest_reg);
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);
```

### 1.7 Phase 1 Summary

| Check | Result |
|-------|--------|
| FRECPE/FRSQRTE/FRECPS/FRSQRTS encoding | PASS |
| Rd/Rn/Rm field placement | PASS |
| FRECPS/FRSQRTS operand order | PASS |
| PFRCP precision >= 14-bit | PASS |
| PFRSQRT precision >= 15-bit | PASS (tight: 15.4 bits worst case) |
| ARMv8.0 baseline compliance | PASS |
| Same-register edge case (dest==src) | **FAIL** -- see 1.6 |

**Phase 1 Verdict: CONDITIONAL PASS** -- fix same-register clobber before merging.

---

## Phase 2: PC-Relative BL for Intra-Pool Stub Calls

### 2.1 BL Opcode Encoding

`OPCODE_BL = 0x94000000`. Format: bit[31]=1 (link), bits[30:26]=00101, bits[25:0]=imm26.
Cross-reference: B is `0x14000000` (bit[31]=0). Only difference is bit 31. -- PASS

### 2.2 OFFSET26 Macro

```c
#define OFFSET26(offset)  ((offset >> 2) & 0x03ffffff)
```
Divides byte offset by 4, masks to 26 bits for two's complement. Verified with test vectors (+4, -4, +128MB-4, -128MB). -- PASS

### 2.3 offset_is_26bit Range Check

Range: -128MB to +128MB-1. JIT pool size is ~120MB, guaranteed within BL range. -- PASS

### 2.4 codegen_alloc BEFORE Offset Capture

`codegen_alloc(block, 4)` at line 1553, offset computed at line 1554. Alloc ensures stable `block_write_data`/`block_pos` for the subsequent `codegen_addlong`. Matches existing `host_arm64_B()` pattern. -- PASS

### 2.5 Off-by-One Check

ARM64 BL uses the instruction's own address as PC base (not PC+4 like ARM32). `&block_write_data[block_pos]` is exactly where BL will be written. `offset = dest - PC` is correct. -- PASS

### 2.6 Call Site Audit (26 sites)

All 26 `host_arm64_call_intrapool` targets verified as intra-pool stubs (assigned from `&block_write_data[block_pos]` during `codegen_backend_init()`):

| # | Target | Intra-pool? |
|---|--------|-------------|
| 1-9 | codegen_mem_load_{byte,word,long,quad,double,single} | YES |
| 10-23 | codegen_mem_store_{byte,word,long,quad,single,double} | YES |
| 24-26 | codegen_fp_round, codegen_fp_round_quad | YES |

-- PASS: All 26 targets are intra-pool stubs.

### 2.7 External Calls Left Unchanged (6 sites)

| Target | Handler | Verdict |
|--------|---------|---------|
| uop->p | CALL_FUNC / CALL_FUNC_RESULT / CALL_INSTRUCTION_FUNC | CORRECT |
| x86_int | FP_ENTER / MMX_ENTER | CORRECT |
| loadseg | LOAD_SEG | CORRECT |

-- PASS: All external calls correctly use absolute addressing.

### 2.8 codegen_JMP Optimization

`host_arm64_B(block, uop->p)` replaces `host_arm64_jump`. All `uop_JMP` targets are `codegen_exit_rout` (intra-pool). -- PASS

### 2.9 Phase 2 Summary

| Check | Result |
|-------|--------|
| OPCODE_BL encoding | PASS |
| OFFSET26 macro | PASS |
| offset_is_26bit range | PASS |
| codegen_alloc before offset capture | PASS |
| No off-by-one | PASS |
| All 26 call sites intra-pool | PASS |
| No external C function incorrectly converted | PASS |
| codegen_JMP -> host_arm64_B | PASS |

**Phase 2 Verdict: PASS** -- No bugs found.

---

## Overall Summary

| Phase | Verdict | Issues |
|-------|---------|--------|
| 1 | CONDITIONAL PASS | 1 bug: same-register clobber when dest==src (section 1.6) |
| 2 | PASS | No issues |

### Action Items

1. **[REQUIRED]** Fix same-register clobber in PFRCP and PFRSQRT (use REG_V_TEMP for initial estimate)
2. **[INFO]** PFRSQRT precision margin at ARM minimum (8-bit) is 0.4 bits above AMD spec. Safe but tight. If issues arise on specific Cortex-A53 implementations, add second NR step (+2 instructions).
